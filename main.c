#include "os.h"
#include "zBase.h"
#include "stream.h"
#include "muda_parser.h"
#include "lenstring.h"

#include <stdlib.h>

void AssertHandle(const char *reason, const char *file, int line, const char *proc) {
    OsConsoleOut(OsGetStdOutputHandle(), "%s (%s:%d) - Procedure: %s\n", reason, file, line, proc);
	TriggerBreakpoint();
}

void DeprecateHandle(const char *file, int line, const char *proc) {
    OsConsoleOut(OsGetStdOutputHandle(), "Deprecated procedure \"%s\" used at \"%s\":%d\n", proc, file, line);
}

static Directory_Iteration DirectoryIteratorPrintNoBin(const File_Info *info, void *user_context) {
	if (info->Atribute & File_Attribute_Hidden) return Directory_Iteration_Continue;
	if (StrMatch(info->Name, StringLiteral("bin"))) return Directory_Iteration_Continue;
	LogInfo("%s - %zu bytes\n", info->Path.Data, info->Size);
	return Directory_Iteration_Recurse;
}

typedef enum Compile_Type {
	Compile_Type_Project,
	Compile_Type_Solution,
} Compile_Type;

#define MAX_NODE_DATA_COUNT 8

typedef struct String_List_Node {
    String Data[MAX_NODE_DATA_COUNT];
    struct String_List_Node *Next;
}String_List_Node;

typedef struct String_List {
    String_List_Node Head;
    String_List_Node *Tail;
    Uint32 Used;
}String_List;

typedef struct Compiler_Config {
	Compile_Type Type;
	bool Optimization;
	String Build;
	String BuildDirectory;
	String_List Defines;
	String_List IncludeDirectory;
	String_List Source;
	String_List LibraryDirectory;
	String_List Library;
} Compiler_Config;

#define MUDA_VERSION_MAJOR 0
#define MUDA_VERSION_MINOR 1
#define MUDA_VERSION_PATCH 0

#define MUDA_BACKWARDS_COMPATIBLE_VERSION_MAJOR 0
#define MUDA_BACKWARDS_COMPATIBLE_VERSION_MINOR 1
#define MUDA_BACKWARDS_COMPATIBLE_VERSION_PATCH 0

INLINE_PROCEDURE Uint32 MudaMakeVersion(Uint32 major, Uint32 minor, Uint32 patch) {
    Assert(major <= UINT8_MAX && minor <= UINT8_MAX && patch <= UINT16_MAX);
    Uint32 version = patch | (minor << 16) | (major << 24);
    return version;
}

#define MUDA_CURRENT_VERSION MudaMakeVersion(MUDA_VERSION_MAJOR, MUDA_VERSION_MINOR, MUDA_VERSION_PATCH)
#define MUDA_BACKWARDS_COMPATIBLE_VERSION MudaMakeVersion(MUDA_BACKWARDS_COMPATIBLE_VERSION_MAJOR, \
                                                            MUDA_BACKWARDS_COMPATIBLE_VERSION_MINOR, \
                                                            MUDA_BACKWARDS_COMPATIBLE_VERSION_PATCH)

static bool IsListEmpty(String_List *list) {
    return list->Head.Next == NULL && list->Used == 0;
}

static void AddToList(String_List *dst, String string){
    Memory_Arena *scratch = ThreadScratchpad();
    if (dst->Used == MAX_NODE_DATA_COUNT){
        dst->Used = 0;
        dst->Tail->Next = PushSize(scratch, sizeof(String_List_Node));
        dst->Tail = dst->Tail->Next;
        dst->Tail->Next = NULL;
    }
    dst->Tail->Data[dst->Used] = string;
    dst->Used++;
}

static void ClearList(String_List *lst){
    lst->Used = 0;
    lst->Head.Next = NULL;
    lst->Tail = &lst->Head;
}

static void ReadList(String_List *dst, String data){
    Memory_Arena *scratch = ThreadScratchpad();

    Int64 prev_pos = 0;
    Int64 curr_pos = 0;

    while (curr_pos < data.Length) {
        // Remove prefixed spaces (postfix spaces in the case of 2+ iterations)
        while (curr_pos < data.Length && isspace(data.Data[curr_pos])) 
            curr_pos += 1;
        prev_pos = curr_pos;

        // Count number of characters in the string
        while (curr_pos < data.Length && !isspace(data.Data[curr_pos])) 
            curr_pos += 1;

        AddToList(dst, StrDuplicateArena(StringMake(data.Data + prev_pos, curr_pos - prev_pos), scratch));
    }
}

void CompilerConfigInit(Compiler_Config *config) {
    memset(config, 0, sizeof(*config));
    config->Defines.Tail = &config->Defines.Head;
    config->IncludeDirectory.Tail = &config->IncludeDirectory.Head;
    config->Source.Tail = &config->Source.Head;
    config->LibraryDirectory.Tail = &config->Source.Head;
    config->Library.Tail = &config->Source.Head;
}

void PushDefaultCompilerConfig(Compiler_Config *config, Compiler_Kind compiler) {
    if (config->BuildDirectory.Length == 0) {
        config->BuildDirectory = StringLiteral("./bin");
    }

    if (config->Build.Length == 0) {
        config->Build = StringLiteral("output");
    }

    if (IsListEmpty(&config->Source)) {
        AddToList(&config->Source, StringLiteral("*.c"));
    }

    if (compiler == Compiler_Kind_CL && IsListEmpty(&config->Defines)) {
        AddToList(&config->Defines, StringLiteral("_CRT_SECURE_NO_WARNINGS"));
    }
}

void LoadCompilerConfig(Compiler_Config *config, Uint8* data, int length) {
    if (!data) return;
	Memory_Arena *scratch = ThreadScratchpad();
    
    Muda_Parser prsr = MudaParseInit(data, length);

    Uint32 version = 0;
    Uint32 major = 0, minor = 0, patch = 0;

    if (MudaParseNext(&prsr)){
        if (prsr.Token.Kind == Muda_Token_Tag && StrMatchCaseInsensitive(prsr.Token.Data.Tag.Title, StringLiteral("version"))) {
            if (prsr.Token.Data.Tag.Value.Data){
                if (sscanf(prsr.Token.Data.Tag.Value.Data, "%d.%d.%d", &major, &minor, &patch) != 3) {
                    FatalError("Error: Bad file version\n");
                }
                version = MudaMakeVersion(major, minor, patch);
            } else {
                FatalError("Error: Version info missing\n");
            }
        } else {
            FatalError("Error: Version tag missing at top of file\n");
        }
    }

    if (version < MUDA_BACKWARDS_COMPATIBLE_VERSION || version > MUDA_CURRENT_VERSION) {
        String error = FmtStr(scratch, "Version %d.%d.%d not supported. \n"
            "Minimum version supported: %d.%d.%d\n"
            "Current version: %d.%d.%d\n",
            major, minor, patch,
            MUDA_BACKWARDS_COMPATIBLE_VERSION_MAJOR,
            MUDA_BACKWARDS_COMPATIBLE_VERSION_MINOR,
            MUDA_BACKWARDS_COMPATIBLE_VERSION_PATCH,
            MUDA_VERSION_MAJOR,
            MUDA_VERSION_MINOR,
            MUDA_VERSION_PATCH);
        FatalError(error.Data);
    }

    while (MudaParseNext(&prsr)) {
        // Temporary
        if (prsr.Token.Kind == Muda_Token_Tag){
            if (prsr.Token.Data.Tag.Value.Data)
                LogInfo("[TAG] %s : %s\n", prsr.Token.Data.Tag.Title.Data, prsr.Token.Data.Tag.Value.Data);
            else 
                LogInfo("[TAG] %s\n", prsr.Token.Data.Tag.Title.Data);
        }
        if (prsr.Token.Kind != Muda_Token_Property) continue;

        if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("Type"))){
            if (StrMatch(prsr.Token.Data.Property.Value, StringLiteral("Project")))
                config->Type = Compile_Type_Project;
            else
                config->Type = Compile_Type_Solution;
        }

        else if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("Optimization")))
            config->Optimization = StrMatch(prsr.Token.Data.Property.Value, StringLiteral("true"));

        else if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("BuildDirectory"))) {
            config->BuildDirectory.Length = prsr.Token.Data.Property.Value.Length;
            config->BuildDirectory.Data = prsr.Token.Data.Property.Value.Data;
        }

        else if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("Build"))) {
            config->Build.Length = prsr.Token.Data.Property.Value.Length;
            config->Build.Data = prsr.Token.Data.Property.Value.Data;
        }

        else if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("Define")))
            ReadList(&config->Defines, prsr.Token.Data.Property.Value);

        else if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("IncludeDirectory")))
            ReadList(&config->IncludeDirectory, prsr.Token.Data.Property.Value);

        else if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("Source")))
            ReadList(&config->Source, prsr.Token.Data.Property.Value);

        else if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("LibraryDirectory")))
            ReadList(&config->LibraryDirectory, prsr.Token.Data.Property.Value);

        else if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("Library")))
            ReadList(&config->Library, prsr.Token.Data.Property.Value);
    }
    if (prsr.Token.Kind == Muda_Token_Error)
        LogError ("%s at Line %d, Column %d\n", prsr.Token.Data.Error.Desc, prsr.Token.Data.Error.Line, prsr.Token.Data.Error.Column);
}

void Compile(Compiler_Config *config, Compiler_Kind compiler) {
	Memory_Arena *scratch = ThreadScratchpad();

	Assert(config->Type == Compile_Type_Project);

	Temporary_Memory temp = BeginTemporaryMemory(scratch);

	Out_Stream out;
	OutCreate(&out, MemoryArenaAllocator(scratch));
        
    Uint32 result = OsCheckIfPathExists(config->BuildDirectory);
    if (result == Path_Does_Not_Exist) {
        if (!OsCreateDirectoryRecursively(config->BuildDirectory)) {
            String error = FmtStr(scratch, "Failed to create directory %s!", config->BuildDirectory.Data);
            FatalError(error.Data);
        }
    }
    else if (result == Path_Exist_File) {
        String error = FmtStr(scratch, "%s: Path exist but is a file!\n", config->BuildDirectory.Data);
        FatalError(error.Data);
    }

    if (compiler == Compiler_Kind_CL){
        // For CL, we output intermediate files to "BuildDirectory/int"
        String intermediate;
        if (config->BuildDirectory.Data[config->BuildDirectory.Length - 1] == '/') {
            intermediate = FmtStr(scratch, "%sint", config->BuildDirectory.Data);
        }
        else {
            intermediate = FmtStr(scratch, "%s/int", config->BuildDirectory.Data);
        }

        result = OsCheckIfPathExists(intermediate);
        if (result == Path_Does_Not_Exist) {
            if (!OsCreateDirectoryRecursively(intermediate)) {
                String error = FmtStr(scratch, "Failed to create directory %s!", intermediate.Data);
                FatalError(error.Data);
            }
        }
        else if (result == Path_Exist_File) {
            String error = FmtStr(scratch, "%s: Path exist but is a file!\n", intermediate.Data);
            FatalError(error.Data);
        }

        OutFormatted(&out, "cl -nologo -Zi -EHsc ");

        if (config->Optimization)
            OutFormatted(&out, "-O2 ");
        else
            OutFormatted(&out, "-Od ");

        for (String_List_Node* ntr = &config->Defines.Head; ntr && config->Defines.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->Defines.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "-D%s ", ntr->Data[i].Data);
        }

        for (String_List_Node* ntr = &config->IncludeDirectory.Head; ntr && config->IncludeDirectory.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->IncludeDirectory.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "-I%s ", ntr->Data[i].Data);
        }

        for (String_List_Node* ntr = &config->Source.Head; ntr && config->Source.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->Source.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "\"%s\" ", ntr->Data[i].Data);
        }

        // TODO: Make directory if not present, need to add OS api for making directory!
        // Until then make "bin/int" directory manually :(
        OutFormatted(&out, "-Fo\"%s/int/\" ", config->BuildDirectory.Data);
        OutFormatted(&out, "-Fd\"%s/\" ", config->BuildDirectory.Data);
        OutFormatted(&out, "-link ");
        OutFormatted(&out, "-out:\"%s/%s.exe\" ", config->BuildDirectory.Data, config->Build.Data);
        OutFormatted(&out, "-pdb:\"%s/%s.pdb\" ", config->BuildDirectory.Data, config->Build.Data);

        for (String_List_Node* ntr = &config->LibraryDirectory.Head; ntr && config->LibraryDirectory.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->LibraryDirectory.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "-LIBPATH:\"%s\" ", ntr->Data[i].Data);
        }

        for (String_List_Node* ntr = &config->Library.Head; ntr && config->Library.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->Library.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "\"%s\" ", ntr->Data[i].Data);
        }
    } else if (compiler == Compiler_Kind_GCC || compiler == Compiler_Kind_CLANG){
        if (compiler == Compiler_Kind_GCC) OutFormatted(&out, "gcc -pipe ");
        else OutFormatted(&out, "clang -gcodeview -w ");

        if (config->Optimization) OutFormatted(&out, "-O2 ");
        else OutFormatted(&out, "-g ");

        for (String_List_Node* ntr = &config->Defines.Head; ntr && config->Defines.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->Defines.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "-D%s ", ntr->Data[i].Data);
        }

        for (String_List_Node* ntr = &config->IncludeDirectory.Head; ntr && config->IncludeDirectory.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->IncludeDirectory.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "-I%s ", ntr->Data[i].Data);
        }

        for (String_List_Node* ntr = &config->Source.Head; ntr && config->Source.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->Source.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "%s ", ntr->Data[i].Data);
        }

        if (PLATFORM_OS_LINUX)
            OutFormatted(&out, "-o %s/%s.out ", config->BuildDirectory.Data, config->Build.Data);
        else if (PLATFORM_OS_WINDOWS)
            OutFormatted(&out, "-o %s/%s.exe ", config->BuildDirectory.Data, config->Build.Data);

        for (String_List_Node* ntr = &config->LibraryDirectory.Head; ntr && config->LibraryDirectory.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->LibraryDirectory.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "-L%s ", ntr->Data[i].Data);
        }

        for (String_List_Node* ntr = &config->Library.Head; ntr && config->Library.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->Library.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "-l%s ", ntr->Data[i].Data);
        }
    }

    ThreadContext.Allocator = MemoryArenaAllocator(scratch);
    String cmd_line = OutBuildString(&out);
    ThreadContext.Allocator = NullMemoryAllocator();

	LogInfo("Command Line: %s\n", cmd_line.Data);

	OsExecuteCommandLine(cmd_line);

	EndTemporaryMemory(&temp);
}

static void LogProcedure(void *agent, Log_Kind kind, const char *fmt, va_list list) {
    void *fp = (kind == Log_Kind_Info) ? OsGetStdOutputHandle() : OsGetErrorOutputHandle();
    OsConsoleOutV(fp, fmt, list);
}

static void FatalErrorProcedure(const char *message) {
    OsConsoleWrite("%s", message);
    exit(1);
}

void PrintCompilerConfig(Compiler_Config conf){
    OsConsoleWrite("\nType                : %s", conf.Type == Compile_Type_Project ? "Project" : "Solution");
    OsConsoleWrite("\nOptimization        : %s", conf.Optimization ? "True" : "False");
    OsConsoleWrite("\nBuild               : %s", conf.Build.Data);
    OsConsoleWrite("\nBuild Directory     : %s", conf.BuildDirectory.Data);

    OsConsoleWrite("\nSource              : ");
    for (String_List_Node* ntr = &conf.Source.Head; ntr && conf.Source.Used; ntr = ntr->Next){
        int len = ntr->Next ? 8 : conf.Source.Used;
        for (int i = 0; i < len; i ++) OsConsoleWrite("%s ", ntr->Data[i].Data);
    }
    OsConsoleWrite("\nDefines             : ");
    for (String_List_Node* ntr = &conf.Defines.Head; ntr && conf.Defines.Used; ntr = ntr->Next){
        int len = ntr->Next ? 8 : conf.Defines.Used;
        for (int i = 0; i < len; i ++) OsConsoleWrite("%s ", ntr->Data[i].Data);
    }
    OsConsoleWrite("\nInclude Directories : ");
    for (String_List_Node* ntr = &conf.IncludeDirectory.Head; ntr && conf.IncludeDirectory.Used; ntr = ntr->Next){
        int len = ntr->Next ? 8 : conf.IncludeDirectory.Used;
        for (int i = 0; i < len; i ++) OsConsoleWrite("%s ", ntr->Data[i].Data);
    }
    OsConsoleWrite("\nLibrary Directories : ");
    for (String_List_Node* ntr = &conf.LibraryDirectory.Head; ntr && conf.LibraryDirectory.Used; ntr = ntr->Next){
        int len = ntr->Next ? 8 : conf.LibraryDirectory.Used;
        for (int i = 0; i < len; i ++) OsConsoleWrite("%s ", ntr->Data[i].Data);
    }
    OsConsoleWrite("\nLibraries           : ");
    for (String_List_Node* ntr = &conf.Library.Head; ntr && conf.Library.Used; ntr = ntr->Next){
        int len = ntr->Next ? 8 : conf.Library.Used;
        for (int i = 0; i < len; i ++) OsConsoleWrite("%s ", ntr->Data[i].Data);
    }
    OsConsoleWrite("\n");
}

typedef struct Muda_Option {
    String Name;
    String Desc;
    void (*Proc)();
} Muda_Option;

void OptHelp();
void OptDefault();
void OptSetup();
void OptVersion();

static const Muda_Option Options[] = {
    { StringLiteralExpand("version"), StringLiteralExpand("Check the version of Muda installed"), OptVersion },
    { StringLiteralExpand("default"), StringLiteralExpand("Display default configuration"), OptDefault },
    { StringLiteralExpand("setup"), StringLiteralExpand("Setup a Muda build system"), OptSetup },
    { StringLiteralExpand("help"), StringLiteralExpand("Muda description and list all the command"), OptHelp },
};

void OptHelp() {
    // Dont judge me pls I was bored
    OptVersion();
    LogInfo("Usage:\n\tpath/to/muda [-flags] [build_script]\n\n");
    LogInfo("Flags:\n");

    for (int opt_i = 0; opt_i < ArrayCount(Options); ++opt_i) {
        LogInfo("\t-%-10s: %s\n", Options[opt_i].Name.Data, Options[opt_i].Desc.Data);
    }

    LogInfo("\nRepository:\n\thttps://github.com/IT-Club-Pulchowk/muda\n\n");
}

void OptDefault() {
    LogInfo(" ___                             \n(|  \\  _ |\\  _,        |\\_|_  ,  \n |   ||/ |/ / |  |  |  |/ |  / \\_\n(\\__/ |_/|_/\\/|_/ \\/|_/|_/|_/ \\/ \n         |)                      \n");
    Compiler_Config def;
    CompilerConfigInit(&def);
    PushDefaultCompilerConfig(&def, Compiler_Kind_NULL);
    PrintCompilerConfig(def);
    LogInfo("\n");
}

void OptSetup() {
    OptVersion();

    OsConsoleWrite("Muda Configuration:\n");

    Compiler_Config def;
    CompilerConfigInit(&def);
    PushDefaultCompilerConfig(&def, Compiler_Kind_NULL);

    // We take these as default
    // Type = Solution
    // Optimization = false

    // TODO: We are not using what we have input from console yet... :(
    // TODO: Remove white spaces at start and end of the string as well
    char read_buffer[256];

    OsConsoleWrite("Build Directory (default: %s) #\n   > ", def.BuildDirectory.Data);
    OsConsoleRead(read_buffer, sizeof(read_buffer));

    OsConsoleWrite("Build Executable (default: %s) #\n   > ", def.Build.Data);
    OsConsoleRead(read_buffer, sizeof(read_buffer));

    OsConsoleWrite("Defines #\n   > ");
    OsConsoleRead(read_buffer, sizeof(read_buffer));

    OsConsoleWrite("Include Directory #\n   > ");
    OsConsoleRead(read_buffer, sizeof(read_buffer));

    OsConsoleWrite("Source (default: %s) #\n   > ", def.Source.Head.Data[0].Data);
    OsConsoleRead(read_buffer, sizeof(read_buffer));

    OsConsoleWrite("Library Directory #\n   > ");
    OsConsoleRead(read_buffer, sizeof(read_buffer));

    OsConsoleWrite("Input Library #\n   > ");
    OsConsoleRead(read_buffer, sizeof(read_buffer));

    OsConsoleWrite("\n");
}

void OptVersion() {
    LogInfo("                        \n               _|   _,  \n/|/|/|  |  |  / |  / |  \n | | |_/ \\/|_/\\/|_/\\/|_/\n\n");
    LogInfo("Muda v %d.%d.%d\n\n", MUDA_VERSION_MAJOR, MUDA_VERSION_MINOR, MUDA_VERSION_PATCH);
}

int main(int argc, char *argv[]) {
    InitThreadContext(NullMemoryAllocator(), MegaBytes(512), (Log_Agent){ .Procedure = LogProcedure }, FatalErrorProcedure);

    OsSetupConsole();

    // If there are 2 arguments, then the 2nd argument could be options
    // Options are the strings that start with "-", we don't allow "- options", only allow "-option"
    if (argc == 2 && argv[1][0] == '-') {
        String option = StringMake(argv[1] + 1, strlen(argv[1] + 1));

        for (int opt_i = 0; opt_i < ArrayCount(Options); ++opt_i) {
            if (StrMatchCaseInsensitive(option, Options[opt_i].Name)) {
                Options[opt_i].Proc();
                return 0;
            }
        }

        LogError("ERROR: Unrecognized option: %s\n", option.Data);
        return 1;
    }

	Compiler_Kind compiler = OsDetectCompiler();
    if (compiler == Compiler_Kind_NULL) {
        return 1;
    }

    Compiler_Config config;
    CompilerConfigInit(&config);

    String config_path = { 0,0 };

    const String LocalMudaFile = StringLiteral("build.muda");
    if (OsCheckIfPathExists(LocalMudaFile) == Path_Exist_File) {
        config_path = LocalMudaFile;
    }
    else {
        String muda_user_path = OsGetUserConfigurationPath(StringLiteral("muda/config.muda"));
        if (OsCheckIfPathExists(muda_user_path) == Path_Exist_File) {
            config_path = muda_user_path;
        }
    }

    if (config_path.Length) {
        Memory_Arena *scratch = ThreadScratchpad();
        Temporary_Memory temp = BeginTemporaryMemory(scratch);

        File_Handle fp = OsFileOpen(config_path, File_Mode_Read);
        if (fp.PlatformFileHandle) {
            Ptrsize size = OsFileGetSize(fp);
            const Ptrsize MAX_ALLOWED_MUDA_FILE_SIZE = MegaBytes(32);

            if (size > MAX_ALLOWED_MUDA_FILE_SIZE) {
                float max_size = (float)MAX_ALLOWED_MUDA_FILE_SIZE / (1024 * 1024);
                String error = FmtStr(scratch, "Fatal Error: File %s too large. Max memory: %.3fMB!\n", config_path.Data, max_size);
                FatalError(error.Data);
            }

            Uint8 *buffer = PushSize(scratch, size + 1);
            if (OsFileRead(fp, buffer, size)) {
                buffer[size] = 0;
                LoadCompilerConfig(&config, buffer, size);
            }
            else {
                LogError("ERROR: Could not read the configuration file %s!\n", config_path.Data);
            }

            OsFileClose(fp);
        }
        else {
            LogError("ERROR: Could not open the configuration file %s!\n", config_path.Data);
        }

        EndTemporaryMemory(&temp);
    }

    {
        Memory_Arena *scratch = ThreadScratchpad();

        Temporary_Memory temp = BeginTemporaryMemory(scratch);

        Out_Stream out;
        OutCreate(&out, MemoryArenaAllocator(scratch));

        for (int argi = 1; argi < argc; ++argi) {
            OutFormatted(&out, "%s ", argv[argi]);
        }

        ThreadContext.Allocator = MemoryArenaAllocator(scratch);
        String cmd_line = OutBuildString(&out);
        ThreadContext.Allocator = NullMemoryAllocator();
        LoadCompilerConfig(&config, cmd_line.Data, cmd_line.Length);

        EndTemporaryMemory(&temp);
    }

    PushDefaultCompilerConfig(&config, compiler);

    Compile(&config, compiler);

	return 0;
}

#include "zBase.c"

#if PLATFORM_OS_WINDOWS == 1
#include "os_windows.c"
#endif
#if PLATFORM_OS_LINUX == 1
#include "os_linux.c"
#endif

#include "os.h"
#include "zBase.h"
#include "stream.h"
#include "muda_parser.h"
#include "lenstring.h"
#include "cmd_line.h"

static Directory_Iteration DirectoryIteratorPrintNoBin(const File_Info *info, void *user_context) {
	if (info->Atribute & File_Attribute_Hidden) return Directory_Iteration_Continue;
	if (StrMatch(info->Name, StringLiteral("bin"))) return Directory_Iteration_Continue;
	LogInfo("%s - %zu bytes\n", info->Path.Data, info->Size);
	return Directory_Iteration_Recurse;
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

        StringListAdd(dst, StrDuplicateArena(StringMake(data.Data + prev_pos, curr_pos - prev_pos), scratch));
    }
}

void LoadCompilerConfig(Compiler_Config *config, Uint8* data, int length, bool check_ver) {
    if (!data) return;
	Memory_Arena *scratch = ThreadScratchpad();
    
    Muda_Parser prsr = MudaParseInit(data, length);

    Uint32 version = 0;
    Uint32 major = 0, minor = 0, patch = 0;

    if (check_ver){
        if (MudaParseNext(&prsr)){
            if (prsr.Token.Kind == Muda_Token_Tag && StrMatchCaseInsensitive(prsr.Token.Data.Tag.Title, StringLiteral("version"))) {
                if (prsr.Token.Data.Tag.Value.Data){
                    if (sscanf(prsr.Token.Data.Tag.Value.Data, "%d.%d.%d", &major, &minor, &patch) != 3) {
                        FatalError("Error: Bad file version\n");
                    }
                } else {
                    FatalError("Error: Version info missing\n");
                }
            } else {
                FatalError("Error: Version tag missing at top of file\n");
            }
        }
        version = MudaMakeVersion(major, minor, patch);

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

//
// Base setup
//

void AssertHandle(const char *reason, const char *file, int line, const char *proc) {
    OsConsoleOut(OsGetStdOutputHandle(), "%s (%s:%d) - Procedure: %s\n", reason, file, line, proc);
    TriggerBreakpoint();
}

void DeprecateHandle(const char *file, int line, const char *proc) {
    OsConsoleOut(OsGetStdOutputHandle(), "Deprecated procedure \"%s\" used at \"%s\":%d\n", proc, file, line);
}

static void LogProcedure(void *agent, Log_Kind kind, const char *fmt, va_list list) {
    void *fp = (kind == Log_Kind_Info) ? OsGetStdOutputHandle() : OsGetErrorOutputHandle();
    OsConsoleOutV(fp, fmt, list);
}

static void FatalErrorProcedure(const char *message) {
    OsConsoleWrite("%s", message);
    OsProcessExit(0);
}

//
//
//

int main(int argc, char *argv[]) {
    InitThreadContext(NullMemoryAllocator(), MegaBytes(512), (Log_Agent){ .Procedure = LogProcedure }, FatalErrorProcedure);

    OsSetupConsole();

    if (HandleCommandLineArguments(argc, argv))
        return 0;

	Compiler_Kind compiler = OsDetectCompiler();
    if (compiler == Compiler_Kind_NULL)
        return 1;

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
                LoadCompilerConfig(&config, buffer, size, true);
            } else {
                LogError("ERROR: Could not read the configuration file %s!\n", config_path.Data);
            }

            OsFileClose(fp);
        } else {
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
        LoadCompilerConfig(&config, cmd_line.Data, cmd_line.Length, false);

        EndTemporaryMemory(&temp);
    }

    PushDefaultCompilerConfig(&config, compiler);

    Compile(&config, compiler);

	return 0;
}

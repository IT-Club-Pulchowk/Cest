#include "os.h"
#include "zBase.h"
#include "stream.h"
#include "muda_parser.h"
#include "lenstring.h"

#include <stdlib.h>

void AssertHandle(const char *reason, const char *file, int line, const char *proc) {
	fprintf(stderr, "%s (%s:%d) - Procedure: %s\n", reason, file, line, proc);
	TriggerBreakpoint();
}

void DeprecateHandle(const char *file, int line, const char *proc) {
	fprintf(stderr, "Deprecated procedure \"%s\" used at \"%s\":%d\n", proc, file, line);
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
    for (int i = 0; i < MAX_NODE_DATA_COUNT; i++) {
        lst->Head.Data[i].Data = NULL;        
        lst->Head.Data[i].Length = 0;        
    }
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

void SetDefaultCompilerConfig(Compiler_Config *config) {
	// TODO: Use sensible default values

	config->BuildDirectory = StringLiteral("./bin");
	config->Build = StringLiteral("bootstrap");

	config->Type = Compile_Type_Project;

	config->Optimization = false;

    Uint8 str[] = "ASSERTION_HANDLED DEPRECATION_HANDLED _CRT_SECURE_NO_WARNINGS"; 
	ReadList(&config->Defines, (String){strlen(str), str});

    Uint8 str2[] = "main.c"; 
	ReadList(&config->Source, (String){strlen(str2), str2});
}

void LoadCompilerConfig(Compiler_Config *config, Uint8* data, int length) {
    if (!data) return;
	Memory_Arena *scratch = ThreadScratchpad();
    
    Muda_Parser prsr = MudaParseInit(data, length);
    while (MudaParseNext(&prsr)) {
        // Temporary
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
	String cmdline = {0, 0};

	Memory_Arena *scratch = ThreadScratchpad();

	Assert(config->Type == Compile_Type_Project);

	Temporary_Memory temp = BeginTemporaryMemory(scratch);

	Out_Stream out;
	OutCreate(&out, MemoryArenaAllocator(scratch));
        
    Uint32 result = CheckIfPathExists(config->BuildDirectory);
    if (result == Path_Does_Not_Exist) {
        if (!CreateDirectoryRecursively(config->BuildDirectory)) {
            String error = FmtStr(scratch, "Failed to create directory %s!", config->BuildDirectory.Data);
            FatalError(error.Data);
        }
    }
    else if (result == Path_Exist_File) {
        String error = FmtStr(scratch, "%s: Path exist but is a file!\n", config->BuildDirectory.Data);
        FatalError(error.Data);
    }
	// Defaults
    if (compiler == Compiler_Kind_CL){
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
            OutFormatted(&out, "-o%s/%s.out ", config->BuildDirectory.Data, config->Build.Data);
        else if (PLATFORM_OS_WINDOWS)
            OutFormatted(&out, "-o%s/%s.exe ", config->BuildDirectory.Data, config->Build.Data);

        for (String_List_Node* ntr = &config->LibraryDirectory.Head; ntr && config->LibraryDirectory.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->LibraryDirectory.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "-L%s ", ntr->Data[i].Data);
        }

        for (String_List_Node* ntr = &config->Library.Head; ntr && config->Library.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->Library.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "-l%s ", ntr->Data[i].Data);
        }
    }

	cmdline = OutBuildString(&out);

	EndTemporaryMemory(&temp);

	LogInfo("Command Line: %s\n", cmdline.Data);

	LaunchCompilation(compiler, cmdline);
}

static void LogProcedure(void *agent, Log_Kind kind, const char *fmt, va_list list) {
    FILE *fp = ((kind == Log_Kind_Info) ? stdout : stderr);
    vfprintf(fp, fmt, list);
}

static void FatalErrorProcedure(const char *message) {
    fprintf(stderr, "%s", message);
    exit(1);
}

int main(int argc, char *argv[]) {
    Memory_Arena arena = MemoryArenaCreate(MegaBytes(512));

    InitThreadContext(MemoryArenaAllocator(&arena), 
        MegaBytes(128), (Log_Agent){ .Procedure = LogProcedure }, FatalErrorProcedure);

	Compiler_Kind compiler = DetectCompiler();
    //if (compiler == Compiler_Kind_NULL) {
    //    return 1;
    //}

	Memory_Arena *scratch = ThreadScratchpad();

    String config_file = { 0,0 };

    const String LocalMudaFile = StringLiteral("build.muda");
    if (CheckIfPathExists(LocalMudaFile) == Path_Exist_File) {
        config_file = LocalMudaFile;
    } else {
        String global_muda_file = GetGlobalConfigurationFile();
        if (CheckIfPathExists(global_muda_file) == Path_Exist_File) {
            config_file = global_muda_file;
        }
    }

	Compiler_Config compiler_config;
    CompilerConfigInit(&compiler_config);

    SetDefaultCompilerConfig(&compiler_config);

    SetDefaultCompilerConfig(&compiler_config);
    if (config_file.Length) {
        FILE *fp = fopen(config_file.Data, "rb");
        fseek(fp, 0L, SEEK_END);
        int size = ftell(fp);
        fseek(fp, 0L, SEEK_SET);

        Uint8 *config = PushSize(scratch, size + 1);
        fread(config, size, 1, fp);
        config[size] = 0;
        fclose(fp);

        LoadCompilerConfig(&compiler_config, config, size);
    }

    {
        Temporary_Memory temp = BeginTemporaryMemory(scratch);

        Out_Stream out;
        OutCreate(&out, MemoryArenaAllocator(scratch));

        for (int argi = 1; argi < argc; ++argi) {
            OutFormatted(&out, "%s ", argv[argi]);
        }

        String cmd_line = OutBuildString(&out);
        LoadCompilerConfig(&compiler_config, cmd_line.Data, cmd_line.Length);

        EndTemporaryMemory(&temp);
    }

    Compile(&compiler_config, compiler);

#if 0
	if (argc != 2) {
		LogError("\nUsage: %s <directory name>\n", argv[0]);
		return 1;
	}

	const char *dir = argv[1];

	IterateDirectroy(dir, DirectoryIteratorPrintNoBin, NULL);
#endif

	return 0;
}

#include "zBase.c"

#if PLATFORM_OS_WINDOWS == 1
#include "os_windows.c"
#endif
#if PLATFORM_OS_LINUX == 1
#include "os_linux.c"
#endif

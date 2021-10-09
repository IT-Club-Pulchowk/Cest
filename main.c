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

typedef struct Node {
    String Data;
    struct Node *Next;
}Node;

typedef struct String_List {
    bool IsEmpty;
    Node Head;
    Node *Tail;
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

static void ReadList(String_List *dst, String data){
    if (data.Length <= 0){
        dst->IsEmpty = true;
        return;
    }

    dst->IsEmpty = false;
    for (int i = 0; i < data.Length; i++) {
        if (isspace(data.Data[i]))
            data.Data[i] = 0;
    }

    dst->Head.Data.Data = data.Data;
    dst->Head.Data.Length = strlen(data.Data);
    dst->Head.Next = NULL;
    dst->Tail = &dst->Head;

    Memory_Arena *scratch = ThreadScratchpad();
    for (int i = 0; i < data.Length; i++) {
        if (!data.Data[i] && strlen(data.Data + i + 1)){
            dst->Tail->Next = PushSize(scratch, sizeof(Node));
            dst->Tail->Next->Data.Data = data.Data + i + 1;
            dst->Tail->Next->Data.Length = strlen(data.Data + i + 1);
            dst->Tail = dst->Tail->Next;
            dst->Tail->Next = NULL;
        }        
    }
}

void SetDefaultCompilerConfig(Compiler_Config *config) {
	// TODO: Use sensible default values

	config->BuildDirectory = StringLiteral("./bin");
	config->Build = StringLiteral("bootstrap");

	config->Type = Compile_Type_Project;

	config->Optimization = false;

    Uint8 str[] = "ASSERTION_HANDLED DEPRECATION_HANDLED _CRT_SECURE_NO_WARNINGS"; 
	ReadList(&config->Defines, (String){strlen(str), str});

	config->IncludeDirectory.IsEmpty = true;

    Uint8 str2[] = "main.c"; 
	ReadList(&config->Source, (String){strlen(str2), str2});

	config->LibraryDirectory.IsEmpty = true;

	config->Library.IsEmpty = true;
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

        if (!config->Defines.IsEmpty){
            Node *head = &config->Defines.Head;
            while (head){
                OutFormatted(&out, "-D%s ", head->Data.Data);
                head = head->Next;
            }
        }

        if (!config->IncludeDirectory.IsEmpty){
            Node *head = &config->IncludeDirectory.Head;
            while (head){
                OutFormatted(&out, "-I%s ", head->Data.Data);
                head = head->Next;
            }
        }

        if (!config->Source.IsEmpty){
            Node *head = &config->Source.Head;
            while (head){
                OutFormatted(&out, "\"%s\" ", head->Data.Data);
                head = head->Next;
            }
        }

        // TODO: Make directory if not present, need to add OS api for making directory!
        // Until then make "bin/int" directory manually :(
        OutFormatted(&out, "-Fo\"%s/int/\" ", config->BuildDirectory.Data);
        OutFormatted(&out, "-Fd\"%s/\" ", config->BuildDirectory.Data);
        OutFormatted(&out, "-link ");
        OutFormatted(&out, "-out:\"%s/%s.exe\" ", config->BuildDirectory.Data, config->Build.Data);
        OutFormatted(&out, "-pdb:\"%s/%s.pdb\" ", config->BuildDirectory.Data, config->Build.Data);

        if (!config->LibraryDirectory.IsEmpty) {
            Node *head = &config->LibraryDirectory.Head;
            while (head){
                OutFormatted(&out, "-LIBPATH:\"%s\" ", head->Data.Data);
                head = head->Next;
            }
        }

        if (!config->Library.IsEmpty) {
            Node *head = &config->Library.Head;
            while (head){
                OutFormatted(&out, "\"%s\" ", head->Data.Data);
                head = head->Next;
            }
        }
    } else if (compiler == Compiler_Kind_GCC || compiler == Compiler_Kind_CLANG){
        if (compiler == Compiler_Kind_GCC) OutFormatted(&out, "gcc -pipe ");
        else OutFormatted(&out, "clang -gcodeview -w ");

        if (config->Optimization) OutFormatted(&out, "-O2 ");
        else OutFormatted(&out, "-g ");

        if (!config->Defines.IsEmpty){
            Node *head = &config->Defines.Head;
            while (head){
                OutFormatted(&out, "-D%s ", head->Data.Data);
                head = head->Next;
            }
        }

        if (!config->IncludeDirectory.IsEmpty){
            Node *head = &config->IncludeDirectory.Head;
            while (head){
                OutFormatted(&out, "-I%s ", head->Data.Data);
                head = head->Next;
            }
        }

        if (!config->Source.IsEmpty){
            Node *head = &config->Source.Head;
            while (head){
                OutFormatted(&out, "%s ", head->Data.Data);
                head = head->Next;
            }
        }

        if (PLATFORM_OS_LINUX)
            OutFormatted(&out, "-o%s/%s.out ", config->BuildDirectory.Data, config->Build.Data);
        else if (PLATFORM_OS_WINDOWS)
            OutFormatted(&out, "-o%s/%s.exe ", config->BuildDirectory.Data, config->Build.Data);

        if (!config->LibraryDirectory.IsEmpty){
            Node *head = &config->LibraryDirectory.Head;
            while (head){
                OutFormatted(&out, "-L%s ", head->Data.Data);
                head = head->Next;
            }
        }

        if (!config->Library.IsEmpty){
            Node *head = &config->Library.Head;
            while (head){
                OutFormatted(&out, "-l%s ", head->Data.Data);
                head = head->Next;
            }
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
    if (compiler == Compiler_Kind_NULL) {
        return 1;
    }

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

    if (config_file.Length) {
        FILE *fp = fopen(config_file.Data, "rb");
        fseek(fp, 0L, SEEK_END);
        int size = ftell(fp);
        fseek(fp, 0L, SEEK_SET);

        Uint8 *config = PushSize(scratch, size + 1);
        fread(config, size, 1, fp);
        config[size] = 0;
        fclose(fp);

        SetDefaultCompilerConfig(&compiler_config);
        LoadCompilerConfig(&compiler_config, config, size);
    }
    else {
        SetDefaultCompilerConfig(&compiler_config);
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

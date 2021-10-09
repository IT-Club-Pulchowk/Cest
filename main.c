#include "os.h"
#include "zBase.h"
#include "zBaseCRT.h"
#include "stream.h"
#include "muda_parser.h"

void AssertHandle(const char *reason, const char *file, int line, const char *proc) {
	fprintf(stderr, "%s (%s:%d) - Procedure: %s\n", reason, file, line, proc);
	TriggerBreakpoint();
}

void DeprecateHandle(const char *file, int line, const char *proc) {
	fprintf(stderr, "Deprecated procedure \"%s\" used at \"%s\":%d\n", proc, file, line);
}

static String FormatStringV(Memory_Arena *arena, const char *fmt, va_list list) {
    va_list args;
    va_copy(args, list);
    int len = 1 + vsnprintf(NULL, 0, fmt, args);
    char *buf = (char *)PushSize(arena, len);
    vsnprintf(buf, len, fmt, args);
    va_end(args);
    return (String){ len - 1, (uint8_t *)buf};
}

static String FormatString(Memory_Arena *arena, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    String string = FormatStringV(arena, fmt, args);
    va_end(args);
    return string;
}

static Directory_Iteration DirectoryIteratorPrintNoBin(const File_Info *info, void *user_context) {
	if (info->Atribute & File_Attribute_Hidden) return Directory_Iteration_Continue;
	if (strcmp((char *)info->Name.Data, "bin") == 0) return Directory_Iteration_Continue;
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
	Memory_Arena *arena = ThreadScratchpadI(1);
    
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

        if (!strcmp((char *)prsr.Token.Data.Property.Key.Data, "Type")){
            if (!strcmp((char *)prsr.Token.Data.Property.Value.Data, "Project"))
                config->Type = Compile_Type_Project;
            else
                config->Type = Compile_Type_Solution;
        }

        else if (!strcmp((char *)prsr.Token.Data.Property.Key.Data, "Optimization"))
            config->Optimization = !strcmp((char *)prsr.Token.Data.Property.Value.Data, "true");

        else if (!strcmp((char *)prsr.Token.Data.Property.Key.Data, "BuildDirectory")){
            config->BuildDirectory.Length = prsr.Token.Data.Property.Value.Length;
            config->BuildDirectory.Data = prsr.Token.Data.Property.Value.Data;
        }

        else if (!strcmp((char *)prsr.Token.Data.Property.Key.Data, "Build")){
            config->Build.Length = prsr.Token.Data.Property.Value.Length;
            config->Build.Data = prsr.Token.Data.Property.Value.Data;
        }

        else if (!strcmp((char *)prsr.Token.Data.Property.Key.Data, "Define"))
            ReadList(&config->Defines, prsr.Token.Data.Property.Value);

        else if (!strcmp((char *)prsr.Token.Data.Property.Key.Data, "IncludeDirectory"))
            ReadList(&config->IncludeDirectory, prsr.Token.Data.Property.Value);

        else if (!strcmp((char *)prsr.Token.Data.Property.Key.Data, "Source"))
            ReadList(&config->Source, prsr.Token.Data.Property.Value);

        else if (!strcmp((char *)prsr.Token.Data.Property.Key.Data, "LibraryDirectory"))
            ReadList(&config->LibraryDirectory, prsr.Token.Data.Property.Value);

        else if (!strcmp((char *)prsr.Token.Data.Property.Key.Data, "Library"))
            ReadList(&config->Library, prsr.Token.Data.Property.Value);
    }
}

void Compile(Compiler_Config *config, Compiler_Kind compiler) {
	String cmdline = {0, 0};

	Memory_Arena *scratch = ThreadScratchpadI(1);

	Assert(config->Type == Compile_Type_Project);

	Temporary_Memory temp = BeginTemporaryMemory(scratch);

	Out_Stream out;
	OutCreate(&out, MemoryArenaAllocator(scratch));
        
    Uint32 result = CheckIfPathExists(config->BuildDirectory);
    if (result == Path_Does_Not_Exist) {
        CreateDirectoryRecursively(config->BuildDirectory);
    }
    else if (result == Path_Exist_File) {
        String error = FormatString(scratch, "%s: Path exist but is a file");
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

	Push_Allocator point = PushThreadAllocator(MemoryArenaAllocator(ThreadScratchpadI(0)));
	cmdline = OutBuildString(&out);
	PopThreadAllocator(&point);

	EndTemporaryMemory(&temp);

	LogInfo("Command Line: %s\n", cmdline.Data);

	LaunchCompilation(compiler, cmdline);
}


int main(int argc, char *argv[]) {
	InitThreadContextCrt(MegaBytes(512));

	Compiler_Kind compiler = DetectCompiler();

	Memory_Arena *scratch = ThreadScratchpad();

	// TODO: Error checking
	FILE *fp = fopen("sample.muda", "rb");
	fseek(fp, 0L, SEEK_END);
	int size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	Uint8 *config = PushSize(scratch, size + 1);
	fread(config, size, 1, fp);
	config[size] = 0;
	fclose(fp);
	
	Compiler_Config compiler_config;
	SetDefaultCompilerConfig(&compiler_config);
    LoadCompilerConfig(&compiler_config, config, size);

    if (compiler != Compiler_Kind_NULL)
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
#include "zBaseCRT.c"

#if PLATFORM_OS_WINDOWS == 1
#include "os_windows.c"
#endif
#if PLATFORM_OS_LINUX == 1
#include "os_linux.c"
#endif

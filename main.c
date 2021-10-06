
#include "os.h"
#include "zBaseCRT.h"
#include "stream.h"

#include <stdio.h>
#include <string.h>

void AssertHandle(const char *reason, const char *file, int line, const char *proc) {
	fprintf(stderr, "%s (%s:%d) - Procedure: %s\n", reason, file, line, proc);
	TriggerBreakpoint();
}

void DeprecateHandle(const char *file, int line, const char *proc) {
	fprintf(stderr, "Deprecated procedure \"%s\" used at \"%s\":%d\n", proc, file, line);
}

Directory_Iteration DirectoryIteratorPrintNoBin(const File_Info *info, void *user_context) {
	if (info->Atribute & File_Attribute_Hidden) return Directory_Iteration_Continue;
	if (strcmp((char *)info->Name.Data, "bin") == 0) return Directory_Iteration_Continue;
	LogInfo("%s - %zu bytes\n", info->Path.Data, info->Size);
	return Directory_Iteration_Recurse;
}

typedef enum Compile_Type {
	Compile_Type_Project,
	Compile_Type_Solution,
} Compile_Type;

typedef struct Compiler_Config {
	Compile_Type Type;

	bool Optimization;

	String *Defines;
	Uint32 DefineCount;

	String *IncludeDirectory;
	Uint32 IncludeDirectoryCount;

	String *Source;
	Uint32 SourceCount;

	String BuildDirectory;
	String Build;

	String *LiraryDirectory;
	Uint32 LibraryDirectoryCount;

	String *Library;
	Uint32 LibraryCount;
} Compiler_Config;

void SetDefaultCompilerConfig(Compiler_Config *config) {
	Memory_Arena *arena = ThreadScratchpadI(1);

	// TODO: Use sensible default values

	config->Type = Compile_Type_Project;

	config->Optimization = false;

	config->DefineCount = 3;
	config->Defines = PushArrayAligned(arena, String, config->DefineCount, sizeof(Ptrsize));

	config->Defines[0] = StringLiteral("ASSERTION_HANDLED");
	config->Defines[1] = StringLiteral("DEPRECATION_HANDLED");
	config->Defines[2] = StringLiteral("_CRT_SECURE_NO_WARNINGS");

	config->IncludeDirectory = NULL;
	config->IncludeDirectoryCount = 0;

	config->SourceCount = 1;
	config->Source = PushArrayAligned(arena, String, config->DefineCount, sizeof(Ptrsize));

	config->Source[0] = StringLiteral("main.c");

	config->BuildDirectory = StringLiteral("./bin");
	config->Build = StringLiteral("bootstrap");

	config->LiraryDirectory = NULL;
	config->LibraryDirectoryCount = 0;

	config->Library = NULL;
	config->LibraryCount = 0;
}

void Compile(Compiler_Config *config, Compiler_Kind compiler) {
	String cmdline = {0, 0};

	Memory_Arena *scratch = ThreadScratchpadI(1);

	Assert(config->Type == Compile_Type_Project);
	Assert(compiler != Compiler_Kind_CLANG);

	Temporary_Memory temp = BeginTemporaryMemory(scratch);

	Out_Stream out;
	OutCreate(&out, MemoryArenaAllocator(scratch));

	// TODO: Check if the compiler is CL or CLANG

	// Defaults
    if (compiler == Compiler_Kind_CL){
        OutFormatted(&out, "cl -nologo -Zi -EHsc ");

        if (config->Optimization)
            OutFormatted(&out, "-O2 ");
        else
            OutFormatted(&out, "-Od ");

        for (Uint32 i = 0; i < config->DefineCount; ++i)
            OutFormatted(&out, "-D%s ", config->Defines[i].Data);

        for (Uint32 i = 0; i < config->IncludeDirectoryCount; ++i)
            OutFormatted(&out, "-I%s ", config->IncludeDirectory[i].Data);

        // TODO: Add Recursively if it is has "*"
        for (Uint32 i = 0; i < config->SourceCount; ++i)
            OutFormatted(&out, "\"%s\" ", config->Source[i].Data);

        // TODO: Make directory if not present, need to add OS api for making directory!
        // Until then make "bin/int" directory manually :(
        OutFormatted(&out, "-Fo\"%s/int/\" ", config->BuildDirectory.Data);
        OutFormatted(&out, "-Fd\"%s/\" ", config->BuildDirectory.Data);
        OutFormatted(&out, "-link ");
        OutFormatted(&out, "-out:\"%s/%s.exe\" ", config->BuildDirectory.Data, config->Build.Data);
        OutFormatted(&out, "-pdb:\"%s/%s.pdb\" ", config->BuildDirectory.Data, config->Build.Data);

        for (Uint32 i = 0; i < config->LibraryDirectoryCount; ++i)
            OutFormatted(&out, "-LIBPATH:\"%s\" ", config->LiraryDirectory[i].Data);

        for (Uint32 i = 0; i < config->LibraryCount; ++i)
            OutFormatted(&out, "\"%s\" ", config->Library[i].Data);
    }
    else if (compiler == Compiler_Kind_GCC){
        // TODO: Same as with cl
        OutFormatted(&out, "gcc -pipe ");

        if (config->Optimization)
            OutFormatted(&out, "-O2 ");
        else
            OutFormatted(&out, "-g ");

        for (Uint32 i = 0; i < config->DefineCount; ++i)
            OutFormatted(&out, "-D%s ", config->Defines[i].Data);

        for (Uint32 i = 0; i < config->IncludeDirectoryCount; ++i)
            OutFormatted(&out, "-I%s ", config->IncludeDirectory[i].Data);

        for (Uint32 i = 0; i < config->SourceCount; ++i)
            OutFormatted(&out, "%s ", config->Source[i].Data);

        OutFormatted(&out, "-o%s/%s.out ", config->BuildDirectory.Data, config->Build.Data);

        for (Uint32 i = 0; i < config->LibraryDirectoryCount; ++i)
            OutFormatted(&out, "-L%s ", config->LiraryDirectory[i].Data);

        for (Uint32 i = 0; i < config->LibraryCount; ++i)
            OutFormatted(&out, "-l%s ", config->Library[i].Data);
    }

	Push_Allocator point = PushThreadAllocator(MemoryArenaAllocator(ThreadScratchpadI(0)));
	cmdline = OutBuildString(&out);
	PopThreadAllocator(&point);

	EndTemporaryMemory(&temp);


	LogInfo("Command Line: %s\n", cmdline.Data);

	OsLaunchCompilation(compiler, cmdline);
}


int main(int argc, char *argv[]) {
	InitThreadContextCrt(MegaBytes(512));

	Compiler_Kind compiler = DetectCompiler();

	Memory_Arena *scratch = ThreadScratchpad();

	// TODO: Error checking
	FILE *fp = fopen("sample.cest", "rb");
	fseek(fp, 0L, SEEK_END);
	int size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	Uint8 *config = PushSize(scratch, size + 1);
	fread(config, size, 1, fp);
	config[size] = 0;
	fclose(fp);
	
	Compiler_Config compiler_config;
	SetDefaultCompilerConfig(&compiler_config);

#if PLATFORM_OS_WINDOWS == 1
	Compile(&compiler_config, compiler);
#endif
#if PLATFORM_OS_LINUX == 1
	Compile(&compiler_config, compiler);
#endif

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

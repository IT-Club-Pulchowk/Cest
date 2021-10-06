
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
	static String source;
	if (!source.Data) source = StringLiteral("*.c");

	config->Type = Compile_Type_Project;

	config->Optimization = false;

	config->Defines = NULL;
	config->DefineCount = 0;

	config->IncludeDirectory = NULL;
	config->IncludeDirectoryCount = 0;

	config->Source = (String *)&source;
	config->SourceCount = 1;

	config->BuildDirectory = StringLiteral("./bin");
	config->Build = StringLiteral("main");

	config->LiraryDirectory = NULL;
	config->LibraryDirectoryCount = 0;

	config->Library = NULL;
	config->LibraryCount = 0;
}

void Compile(Compiler_Config *config) {
	Memory_Arena *scratch = ThreadScratchpadI(1);

	Assert(config->Type == Compile_Type_Project);

	Temporary_Memory temp = BeginTemporaryMemory(scratch);

	Out_Stream out;
	OutCreate(&out, MemoryArenaAllocator(scratch));

	// TODO: Check if the compiler is CL or CLANG

	// Defaults
	OutFormatted(&out, "-nologo -Zi -EHsc ");

	if (config->Optimization) {
		OutFormatted(&out, "-O2 ");
	}
	else {
		OutFormatted(&out, "-Od ");
	}

	for (Uint32 i = 0; i < config->DefineCount; ++i) {
		OutFormatted(&out, "-D%s ", config->Defines[i].Data);
	}

	for (Uint32 i = 0; i < config->IncludeDirectoryCount; ++i) {
		OutFormatted(&out, "-I%s ", config->IncludeDirectory[i].Data);
	}

	// TODO: Add Recursively if it is has "*"
	for (Uint32 i = 0; i < config->SourceCount; ++i) {
		OutFormatted(&out, "\"%s\" ", config->Source[i].Data);
	}

	// For CL, we need to change the working directory to BuildDirectory, so here we just set Build
	OutFormatted(&out, "-Fe\"%s.exe\" ", config->Build.Data);

	if (config->LibraryDirectoryCount) {
		OutFormatted(&out, "-link ");
		for (Uint32 i = 0; i < config->LibraryDirectoryCount; ++i) {
			OutFormatted(&out, "-LIBPATH:\"%s\" ", config->LiraryDirectory[i].Data);
		}
	}

	for (Uint32 i = 0; i < config->LibraryCount; ++i) {
		OutFormatted(&out, "\"%s\" ", config->Library[i].Data);
	}

	Push_Allocator point = PushThreadAllocator(MemoryArenaAllocator(ThreadScratchpadI(0)));
	String cmdline = OutBuildString(&out);
	PopThreadAllocator(&point);

	EndTemporaryMemory(&temp);


	LogInfo("Command Line: %s\n", cmdline.Data);
}


int main(int argc, char *argv[]) {
	InitThreadContextCrt(MegaBytes(512));

	DetectCompiler();

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

	//LogInfo("Config:\n%s\n", config);
	
	Compiler_Config compiler_config;
	SetDefaultCompilerConfig(&compiler_config);
	Compile(&compiler_config);

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

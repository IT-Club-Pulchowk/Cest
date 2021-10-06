
#include "os.h"
#include "zBaseCRT.h"

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
	String BuildDirectory;
	String Build;
	String *Defines;
	Uint32 DefineCount;
	String *Source;
	Uint32 SourceCount;
	String *IncludeDirectory;
	Uint32 IncludeDirectoryCount;
	String *LiraryDirectory;
	Uint32 LibraryDirectoryCount;
	String *Library;
	Uint32 LibraryCount;
	bool Optimization;
} Compiler_Config;

void SetDefaultCompilerConfig(Compiler_Config *config) {
	static String source;
	if (!source.Data) source = StringLiteral("*.c");

	config->Type = Compile_Type_Project;
	config->BuildDirectory = StringLiteral("./bin");
	config->Build = StringLiteral("main");
	config->Defines = NULL;
	config->DefineCount = 0;
	config->Source = (String *)&source;
	config->SourceCount = 1;
	config->IncludeDirectory = NULL;
	config->IncludeDirectoryCount = 0;
	config->LiraryDirectory = NULL;
	config->LibraryDirectoryCount = 0;
	config->Library = NULL;
	config->LibraryCount = 0;
	config->Optimization = false;
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

	LogInfo("Config:\n%s\n", config);

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

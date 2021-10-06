
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

int main(int argc, char *argv[]) {
	InitThreadContextCrt(MegaBytes(512));

	if (argc != 2) {
		LogError("\nUsage: %s <directory name>\n", argv[0]);
		return 1;
	}

	DetectCompiler();

	const char *dir = argv[1];

	IterateDirectroy(dir, DirectoryIteratorPrintNoBin, NULL);

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

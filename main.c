
#include "os.h"
#include "zBaseCRT.h"

#include <stdio.h>
#include <string.h>

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

	const char *dir = argv[1];

	IterateDirectroy(dir, DirectoryIteratorPrintNoBin, NULL);

	return 0;
}

#include "zBase.c"
#include "zBaseCRT.c"

#if OS_WINDOWS == 1
#include "os_windows.c"
#endif
#if OS_LINUX == 1
#include "os_linux.c"
#endif

#include "lenstring.h"
#include "os.h"

typedef enum Compile_Type {
	Compile_Type_Project,
	Compile_Type_Solution,
} Compile_Type;

typedef struct Build_Config {
    Compiler_Kind ForceCompiler;
    bool ForceOptimization;
    bool DisplayCommandLine;
    bool DisableLogs;
} Build_Config;

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

    Build_Config BuildConfig;
} Compiler_Config;

//
//
//

INLINE_PROCEDURE void CompilerConfigInit(Compiler_Config *config) {
    memset(config, 0, sizeof(*config));
    StringListInit(&config->Defines);
    StringListInit(&config->IncludeDirectory);
    StringListInit(&config->Source);
    StringListInit(&config->LibraryDirectory);
    StringListInit(&config->Library);

    config->BuildConfig.ForceCompiler = 0;
    config->BuildConfig.ForceOptimization = false;
    config->BuildConfig.DisplayCommandLine = false;
    config->BuildConfig.DisableLogs = false;
}

INLINE_PROCEDURE void PushDefaultCompilerConfig(Compiler_Config *config, Compiler_Kind compiler) {
    Memory_Arena *scratch = ThreadScratchpad();

    if (config->BuildDirectory.Length == 0) {
        config->BuildDirectory = StrDuplicateArena(StringLiteral("./bin"), scratch);
    }

    if (config->Build.Length == 0) {
        config->Build = StrDuplicateArena(StringLiteral("output"), scratch);
    }

    if (StringListIsEmpty(&config->Source)) {
        StringListAdd(&config->Source, StrDuplicateArena(StringLiteral("*.c"), scratch));
    }

    if ((compiler & Compiler_Bit_CL) && StringListIsEmpty(&config->Defines)) {
        StringListAdd(&config->Defines, StrDuplicateArena(StringLiteral("_CRT_SECURE_NO_WARNINGS"), scratch));
    }
}

INLINE_PROCEDURE void PrintCompilerConfig(Compiler_Config conf) {
    OsConsoleWrite("\nType                : %s", conf.Type == Compile_Type_Project ? "Project" : "Solution");
    OsConsoleWrite("\nOptimization        : %s", conf.Optimization ? "True" : "False");
    OsConsoleWrite("\nBuild               : %s", conf.Build.Data);
    OsConsoleWrite("\nBuild Directory     : %s", conf.BuildDirectory.Data);

    OsConsoleWrite("\nSource              : ");
    for (String_List_Node *ntr = &conf.Source.Head; ntr && conf.Source.Used; ntr = ntr->Next) {
        int len = ntr->Next ? 8 : conf.Source.Used;
        for (int i = 0; i < len; i++) OsConsoleWrite("%s ", ntr->Data[i].Data);
    }
    OsConsoleWrite("\nDefines             : ");
    for (String_List_Node *ntr = &conf.Defines.Head; ntr && conf.Defines.Used; ntr = ntr->Next) {
        int len = ntr->Next ? 8 : conf.Defines.Used;
        for (int i = 0; i < len; i++) OsConsoleWrite("%s ", ntr->Data[i].Data);
    }
    OsConsoleWrite("\nInclude Directories : ");
    for (String_List_Node *ntr = &conf.IncludeDirectory.Head; ntr && conf.IncludeDirectory.Used; ntr = ntr->Next) {
        int len = ntr->Next ? 8 : conf.IncludeDirectory.Used;
        for (int i = 0; i < len; i++) OsConsoleWrite("%s ", ntr->Data[i].Data);
    }
    OsConsoleWrite("\nLibrary Directories : ");
    for (String_List_Node *ntr = &conf.LibraryDirectory.Head; ntr && conf.LibraryDirectory.Used; ntr = ntr->Next) {
        int len = ntr->Next ? 8 : conf.LibraryDirectory.Used;
        for (int i = 0; i < len; i++) OsConsoleWrite("%s ", ntr->Data[i].Data);
    }
    OsConsoleWrite("\nLibraries           : ");
    for (String_List_Node *ntr = &conf.Library.Head; ntr && conf.Library.Used; ntr = ntr->Next) {
        int len = ntr->Next ? 8 : conf.Library.Used;
        for (int i = 0; i < len; i++) OsConsoleWrite("%s ", ntr->Data[i].Data);
    }
    OsConsoleWrite("\n");
}

#pragma once
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
    String_List Source;
	struct {
        String_List* Properties;
        Uint32 Cap, Count;
    } Optionals;
    Build_Config BuildConfig;
} Compiler_Config;

typedef struct {
    String Name;
    const char* Desc;
    const char* Fmt_MSVC;
    const char* Fmt_GCC;
    const char* Fmt_CLANG;
    Int32 Max_Vals;
} Compiler_Optionals;

const Compiler_Optionals Available_Optionals[] = {
    {StringLiteralExpand("Defines"), "List of preprocessing symbols to be defined", "-D%s ", "-D%s ", "-D%s ", -1},
    {StringLiteralExpand("Library"), "List of additional libraries to be linked [For MSVC: do not include the .lib extension]", "\"%s.lib\" " , "-l%s ", "-l%s ", -1},
    {StringLiteralExpand("IncludeDirectory"), "List of directories to search for include files", "-I\"%s\" ", "-I%s ", "-I%s ", -1},
    {StringLiteralExpand("LibraryDirectory"), "List of paths to be searched for additional libraries", "-LIBPATH:\"%s\" ", "-L%s ", "-L%s ", -1},

#if PLATFORM_OS_WINDOWS
    {StringLiteralExpand("Subsystem"), "Specify the environment for the executable", "-subsystem:%s ", "-subsystem,%s ", "-subsystem:%s ",  1},
#endif
};

//
//
//

#define INITIAL_OPT_CAP 5
INLINE_PROCEDURE void CompilerConfigInit(Compiler_Config *config) {
    memset(config, 0, sizeof(*config));
    StringListInit(&config->Source);
    Memory_Arena *scratch = ThreadScratchpad();
    config->Optionals.Properties = (String_List *)PushSize(scratch, sizeof(String_List) * INITIAL_OPT_CAP);
    config->Optionals.Cap = INITIAL_OPT_CAP;
    config->Optionals.Count = 0;

    config->BuildConfig.ForceCompiler      = 0;
    config->BuildConfig.ForceOptimization  = false;
    config->BuildConfig.DisplayCommandLine = false;
    config->BuildConfig.DisableLogs        = false;
}

//TODO: reimplement the commented out code

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
/*  */
/*     if ((compiler & Compiler_Bit_CL) && StringListIsEmpty(&config->Defines)) { */
/*         StringListAdd(&config->Defines, StrDuplicateArena(StringLiteral("_CRT_SECURE_NO_WARNINGS"), scratch)); */
/*     } */
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
    /* OsConsoleWrite("\nDefines             : "); */
    /* for (String_List_Node *ntr = &conf.Defines.Head; ntr && conf.Defines.Used; ntr = ntr->Next) { */
    /*     int len = ntr->Next ? 8 : conf.Defines.Used; */
    /*     for (int i = 0; i < len; i++) OsConsoleWrite("%s ", ntr->Data[i].Data); */
    /* } */
    /* OsConsoleWrite("\nInclude Directories : "); */
    /* for (String_List_Node *ntr = &conf.IncludeDirectory.Head; ntr && conf.IncludeDirectory.Used; ntr = ntr->Next) { */
    /*     int len = ntr->Next ? 8 : conf.IncludeDirectory.Used; */
    /*     for (int i = 0; i < len; i++) OsConsoleWrite("%s ", ntr->Data[i].Data); */
    /* } */
    /* OsConsoleWrite("\nLibrary Directories : "); */
    /* for (String_List_Node *ntr = &conf.LibraryDirectory.Head; ntr && conf.LibraryDirectory.Used; ntr = ntr->Next) { */
    /*     int len = ntr->Next ? 8 : conf.LibraryDirectory.Used; */
    /*     for (int i = 0; i < len; i++) OsConsoleWrite("%s ", ntr->Data[i].Data); */
    /* } */
    /* OsConsoleWrite("\nLibraries           : "); */
    /* for (String_List_Node *ntr = &conf.Library.Head; ntr && conf.Library.Used; ntr = ntr->Next) { */
    /*     int len = ntr->Next ? 8 : conf.Library.Used; */
    /*     for (int i = 0; i < len; i++) OsConsoleWrite("%s ", ntr->Data[i].Data); */
    /* } */
    OsConsoleWrite("\n");
}

#pragma once
#include "lenstring.h"
#include "os.h"
#include <ctype.h>

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

#define MAX_OPTIONALS_IN_NODE 8

typedef struct Opt_Lst_Node{
    String Property_Keys[MAX_OPTIONALS_IN_NODE];
    String_List Property_Values[MAX_OPTIONALS_IN_NODE];
    struct Opt_Lst_Node *Next;
} Optionals_List_Node;

typedef struct {
	Optionals_List_Node Head;
	Optionals_List_Node *Tail;
	Uint32 Used;
} Optionals_List;

typedef struct Compiler_Config {
	Compile_Type Type;
	bool Optimization;
	String Build;
	String BuildDirectory;
    String_List Source;
    Optionals_List Optionals;
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
    {StringLiteralExpand("Define"), "List of preprocessing symbols to be defined", "-D%s ", "-D%s ", "-D%s ", -1},
    {StringLiteralExpand("Library"), "List of additional libraries to be linked [For MSVC: do not include the .lib extension]", "\"%s.lib\" " , "-l%s ", "-l%s ", -1},
    {StringLiteralExpand("IncludeDirectory"), "List of directories to search for include files", "-I\"%s\" ", "-I%s ", "-I%s ", -1},
    {StringLiteralExpand("LibraryDirectory"), "List of paths to be searched for additional libraries", "-LIBPATH:\"%s\" ", "-L%s ", "-L%s ", -1},

#if PLATFORM_OS_WINDOWS
    {StringLiteralExpand("Subsystem"), "Specify the environment for the executable", "-subsystem:%s ", "-subsystem,%s ", "-subsystem:%s ",  1},
#endif
};

//
// Base setup
//

INLINE_PROCEDURE void AssertHandle(const char *reason, const char *file, int line, const char *proc) {
    OsConsoleOut(OsGetStdOutputHandle(), "%s (%s:%d) - Procedure: %s\n", reason, file, line, proc);
    TriggerBreakpoint();
}

INLINE_PROCEDURE void DeprecateHandle(const char *reason, const char *file, int line, const char *proc) {
    OsConsoleOut(OsGetStdOutputHandle(), "Deprecated procedure \"%s\" used at \"%s\":%d. %s\n", proc, file, line, reason);
}

INLINE_PROCEDURE void LogProcedure(void *agent, Log_Kind kind, const char *fmt, va_list list) {
    void *fp = (kind == Log_Kind_Info) ? OsGetStdOutputHandle() : OsGetErrorOutputHandle();
    if (kind == Log_Kind_Info)
        OsConsoleOut(fp, "%-10s", "[Log] ");
    else if (kind == Log_Kind_Error)
        OsConsoleOut(fp, "%-10s", "[Error] ");
    else if (kind == Log_Kind_Warn)
        OsConsoleOut(fp, "%-10s", "[Warning] ");
    OsConsoleOutV(fp, fmt, list);
}

INLINE_PROCEDURE void LogProcedureDisabled(void *agent, Log_Kind kind, const char *fmt, va_list list) {
    if (kind == Log_Kind_Info) return;
    OsConsoleOutV(OsGetErrorOutputHandle(), fmt, list);
}

INLINE_PROCEDURE void FatalErrorProcedure(const char *message) {
    OsConsoleWrite("%-10s", "[Fatal Error] ");
    OsConsoleWrite("%s", message);
    OsProcessExit(0);
}

//
//
//

#define INITIAL_OPT_CAP 5
INLINE_PROCEDURE void CompilerConfigInit(Compiler_Config *config) {
    memset(config, 0, sizeof(*config));
    StringListInit(&config->Source);

    config->BuildConfig.ForceCompiler      = 0;
    config->BuildConfig.ForceOptimization  = false;
    config->BuildConfig.DisplayCommandLine = false;
    config->BuildConfig.DisableLogs        = false;

	config->Optionals.Used = 0;
	config->Optionals.Head.Next = NULL;
	config->Optionals.Tail = &config->Optionals.Head;

    for (int i = 0; i < MAX_OPTIONALS_IN_NODE; i++) {
        config->Optionals.Head.Property_Keys[i] = (String){0, 0};
        StringListInit(&config->Optionals.Head.Property_Values[i]);
    }
}

INLINE_PROCEDURE int CheckIfOptAvailable (String option) {
    for (int i = 0; i < ArrayCount(Available_Optionals); i ++)
        if (StrMatch(option, Available_Optionals[i].Name))
            return i;
    return -1;
}

INLINE_PROCEDURE void ReadList(String_List *dst, String data, Int64 max){
    Int64 prev_pos = 0;
    Int64 curr_pos = 0;
    Int64 count = 0;

    while (curr_pos < data.Length && (max < 0 || count < max)) {
        // Remove prefixed spaces (postfix spaces in the case of 2+ iterations)
        while (curr_pos < data.Length && isspace(data.Data[curr_pos])) 
            curr_pos += 1;
        prev_pos = curr_pos;

        // Count number of characters in the string
        while (curr_pos < data.Length && !isspace(data.Data[curr_pos])) 
            curr_pos += 1;

        StringListAdd(dst, StrDuplicate(StringMake(data.Data + prev_pos, curr_pos - prev_pos)));
        count ++;
    }
}

INLINE_PROCEDURE void OptListAdd(Optionals_List *dst, String key, String data, int optnum) {
	if (dst->Used == MAX_OPTIONALS_IN_NODE) {
		dst->Used = 0;
		dst->Tail->Next = (Optionals_List_Node *)MemoryAllocate(sizeof(Optionals_List_Node), &ThreadContext.Allocator);
		dst->Tail = dst->Tail->Next;
        for (int i = 0; i < MAX_OPTIONALS_IN_NODE; i++) {
            dst->Tail[i].Property_Keys[i] = (String){0, 0};
            StringListInit(&dst->Tail[i].Property_Values[i]);
        }
		dst->Tail->Next = NULL;
	}
	dst->Tail->Property_Keys[dst->Used] = key;
    ReadList(&dst->Tail->Property_Values[dst->Used], data, Available_Optionals[optnum].Max_Vals);
	dst->Used++;
}

//TODO: reimplement the commented out code

INLINE_PROCEDURE void PushDefaultCompilerConfig(Compiler_Config *config, Compiler_Kind compiler) {
    Memory_Arena *scratch = ThreadScratchpad();

    if (config->BuildDirectory.Length == 0) {
        config->BuildDirectory = StrDuplicateArena(StringLiteral("./bin"), scratch);
        LogInfo("Using Default Binary Directory: \"%s\"\n", config->BuildDirectory.Data);
    }

    if (config->Build.Length == 0) {
        config->Build = StrDuplicateArena(StringLiteral("output"), scratch);
        LogInfo("Using Default Binary: %s\n", config->Build.Data);
    }

    if (StringListIsEmpty(&config->Source)) {
        StringListAdd(&config->Source, StrDuplicateArena(StringLiteral("*.c"), scratch));
        LogInfo("Using Default Sources: %s\n", config->Source.Head.Data[0].Data);
    }
/*  */
/*     if ((compiler & Compiler_Bit_CL) && StringListIsEmpty(&config->Defines)) { */
/*         StringListAdd(&config->Defines, StrDuplicateArena(StringLiteral("_CRT_SECURE_NO_WARNINGS"), scratch)); */
/*     } */
}

typedef void (*Stream_Writer_Proc)(void *context, const char *fmt, ...);

INLINE_PROCEDURE void WriteCompilerConfig(Compiler_Config *conf, bool comments, Stream_Writer_Proc writer, void *context) {
    UnusedVariable(comments);

    const char *fmt = "\n%-15s : %s";
    const char *fmt_no_val = "\n%-15s : ";

    writer(context, fmt, "Type", conf->Type == Compile_Type_Project ? "Project" : "Solution");
    writer(context, fmt, "Optimization", conf->Optimization ? "True" : "False");
    writer(context, fmt, "Build", conf->Build.Data);
    writer(context, fmt, "BuildDirectory", conf->BuildDirectory.Data);

    writer(context, fmt_no_val, "Source");

    ForList(String_List_Node, &conf->Source) {
        ForListNode(&conf->Source, MAX_STRING_NODE_DATA_COUNT) {
            writer(context, "\"%s\" ", it->Data[index].Data);
        }
    }

    ForList(Optionals_List_Node, &conf->Optionals) {
        ForListNode(&conf->Optionals, MAX_OPTIONALS_IN_NODE) {
            writer(context, fmt_no_val, it->Property_Keys[index].Data);
            String_List *value_list = &it->Property_Values[index];
            ForList(String_List_Node, value_list) {
                ForListNode(value_list, MAX_STRING_NODE_DATA_COUNT) {
                    writer(context, "\"%s\" ", it->Data[index].Data);
                }
            }
        }
    }
}

INLINE_PROCEDURE void PrintCompilerConfig(Compiler_Config conf) {
    Deprecated("Use WriteCompilerConfig function instead");
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

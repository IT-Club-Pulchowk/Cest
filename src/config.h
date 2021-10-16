#pragma once
#include "version.h"
#include "lenstring.h"
#include "os.h"
#include "stream.h"
#include "version.h"
#include <time.h>

typedef enum Compile_Kind {
	Compile_Project,
	Compile_Solution,
} Compile_Kind;

static const String CompilerKindId[] = {
	StringExpand("Project"), StringExpand("Solution")
};

typedef enum Application_Kind {
	Application_Executable,
	Application_Static_Library,
	Application_Dynamic_Library
} Application_Kind;

static const String ApplicationKindId[] = {
	StringExpand("Executable"), StringExpand("StaticLibrary"), StringExpand("DynamicLibrary")
};

typedef enum Subsystem_Kind {
	Subsystem_Console,
	Subsystem_Windows,
} Subsystem_Kind;

static const String SubsystemKindId[] = {
	StringExpand("Console"), StringExpand("Windows")
};

typedef struct Build_Config {
	Compiler_Kind ForceCompiler;
	bool ForceOptimization;
	bool DisplayCommandLine;
	bool DisableLogs;
	String Configuration;
} Build_Config;

typedef struct Compiler_Config {
	Uint32 Kind; // Compile_Kind 
	Uint32 Application; // Application_Kind 

	bool Optimization;

	Out_Stream  Build;
	Out_Stream  BuildDirectory;
	String_List Sources;
	String_List Flags;
	String_List Defines;
	String_List IncludeDirectories;

	Uint32		Subsystem; // Subsystem_Kind 
	String_List Libraries;
	String_List LibraryDirectories;
	String_List LinkerFlags;

	Build_Config BuildConfig;

	Memory_Arena *Arena;
} Compiler_Config;

typedef enum Compiler_Config_Member_Kind {
	Compiler_Config_Member_Enum,
	Compiler_Config_Member_Bool,
	Compiler_Config_Member_String,
	Compiler_Config_Member_String_Array,
} Compiler_Config_Member_Kind;

typedef struct Enum_Info {
	const String *Ids;
	Uint32 Count;
} Enum_Info;

typedef struct Compiler_Config_Member {
	String Name;
	Compiler_Config_Member_Kind Kind;
	size_t Offset;
	const char *Desc;
	const void *KindInfo;
} Compiler_Config_Member;

static const Enum_Info CompilerKindInfo = { CompilerKindId, ArrayCount(CompilerKindId) };
static const Enum_Info ApplicationKindInfo = { ApplicationKindId, ArrayCount(ApplicationKindId) };
static const Enum_Info SubsystemKindInfo = { SubsystemKindId, ArrayCount(SubsystemKindId) };

static const Compiler_Config_Member CompilerConfigMemberTypeInfo[] = {
	{ StringExpand("Kind"), Compiler_Config_Member_Enum, offsetof(Compiler_Config, Kind),
	"Type of compilation (Project or Solution). Default is Project. "
	"Project only build using a single muda file. "
	"Solution iterates through subdirectories and build each muda file found.",
	&CompilerKindInfo },

	{ StringExpand("Application"), Compiler_Config_Member_Enum, offsetof(Compiler_Config, Application), 
	"Application can be Executable, StaticLibrary or DynamicLibrary. Default is Executable.", &ApplicationKindInfo },

	{ StringExpand("Optimization"), Compiler_Config_Member_Bool, offsetof(Compiler_Config, Optimization), 
	"Use optimization while compiling or not (true/false). Default is false." },

	{ StringExpand("Build"), Compiler_Config_Member_String, offsetof(Compiler_Config, Build), 
	"Name of the output binary." },

	{ StringExpand("BuildDirectory"), Compiler_Config_Member_String, offsetof(Compiler_Config, BuildDirectory), 
	"Path to the directory where the output binary should be placed." },

	{ StringExpand("Sources"), Compiler_Config_Member_String_Array, offsetof(Compiler_Config, Sources), 
	"Path to the source files."},

	{ StringExpand("Flags"), Compiler_Config_Member_String_Array, offsetof(Compiler_Config, Flags), 
	"Flags for the compiler. Different compiler may use different flags, so it is recommended to use sections for using this property." },

	{ StringExpand("Defines"), Compiler_Config_Member_String_Array, offsetof(Compiler_Config, Defines), 
	"List of preprocessing symbols to be defined." },

	{ StringExpand("IncludeDirectories"), Compiler_Config_Member_String_Array, offsetof(Compiler_Config, IncludeDirectories), 
	"List of directories to search for include files." },

	{ StringExpand("Subsystem"), Compiler_Config_Member_Enum, offsetof(Compiler_Config, Subsystem), 
	"This property is only used in Windows. can be Console or Windows. Windows uses the win main entry point.", &SubsystemKindInfo },

	{ StringExpand("Libraries"), Compiler_Config_Member_String_Array, offsetof(Compiler_Config, Libraries), 
	"List of additional libraries to be linked (For MSVC: do not include the .lib extension)." },

	{ StringExpand("LibraryDirectories"), Compiler_Config_Member_String_Array, offsetof(Compiler_Config, LibraryDirectories), 
	"List of paths to be searched for additional libraries." },

	{ StringExpand("LinkerFlags"), Compiler_Config_Member_String_Array, offsetof(Compiler_Config, LinkerFlags), 
	"Flags for the linker. Different linkers may use different flags, so it is recommended to use sections for using this property." },
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

INLINE_PROCEDURE void CompilerConfigInit(Compiler_Config *config, Memory_Arena *arena) {
	Memory_Allocator allocator = MemoryArenaAllocator(arena);

	config->Kind = Compile_Project;
	config->Application = Application_Executable;

	config->Optimization = false;

	OutCreate(&config->Build, allocator);
	OutCreate(&config->BuildDirectory, allocator);
	StringListInit(&config->Sources);
	StringListInit(&config->Flags);
	StringListInit(&config->Defines);
	StringListInit(&config->IncludeDirectories);

	config->Subsystem = Subsystem_Console;
	StringListInit(&config->Libraries);
	StringListInit(&config->LibraryDirectories);
	StringListInit(&config->LinkerFlags);

	config->Arena = arena;

	config->BuildConfig.ForceCompiler = 0;
	config->BuildConfig.ForceOptimization = false;
	config->BuildConfig.DisplayCommandLine = false;
	config->BuildConfig.DisableLogs = false;
}

INLINE_PROCEDURE void ReadList(String_List *dst, String data, Int64 max, Memory_Arena *arena) {
	data = StrTrim(data); // Trimming the end white spaces so that we don't get empty strings at the end

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

		StringListAdd(dst, StrDuplicateArena(StringMake(data.Data + prev_pos, curr_pos - prev_pos), arena));
		count++;
	}
}

INLINE_PROCEDURE void PushDefaultCompilerConfig(Compiler_Config *config, Compiler_Kind compiler, bool write_log) {
	Memory_Arena *scratch = ThreadScratchpad();

	if (config->Build.Size == 0) {
		String def_build = StringLiteral("output");
		OutString(&config->Build, def_build);
		if (write_log)
			LogInfo("Using Default Binary: %s\n", def_build.Data);
	}

	if (config->BuildDirectory.Size == 0) {
		String def_build_dir = StringLiteral("./bin");
		OutString(&config->BuildDirectory, def_build_dir);
		if (write_log)
			LogInfo("Using Default Binary Directory: \"%s\"\n", def_build_dir.Data);
	}

	if (StringListIsEmpty(&config->Sources)) {
		String def_source = StringLiteral("*.c");
		StringListAdd(&config->Sources, StrDuplicateArena(def_source, config->Arena));
		if (write_log)
			LogInfo("Using Default Sources: %s\n", def_source.Data);
	}
}

typedef void (*Stream_Writer_Proc)(void *context, const char *fmt, ...);

/* INLINE_PROCEDURE void WriteCompilerConfig(Compiler_Config *conf, bool comments, Stream_Writer_Proc writer, void *context) { */
/*     const char *fmt = "\n%-15s : %s;\n"; */
/*     const char *fmt_no_val = "\n%-15s : "; */
/*  */
/*     if (comments) { */
/*         time_t t = time(NULL); */
/*         struct tm tm = *localtime(&t); */
/*         writer(context, "# Generated by: Muda Console Setup [%d-%02d-%02d %02d:%02d:%02d]\n\n",  */
/*             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec); */
/*     } */
/*  */
/*     writer(context, "@version %u.%u.%u\n", MUDA_VERSION_MAJOR, MUDA_VERSION_MINOR, MUDA_VERSION_PATCH); */
/*  */
/*     if (comments) */
/*         writer(context, "\n# Change this to 'Solution' if you want to iterate every directory and build the project in subdirectories"); */
/*     writer(context, fmt, "Type", conf->Type == Compile_Type_Project ? "Project" : "Solution"); */
/*  */
/*     if (comments) */
/*         writer(context, "\n# Optimization flag can be changed using command line with \"-optimize\" flag which forces optimization to be turned on"); */
/*     writer(context, fmt, "Optimization", conf->Optimization ? "True" : "False"); */
/*  */
/*     if (comments) */
/*         writer(context, "\n# Name of the executable / library to be generated, don't put extensions here"); */
/*     writer(context, fmt, "Build", conf->Build.Data); */
/*  */
/*     if (comments) */
/*         writer(context, "\n# Directory where the executable / library is generated"); */
/*     writer(context, fmt, "BuildDirectory", conf->BuildDirectory.Data); */
/*  */
/*     if (comments) */
/*         writer(context, "\n\n# All the c files to be compiled"); */
/*     writer(context, fmt_no_val, "Source"); */
/*  */
/*     ForList(String_List_Node, &conf->Source) { */
/*         ForListNode(&conf->Source, MAX_STRING_NODE_DATA_COUNT) { */
/*             writer(context, "\"%s\" ", it->Data[index].Data); */
/*         } */
/*     } */
/*     writer(context, ";\n\n"); */
/*  */
/*     ForList(Optionals_List_Node, &conf->Optionals) { */
/*         ForListNode(&conf->Optionals, MAX_OPTIONALS_IN_NODE) { */
/*             writer(context, fmt_no_val, it->Property_Keys[index].Data); */
/*             String_List *value_list = &it->Property_Values[index]; */
/*             ForList(String_List_Node, value_list) { */
/*                 ForListNode(value_list, MAX_STRING_NODE_DATA_COUNT) { */
/*                     writer(context, "\"%s\" ", it->Data[index].Data); */
/*                 } */
/*             } */
/*             writer(context, ";\n\n"); */
/*         } */
/*     } */
/* } */

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
	String Name;

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

	Memory_Arena *Arena;
} Compiler_Config;

typedef struct Compiler_Config_Node {
	Compiler_Config Config[16];
	struct Compiler_Config_Node *Next;
} Compiler_Config_Node;

typedef struct Compiler_Config_List {
	Compiler_Config_Node Head;
	Compiler_Config_Node *Tail;
	Uint32 Used;

	Build_Config BuildConfig;
	Memory_Arena *Arena;
} Compiler_Config_List;

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
	"Type of compilation. Project only build using a single muda file. Solution iterates through subdirectories and build each muda file found.",
	&CompilerKindInfo },

	{ StringExpand("Application"), Compiler_Config_Member_Enum, offsetof(Compiler_Config, Application),
	"The type of Application that is to be built", &ApplicationKindInfo },

	{ StringExpand("Optimization"), Compiler_Config_Member_Bool, offsetof(Compiler_Config, Optimization),
	"Use optimization while compiling or not (true/false)" },

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

static const bool CompilerConfigMemberTakeInput[ArrayCount(CompilerConfigMemberTypeInfo)] = {
	/*Kind*/ false, /*Application*/ false,

	/*Optimization*/ false,

	/*Build*/ true, /*BuildDirectory*/ true, /*Sources*/ true,
	/*Flags*/ false,
	/*Defines*/ true, /*IncludeDirectories*/ true,

	/*Subsystem*/ false,
	/*Libraries*/ true, /*LibraryDirectories*/ true,
	/*LinkerFlags*/ false
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
	config->Name = StringMake(NULL, 0);
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
}

INLINE_PROCEDURE void CompilerConfigListInit(Compiler_Config_List *list, Memory_Arena *arena) {
	list->Used = 0;
	list->Tail = &list->Head;
	list->Tail->Next = NULL;

	list->BuildConfig.ForceCompiler = 0;
	list->BuildConfig.ForceOptimization = false;
	list->BuildConfig.DisplayCommandLine = false;
	list->BuildConfig.DisableLogs = false;
	list->Arena = arena;
}

INLINE_PROCEDURE Compiler_Config *CompilerConfigListAdd(Compiler_Config_List *list, String name) {
	if (list->Used == ArrayCount(list->Head.Config)) {
		list->Used = 0;
		list->Tail->Next = PushSize(list->Arena, sizeof(Compiler_Config_Node));
		list->Tail = list->Tail->Next;
		list->Tail->Next = NULL;
	}

	Compiler_Config *dst = &list->Tail->Config[list->Used];
	list->Used += 1;
	CompilerConfigInit(dst, list->Arena);
	dst->Name = name;
	return dst;
}

INLINE_PROCEDURE Compiler_Config *CompilerConfigListFindOrAdd(Compiler_Config_List *list, String name) {
	ForList(Compiler_Config_Node, list) {
		ForListNode(list, ArrayCount(list->Head.Config)) {
			if (StrMatch(name, it->Config[index].Name)) {
				return &it->Config[index];
			}
		}
	}
	return CompilerConfigListAdd(list, StrDuplicateArena(name, list->Arena));
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

INLINE_PROCEDURE void PushDefaultCompilerConfig(Compiler_Config *config, bool write_log) {
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

INLINE_PROCEDURE void WriteCompilerConfig(Compiler_Config *conf, bool comments, Stream_Writer_Proc writer, void *context) {
	const char *fmt = "%-18s : %s;\n";
	const char *fmt_no_val = "%-18s : ";

	if (comments) {
		time_t t = time(NULL);
		struct tm tm = *localtime(&t);
		writer(context, "# Generated by: Muda Console Setup [%d-%02d-%02d %02d:%02d:%02d]\n\n",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	}

	writer(context, "@version %u.%u.%u\n\n", MUDA_VERSION_MAJOR, MUDA_VERSION_MINOR, MUDA_VERSION_PATCH);

	for (Uint32 index = 0; index < ArrayCount(CompilerConfigMemberTypeInfo); ++index) {
		const Compiler_Config_Member *const info = &CompilerConfigMemberTypeInfo[index];

		if (comments)
			writer(context, "# %s\n", info->Desc);

		switch (info->Kind) {
		case Compiler_Config_Member_Enum: {
			Enum_Info *en = (Enum_Info *)info->KindInfo;

			Uint32 *value = (Uint32 *)((char *)conf + info->Offset);
			Assert(*value < en->Count);

			writer(context, fmt, info->Name.Data, en->Ids[*value].Data);

			if (comments) {
				writer(context, "# Possible values: ");
				for (Uint32 index = 0; index < en->Count - 1; ++index)
					writer(context, "%s, ", en->Ids[index].Data);
				writer(context, "or %s\n", en->Ids[en->Count - 1].Data);
			}
		} break;

		case Compiler_Config_Member_Bool: {
			bool *in = (bool *)((char *)conf + info->Offset);
			writer(context, fmt, info->Name.Data, *in ? "True" : "False");
		} break;

		case Compiler_Config_Member_String: {
			Out_Stream *in = (Out_Stream *)((char *)conf + info->Offset);

			String value = OutBuildStringSerial(in, ThreadScratchpad());
			writer(context, fmt, info->Name.Data, value.Data);
		} break;

		case Compiler_Config_Member_String_Array: {
			String_List *in = (String_List *)((char *)conf + info->Offset);
			writer(context, fmt_no_val, info->Name.Data);

			ForList(String_List_Node, in) {
				ForListNode(in, MAX_STRING_NODE_DATA_COUNT) {
					writer(context, "%s ", it->Data[index].Data);
				}
			}

			writer(context, ";\n");
		} break;

			NoDefaultCase();
		}

		if (comments)
			writer(context, "\n");
	}
}

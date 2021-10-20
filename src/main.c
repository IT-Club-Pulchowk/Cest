#include "os.h"
#include "zBase.h"
#include "stream.h"
#include "muda_parser.h"
#include "lenstring.h"
#include "cmd_line.h"

typedef enum Muda_Parsing_OS {
    Muda_Parsing_OS_None,
    Muda_Parsing_OS_Windows,
    Muda_Parsing_OS_Linux,
    Muda_Parsing_OS_Mac,
} Muda_Parsing_OS;

typedef struct Muda_Parse_State {
    Muda_Parsing_OS OS;
    Compiler_Kind Compiler;
} Muda_Parse_State;

void MudaParseStateInit(Muda_Parse_State *state) {
    state->OS = Muda_Parsing_OS_None;
    state->Compiler = 0;
}

void DeserializeMuda(Compiler_Config_List *config_list, Uint8* data, Compiler_Kind compiler) {
	Memory_Arena *scratch = ThreadScratchpad();
    
    Muda_Parser prsr = MudaParseInit(data);

    Uint32 version = 0;
    Uint32 major = 0, minor = 0, patch = 0;

    while (MudaParseNext(&prsr))
        if (prsr.Token.Kind != Muda_Token_Comment) break;

    if (prsr.Token.Kind == Muda_Token_Tag && StrMatchCaseInsensitive(prsr.Token.Data.Tag.Title, StringLiteral("version"))) {
        if (prsr.Token.Data.Tag.Value.Data) {
            if (sscanf(prsr.Token.Data.Tag.Value.Data, "%d.%d.%d", &major, &minor, &patch) != 3)
                FatalError("Error: Bad file version\n");
        }
        else 
            FatalError("Error: Version info missing\n");
    }
    else 
        FatalError("Error: Version tag missing at top of file\n");

	version = MudaMakeVersion(major, minor, patch);

	if (version < MUDA_BACKWARDS_COMPATIBLE_VERSION || version > MUDA_CURRENT_VERSION) {
		String error = FmtStr(scratch, "Version %u.%u.%u not supported. \n"
			"Minimum version supported: %u.%u.%u\n"
			"Current version: %u.%u.%u\n\n",
			major, minor, patch,
			MUDA_BACKWARDS_COMPATIBLE_VERSION_MAJOR,
			MUDA_BACKWARDS_COMPATIBLE_VERSION_MINOR,
			MUDA_BACKWARDS_COMPATIBLE_VERSION_PATCH,
			MUDA_VERSION_MAJOR,
			MUDA_VERSION_MINOR,
			MUDA_VERSION_PATCH);
		FatalError(error.Data);
	}

    Muda_Parse_State state;
    MudaParseStateInit(&state);

    bool first_config_name = true;
    Compiler_Config *config = CompilerConfigListAdd(config_list, StringLiteral("default"));

    while (MudaParseNext(&prsr)) {
        switch (prsr.Token.Kind) {
            case Muda_Token_Config: {
                if (first_config_name) {
                    config->Name = StrDuplicateArena(prsr.Token.Data.Config, config->Arena);
                    first_config_name = false;
                }
                else {
                    config = CompilerConfigListFindOrAdd(config_list, prsr.Token.Data.Config);
                }
            } break;

            case Muda_Token_Section: {
                if (StrMatchCaseInsensitive(StringLiteral("OS.ALL"), prsr.Token.Data.Section))
                    state.OS = Muda_Parsing_OS_None;
                else if (StrMatchCaseInsensitive(StringLiteral("OS.WINDOWS"), prsr.Token.Data.Section))
                    state.OS = Muda_Parsing_OS_Windows;
                else if (StrMatchCaseInsensitive(StringLiteral("OS.LINUX"), prsr.Token.Data.Section))
                    state.OS = Muda_Parsing_OS_Linux;
                else if (StrMatchCaseInsensitive(StringLiteral("OS.MAC"), prsr.Token.Data.Section))
                    state.OS = Muda_Parsing_OS_Mac;
                else if (StrMatchCaseInsensitive(StringLiteral("COMPILER.ALL"), prsr.Token.Data.Section))
                    state.Compiler = 0;
                else if (StrMatchCaseInsensitive(StringLiteral("COMPILER.CL"), prsr.Token.Data.Section))
                    state.Compiler = Compiler_Bit_CL;
                else if (StrMatchCaseInsensitive(StringLiteral("COMPILER.CLANG"), prsr.Token.Data.Section))
                    state.Compiler = Compiler_Bit_CLANG;
                else if (StrMatchCaseInsensitive(StringLiteral("COMPILER.GCC"), prsr.Token.Data.Section))
                    state.Compiler = Compiler_Bit_GCC;
                else {
                    // TODO: Print line and column number in the error
                    String error = FmtStr(scratch, "Unknown Section: %s\n", prsr.Token.Data);
                    FatalError(error.Data);
                }
            } break;

            case Muda_Token_Property: {
                first_config_name = false;

                bool reject_os = !(state.OS == Muda_Parsing_OS_None || (PLATFORM_OS_WINDOWS && state.OS == Muda_Parsing_OS_Windows) ||
                    (PLATFORM_OS_LINUX && state.OS == Muda_Parsing_OS_Linux) ||
                    (PLATFORM_OS_MAC && state.OS == Muda_Parsing_OS_Mac));
                
                if (reject_os || (state.Compiler != 0 && compiler != state.Compiler)) break;

                bool property_found = false;

                Muda_Token *token = &prsr.Token;
                for (Uint32 index = 0; index < ArrayCount(CompilerConfigMemberTypeInfo); ++index) {
                    const Compiler_Config_Member *const info = &CompilerConfigMemberTypeInfo[index];
                    if (StrMatch(info->Name, token->Data.Property.Key)) {
                        property_found = true;

                        switch (info->Kind) {
                            case Compiler_Config_Member_Enum: {
                                Enum_Info *en = (Enum_Info *)info->KindInfo;

                                bool found = false;
                                for (Uint32 index = 0; index < en->Count; ++index) {
                                    if (StrMatch(en->Ids[index], token->Data.Property.Value)) {
                                        Uint32 *in = (Uint32 *)((char *)config + info->Offset);
                                        *in = index;
                                        found = true;
                                        break;
                                    }
                                }

                                if (!found) {
                                    Out_Stream out;
                                    Memory_Allocator allocator = MemoryArenaAllocator(scratch);
                                    OutCreate(&out, allocator);

                                    Temporary_Memory temp = BeginTemporaryMemory(scratch);

                                    for (Uint32 index = 0; index < en->Count; ++index) {
                                        OutString(&out, en->Ids[index]);
                                        OutBuffer(&out, ", ", 2);
                                    }

                                    String accepted_values = OutBuildString(&out, &allocator);

                                    // TODO: Print line and column number in the error
                                    LogWarn("Invalid value for Property \"%s\" : %s. Acceptable values are: %s\n",
                                        info->Name.Data, token->Data.Property.Value.Data, accepted_values);

                                    EndTemporaryMemory(&temp);
                                }
                            } break;

                            case Compiler_Config_Member_Bool: {
                                bool *in = (bool *)((char *)config + info->Offset);

                                if (StrMatch(StringLiteral("1"), token->Data.Property.Value) ||
                                    StrMatch(StringLiteral("True"), token->Data.Property.Value)) {
                                    *in = true;
                                }
                                else if (StrMatch(StringLiteral("0"), token->Data.Property.Value) ||
                                    StrMatch(StringLiteral("False"), token->Data.Property.Value)) {
                                    *in = false;
                                }
                                else {
                                    // TODO: Print line and column number in the error
                                    String error = FmtStr(scratch, "Expected boolean: %s\n", prsr.Token.Data);
                                    FatalError(error.Data);
                                }
                            } break;

                            case Compiler_Config_Member_String: {
                                Out_Stream *in = (Out_Stream *)((char *)config + info->Offset);
                                OutReset(in);
                                OutString(in, token->Data.Property.Value);
                            } break;

                            case Compiler_Config_Member_String_Array: {
                                String_List *in = (String_List *)((char *)config + info->Offset);
                                ReadList(in, token->Data.Property.Value, -1, config->Arena);
                            } break;

                            NoDefaultCase();
                        }

                        break;
                    }
                }

                if (!property_found) {
                    // TODO: Print line and column number in the error
                    LogWarn("Invalid Property \"%s\". Ignored.\n", token->Data.Property.Key.Data);
                }
            } break;

            case Muda_Token_Comment: {
                // ignored
            } break;

            case Muda_Token_Tag: {
                Unimplemented();
            } break;
        }
    }

    if (prsr.Token.Kind == Muda_Token_Error) {
        String errmsg = FmtStr(scratch, "%s at Line %d, Column %d\n", prsr.Token.Data.Error.Desc, prsr.Token.Data.Error.Line, prsr.Token.Data.Error.Column);
        FatalError(errmsg.Data);
    }
}

const char *GetCompilerName(Compiler_Kind kind) {
    if (kind == Compiler_Bit_CL)
        return "CL";
    else if (kind == Compiler_Bit_CLANG)
        return "CLANG";
    else if (kind == Compiler_Bit_GCC)
        return "GCC";
    Unreachable();
    return "";
}

typedef struct Directory_Iteration_Context {
    Memory_Arena *Arena;
    String_List *List;
    String_List *Ignore;
} Directory_Iteration_Context;

static Directory_Iteration DirectoryIteratorAddToList(const File_Info *info, void *user_context) {
    if ((info->Atribute & File_Attribute_Directory) && !(info->Atribute & File_Attribute_Hidden)) {
        if (StrMatch(info->Name, StringLiteral(".muda")))
            return Directory_Iteration_Continue;

        Directory_Iteration_Context *context = (Directory_Iteration_Context *)user_context;

        String_List *ignore = context->Ignore;
        ForList(String_List_Node, ignore) {
            ForListNode(ignore, MAX_STRING_NODE_DATA_COUNT) {
                String dir_path = info->Path;
                String dir_name = SubStr(dir_path, 2, dir_path.Length - 2);
#if PLATFORM_OS_WINDOWS == 1
                if (StrMatchCaseInsensitive(dir_name, it->Data[index]) || StrMatchCaseInsensitive(dir_path, it->Data[index]))
                    return Directory_Iteration_Continue;
#else
                if (StrMatch(dir_name, it->Data[index]) || StrMatch(dir_path, it->Data[index]))
                    return Directory_Iteration_Continue;
#endif
            }
        }

        StringListAdd(context->List, StrDuplicateArena(info->Path, context->Arena), context->Arena);
    }
    return Directory_Iteration_Continue;
}

void ExecuteMudaBuild(Compiler_Config *compiler_config, Build_Config *build_config, const Compiler_Kind compiler, const char *parent);
void SearchExecuteMudaBuild(Memory_Arena *arena, Build_Config *build_config, const Compiler_Kind compiler, Compiler_Config *alternative_config, const char *parent);

void ExecuteMudaBuild(Compiler_Config *compiler_config, Build_Config *build_config, const Compiler_Kind compiler, const char *parent) {
    Memory_Arena *scratch = ThreadScratchpad();

    Temporary_Memory temp = BeginTemporaryMemory(scratch);

    if (compiler_config->Prebuild.Size) {
        LogInfo("==> Executing Prebuild command\n");
        Temporary_Memory temp = BeginTemporaryMemory(scratch);
        String prebuild = OutBuildStringSerial(&compiler_config->Prebuild, scratch);
        if (!OsExecuteCommandLine(prebuild)) {
            LogError("Prebuild execution failed. Aborted.\n\n");
            return;
        }
        EndTemporaryMemory(&temp);
        LogInfo("Finished executing Prebuild command\n");
    }

    bool execute_postbuild = true;
    Muda_Plugin_Config plugin_config;
    memset(&plugin_config, 0, sizeof(plugin_config));

    if (compiler_config->Kind == Compile_Project) {
        LogInfo("Beginning compilation\n");

        Out_Stream out;
        OutCreate(&out, MemoryArenaAllocator(compiler_config->Arena));

        Out_Stream lib;
        OutCreate(&lib, MemoryArenaAllocator(compiler_config->Arena));

        Out_Stream res;
        OutCreate(&res, MemoryArenaAllocator(compiler_config->Arena));

        String build_dir = OutBuildStringSerial(&compiler_config->BuildDirectory, scratch);
        String build = OutBuildStringSerial(&compiler_config->Build, scratch);

        Uint32 result = OsCheckIfPathExists(build_dir);
        if (result == Path_Does_Not_Exist) {
            if (!OsCreateDirectoryRecursively(build_dir)) {
                LogError("Failed to create directory %s! Aborted.\n", build_dir.Data);
                return;
            }
        }
        else if (result == Path_Exist_File) {
            LogError("%s: Path exist but is a file! Aborted.\n", build_dir.Data);
            return;
        }

        if (compiler == Compiler_Bit_CL) {
            // For CL, we output intermediate files to "BuildDirectory/int"
            String intermediate;
            if (build_dir.Data[build_dir.Length - 1] == '/')
                intermediate = FmtStr(scratch, "%sint", build_dir.Data);
            else
                intermediate = FmtStr(scratch, "%s/int", build_dir.Data);

            result = OsCheckIfPathExists(intermediate);
            if (result == Path_Does_Not_Exist) {
                if (!OsCreateDirectoryRecursively(intermediate)) {
                    LogError("Failed to create directory %s! Aborted.\n", intermediate.Data);
                    return;
                }
            }
            else if (result == Path_Exist_File) {
                LogError("%s: Path exist but is a file! Aborted.\n", intermediate.Data);
                return;
            }
        }

        // Turn on Optimization if it is forced via command line
        if (build_config->ForceOptimization) {
            LogInfo("Optimization turned on forcefully\n");
            compiler_config->Optimization = true;
        }

        switch (compiler) {
        case Compiler_Bit_CL: {
            OutFormatted(&out, "cl -nologo -EHsc -W3 ");
            OutFormatted(&out, "%s ", compiler_config->Optimization ? "-O2" : "-Od");

            if (compiler_config->DebugSymbol) {
                OutFormatted(&out, "-Zi ");
            }

            ForList(String_List_Node, &compiler_config->Defines) {
                ForListNode(&compiler_config->Defines, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(&out, "-D%s ", it->Data[index].Data);
                }
            }

            ForList(String_List_Node, &compiler_config->IncludeDirectories) {
                ForListNode(&compiler_config->IncludeDirectories, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(&out, "-I\"%s\" ", it->Data[index].Data);
                }
            }

            ForList(String_List_Node, &compiler_config->Sources) {
                ForListNode(&compiler_config->Sources, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(&out, "\"%s\" ", it->Data[index].Data);
                }
            }

#if PLATFORM_OS_WINDOWS == 1
            if (compiler_config->ResourceFile.Size) {
                String resource_file = OutBuildStringSerial(&compiler_config->ResourceFile, scratch);
                OutFormatted(&res, "rc -fo \"%s/%s.res\" \"%s\" ", build_dir.Data, build.Data, resource_file.Data);
                OutFormatted(&out, "\"%s/%s.res\" ", build_dir.Data, build.Data);
            }
#endif

            ForList(String_List_Node, &compiler_config->Flags) {
                ForListNode(&compiler_config->Flags, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(&out, "%s ", it->Data[index].Data);
                }
            }

            OutFormatted(&out, "-Fd\"%s/\" ", build_dir.Data);

            if (compiler_config->Application != Application_Static_Library) {
                OutFormatted(&out, "-Fo\"%s/int/\" ", build_dir.Data);

                if (compiler_config->Application == Application_Dynamic_Library)
                    OutFormatted(&out, "-LD ");

                OutFormatted(&out, "-link ");
                OutFormatted(&out, "-pdb:\"%s/%s.pdb\" ", build_dir.Data, build.Data);

                if (compiler_config->Application != Application_Static_Library)
                    OutFormatted(&out, "-out:\"%s/%s.%s\" ", build_dir.Data, build.Data,
                        compiler_config->Application == Application_Executable ? "exe" : "dll");

                if (compiler_config->Application == Application_Dynamic_Library)
                    OutFormatted(&out, "-IMPLIB:\"%s/%s.lib\" ", build_dir.Data, build.Data);

                ForList(String_List_Node, &compiler_config->LinkerFlags) {
                    ForListNode(&compiler_config->LinkerFlags, MAX_STRING_NODE_DATA_COUNT) {
                        OutFormatted(&out, "%s ", it->Data[index].Data);
                    }
                }
            }
            else {
                OutFormatted(&out, "-c ");
                OutFormatted(&out, "-Fo\"%s/int/%s.obj\" ", build_dir.Data, build.Data);

                OutFormatted(&lib, "lib -nologo \"%s/int/%s.obj\" ", build_dir.Data, build.Data);
                OutFormatted(&lib, "-out:\"%s/%s.lib\" ", build_dir.Data, build.Data);
            }

            Out_Stream *target = ((compiler_config->Application != Application_Static_Library) ? &out : &lib);

            ForList(String_List_Node, &compiler_config->LibraryDirectories) {
                ForListNode(&compiler_config->LibraryDirectories, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(target, "-LIBPATH:\"%s\" ", it->Data[index].Data);
                }
            }

            ForList(String_List_Node, &compiler_config->Libraries) {
                ForListNode(&compiler_config->Libraries, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(target, "\"%s.lib\" ", it->Data[index].Data);
                }
            }

            if (PLATFORM_OS_WINDOWS) {
                OutFormatted(target, "-SUBSYSTEM:%s ", compiler_config->Subsystem == Subsystem_Console ? "CONSOLE" : "WINDOWS");
            }
        } break;

        case Compiler_Bit_CLANG: {
            Unimplemented();
        } break;

        case Compiler_Bit_GCC: {
            Unimplemented();
        } break;
        }

        String cmd_line = OutBuildStringSerial(&out, compiler_config->Arena);

        plugin_config.Name = compiler_config->Name.Data;
        plugin_config.Build = build.Data;
        plugin_config.BuildDir = build_dir.Data;
        plugin_config.MudaDir = (parent ? parent : ".");
        plugin_config.Succeeded = false;
        plugin_config.BuildKind = compiler_config->Application;

#if PLATFORM_OS_WINDOWS == 1
        if (compiler_config->Application == Application_Executable)
            plugin_config.BuildExtension = "exe";
        else if (compiler_config->Application == Application_Dynamic_Library)
            plugin_config.BuildExtension = "dll";
        else
            plugin_config.BuildExtension = "lib";
#elif PLATFORM_OS_LINUX
        if (compiler_config->Application == Application_Executable)
            plugin_config.BuildExtension = "out";
        else if (compiler_config->Application == Application_Dynamic_Library)
            plugin_config.BuildExtension = "so";
        else
            plugin_config.BuildExtension = "a";
#else
#error "Unimplemented"
#endif

        build_config->PluginHook(&ThreadContext, &build_config->Interface, Muda_Plugin_Event_Kind_Prebuild, &plugin_config);

        execute_postbuild = false;

        bool resource_compilation_passed = true;
        if (res.Size) {
            LogInfo("Executing Resource compilation\n");
            String resource_cmd_line = OutBuildStringSerial(&res, scratch);

            if (build_config->DisplayCommandLine) {
                LogInfo("Resource Command Line: %s\n", resource_cmd_line.Data);
            }

            if (OsExecuteCommandLine(resource_cmd_line)) {
                LogInfo("Resource Compilation succeeded\n");
            }
            else {
                LogInfo("Resource Compilation failed\n");
                resource_compilation_passed = false;
            }
        }

        if (resource_compilation_passed) {
            if (build_config->DisplayCommandLine) {
                LogInfo("Compiler Command Line: %s\n", cmd_line.Data);
            }

            LogInfo("Executing compilation\n");
            if (OsExecuteCommandLine(cmd_line)) {
                LogInfo("Compilation succeeded\n\n");
                if (lib.Size) {
                    if (build_config->DisplayCommandLine) {
                        LogInfo("Linker Command Line: %s\n", cmd_line.Data);
                    }

                    LogInfo("Creating static library\n");
                    cmd_line = OutBuildStringSerial(&lib, compiler_config->Arena);
                    if (OsExecuteCommandLine(cmd_line)) {
                        LogInfo("Library creation succeeded\n");
                        execute_postbuild = true;
                    }
                    else {
                        LogError("Library creation failed\n");
                    }
                }
                else {
                    execute_postbuild = true;
                }
            }
            else {
                LogError("Compilation failed\n\n");
            }
        }
    }
    else {
        Assert(compiler_config->Kind == Compile_Solution);

        LogInfo("==> Found Solution Muda Build\n");

        compiler_config->Kind = Compile_Project;

        Memory_Arena *dir_scratch = ThreadScratchpadI(1);

        String_List directory_list;
        String_List *filtered_list = &directory_list;

        if (StringListIsEmpty(&compiler_config->ProjectDirectories)) {
            StringListInit(&directory_list);

            Directory_Iteration_Context directory_iteration;
            directory_iteration.Arena = dir_scratch;
            directory_iteration.List = &directory_list;
            directory_iteration.Ignore = &compiler_config->IgnoredDirectories;
            OsIterateDirectroy(".", DirectoryIteratorAddToList, &directory_iteration);
        }
        else {
            filtered_list = &compiler_config->ProjectDirectories;
        }

        Memory_Arena *arena = compiler_config->Arena;
        ForList(String_List_Node, filtered_list) {
            ForListNode(filtered_list, MAX_STRING_NODE_DATA_COUNT) {
                LogInfo("==> Executing Muda Build in \"%s\" \n", it->Data[index].Data);
                if (OsSetWorkingDirectory(it->Data[index])) {
                    Temporary_Memory arena_temp = BeginTemporaryMemory(arena);
                    SearchExecuteMudaBuild(arena, build_config, compiler, compiler_config, it->Data[index].Data);
                    EndTemporaryMemory(&arena_temp);
                    if (!OsSetWorkingDirectory(StringLiteral(".."))) {
                        LogError("Could not set the original directory as the working directory! Aborted.\n");
                        return;
                    }
                }
                else {
                    LogError("Could not set \"%s\" as working directory, skipped.", it->Data[index].Data);
                }
            }
        }

        MemoryArenaReset(dir_scratch);
    }

    if (execute_postbuild && compiler_config->Postbuild.Size) {
        LogInfo("==> Executing Postbuild command\n");
        String prebuild = OutBuildStringSerial(&compiler_config->Postbuild, scratch);
        if (!OsExecuteCommandLine(prebuild)) {
            LogError("Postbuild execution failed. \n\n");
            return;
        }
        LogInfo("Finished executing Postbuild command\n");
    }

    if (compiler_config->Kind == Compile_Project) {
        plugin_config.Succeeded = execute_postbuild;
        build_config->PluginHook(&ThreadContext, &build_config->Interface, Muda_Plugin_Event_Kind_Postbuild, &plugin_config);
    }

	EndTemporaryMemory(&temp);
}

void SearchExecuteMudaBuild(Memory_Arena *arena, Build_Config *build_config, const Compiler_Kind compiler, Compiler_Config *alternative_config, const char *parent) {
    Temporary_Memory arena_temp = BeginTemporaryMemory(arena);

    Compiler_Config_List *configs = (Compiler_Config_List *)PushSize(arena, sizeof(Compiler_Config_List));
    CompilerConfigListInit(configs, arena);

    String config_path = { 0,0 };

    const String LocalMudaFile = StringLiteral("build.muda");
    if (OsCheckIfPathExists(LocalMudaFile) == Path_Exist_File) {
        config_path = LocalMudaFile;
    }
    else if (alternative_config) {
        CompilerConfigListCopy(configs, alternative_config);
    }
    else {
        String muda_user_path = OsGetUserConfigurationPath(StringLiteral("muda/config.muda"));
        if (OsCheckIfPathExists(muda_user_path) == Path_Exist_File) {
            config_path = muda_user_path;
        }
    }

    if (config_path.Length) {
        LogInfo("Found muda configuration file: \"%s\"\n", config_path.Data);

        Memory_Arena *scratch = ThreadScratchpad();
        Temporary_Memory temp = BeginTemporaryMemory(scratch);

        File_Handle fp = OsFileOpen(config_path, File_Mode_Read);
        if (fp.PlatformFileHandle) {
            Ptrsize size = OsFileGetSize(fp);
            const Ptrsize MAX_ALLOWED_MUDA_FILE_SIZE = MegaBytes(32);

            if (size > MAX_ALLOWED_MUDA_FILE_SIZE) {
                float max_size = (float)MAX_ALLOWED_MUDA_FILE_SIZE / (1024 * 1024);
                LogError("File %s too large. Max memory: %.3fMB! Aborted.\n", config_path.Data, max_size);
                return;
            }

            Uint8 *buffer = PushSize(scratch, size + 1);
            if (OsFileRead(fp, buffer, size) && size > 0) {
                buffer[size] = 0;
                LogInfo("Parsing muda file\n");
                DeserializeMuda(configs, buffer, compiler);
                LogInfo("Finished parsing muda file\n");
            }
            else if (size == 0) {
                LogError("File %s is empty!\n", config_path.Data);
            }
            else {
                LogError("Could not read the configuration file %s!\n", config_path.Data);
            }

            OsFileClose(fp);
        }
        else {
            LogError("Could not open the configuration file %s!\n", config_path.Data);
        }

        EndTemporaryMemory(&temp);
    }

    if (build_config->ConfigurationCount == 0) {
        ForList(Compiler_Config_Node, configs) {
            ForListNode(configs, ArrayCount(configs->Head.Config)) {
                Compiler_Config *config = &it->Config[index];
                LogInfo("==> Building Configuration: %s \n", config->Name.Data);
                PushDefaultCompilerConfig(config, config->Kind == Compile_Project);
                ExecuteMudaBuild(config, build_config, compiler, parent);
            }
        }
    }
    else {
        for (Uint32 index = 0; index < build_config->ConfigurationCount; ++index) {
            Compiler_Config *config = NULL;
            String required_config = build_config->Configurations[index];

            ForList(Compiler_Config_Node, configs) {
                ForListNode(configs, ArrayCount(configs->Head.Config)) {
                    if (StrMatch(it->Config[index].Name, required_config)) {
                        config = &it->Config[index];
                        break;
                    }
                }
            }

            if (config) {
                LogInfo("==> Building Configuration: %s \n", config->Name.Data);
                PushDefaultCompilerConfig(config, config->Kind == Compile_Project);
                ExecuteMudaBuild(config, build_config, compiler, parent);
            }
            else {
                LogError("Configuration \"%s\" not found. Ignored.\n", required_config.Data);
            }
        }
    }

    EndTemporaryMemory(&arena_temp);
}

int main(int argc, char *argv[]) {
    InitThreadContext(NullMemoryAllocator(), MegaBytes(512), (Log_Agent){ .Procedure = LogProcedure }, FatalErrorProcedure);

    OsSetupConsole();

    Build_Config build_config;
    BuildConfigInit(&build_config);
    if (HandleCommandLineArguments(argc, argv, &build_config))
        return 0;

    Compiler_Kind compiler = OsDetectCompiler();
    if (compiler == 0) {
        LogError("Failed to detect compiler! Installation of compiler is required...\n");
        if (PLATFORM_OS_WINDOWS)
            OsConsoleWrite("[Visual Studio - MSVC] https://visualstudio.microsoft.com/ \n");
        OsConsoleWrite("[CLANG] https://releases.llvm.org/download.html \n");
        OsConsoleWrite("[GCC] https://gcc.gnu.org/install/download.html \n");
        return 1;
    }

    if (build_config.DisableLogs)
        ThreadContext.LogAgent.Procedure = LogProcedureDisabled;

    if (build_config.LogFilePath) {
        File_Handle handle = OsFileOpen(StringMake(build_config.LogFilePath, strlen(build_config.LogFilePath)), File_Mode_Write);
        if (handle.PlatformFileHandle) {
            ThreadContext.LogAgent.Data = handle.PlatformFileHandle;
            LogInfo("Logging to file: %s\n", build_config.LogFilePath);
        }
        else {
            LogError("Could not open file: \"%s\" for logging. Logging to file disabled.\n");
        }
    }

    void *plugin = NULL;
    if (build_config.EnablePlugins) {
        plugin = OsLibraryLoad(MudaPluginPath);
        if (plugin) {
            build_config.PluginHook = (Muda_Event_Hook_Procedure)OsGetProcedureAddress(plugin, MudaPluginProcedureName);
            if (build_config.PluginHook) {
                build_config.PluginHook(&ThreadContext, &build_config.Interface, Muda_Plugin_Event_Kind_Detection, NULL);
                LogInfo("Plugin detected. Name: %s\n", build_config.Interface.PluginName);
            }
            else {
                LogWarn("Plugin dectected by could not be loaded\n");
            }
        }
    }

    if (build_config.ForceCompiler) {
        if (compiler & build_config.ForceCompiler) {
            compiler = build_config.ForceCompiler;
            LogInfo("Requested compiler: %s\n", GetCompilerName(compiler));
        }
        else {
            const char *requested_compiler = GetCompilerName(build_config.ForceCompiler);
            LogInfo("Requested compiler: %s but %s could not be detected\n",
                requested_compiler, requested_compiler);
        }
    }

    // Set one compiler from all available compilers
    // This priorities one compiler over the other
    if (compiler & Compiler_Bit_CL) {
        compiler = Compiler_Bit_CL;
        LogInfo("Compiler MSVC Detected.\n");
    }
    else if (compiler & Compiler_Bit_CLANG) {
        compiler = Compiler_Bit_CLANG;
        LogInfo("Compiler CLANG Detected.\n");
    }
    else {
        compiler = Compiler_Bit_GCC;
        LogInfo("Compiler GCC Detected.\n");
    }

    Memory_Arena arena = MemoryArenaCreate(MegaBytes(128));

    SearchExecuteMudaBuild(&arena, &build_config, compiler, NULL, NULL);

    build_config.PluginHook(&ThreadContext, &build_config.Interface, Muda_Plugin_Event_Kind_Destroy, NULL);

    if (ThreadContext.LogAgent.Data) {
        File_Handle handle;
        handle.PlatformFileHandle = ThreadContext.LogAgent.Data;
        OsFileClose(handle);
    }

    if (plugin) {
        OsLibraryFree(plugin);
    }
    
	return 0;
}

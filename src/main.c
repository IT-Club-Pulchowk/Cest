#include "os.h"
#include "zBase.h"
#include "stream.h"
#include "muda_parser.h"
#include "lenstring.h"
#include "cmd_line.h"

#if 0
static Directory_Iteration DirectoryIteratorPrintNoBin(const File_Info *info, void *user_context) {
	if (info->Atribute & File_Attribute_Hidden) return Directory_Iteration_Continue;
	if (StrMatch(info->Name, StringLiteral("bin"))) return Directory_Iteration_Continue;
	LogInfo("%s - %zu bytes\n", info->Path.Data, info->Size);
	return Directory_Iteration_Recurse;
}
#endif

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

void DeserializeCompilerConfig(Compiler_Config *config, Uint8* data, Compiler_Kind compiler) {
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

    while (MudaParseNext(&prsr)) {
        switch (prsr.Token.Kind) {
            case Muda_Token_Config: {
                Unimplemented();
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
                bool reject_os = !(state.OS == Muda_Parsing_OS_None || (PLATFORM_OS_WINDOWS && state.OS == Muda_Parsing_OS_Windows) ||
                    (PLATFORM_OS_LINUX && state.OS == Muda_Parsing_OS_Linux) ||
                    (PLATFORM_OS_MAC && state.OS == Muda_Parsing_OS_Mac));
                
                if (reject_os || (state.Compiler != 0 && compiler != state.Compiler)) break;

                Muda_Token *token = &prsr.Token;
                for (Uint32 index = 0; index < ArrayCount(CompilerConfigMemberTypeInfo); ++index) {
                    const Compiler_Config_Member *const info = &CompilerConfigMemberTypeInfo[index];
                    if (StrMatch(info->Name, token->Data.Property.Key)) {

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
                                    LogWarn("Invalid value for Property \"%s\" : %s. Acceptable values are: \n",
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

void Compile(Compiler_Config *config, Compiler_Kind compiler) {
    Memory_Arena *scratch = ThreadScratchpad();

    Temporary_Memory temp = BeginTemporaryMemory(scratch);

    LogInfo("Beginning compilation\n");

	Assert(config->Kind == Compile_Project);

	Out_Stream out;
	OutCreate(&out, MemoryArenaAllocator(config->Arena));
        
    String build_dir = OutBuildStringSerial(&config->BuildDirectory, scratch);
    String build     = OutBuildStringSerial(&config->Build, scratch);

    Uint32 result = OsCheckIfPathExists(build_dir);
    if (result == Path_Does_Not_Exist) {
        if (!OsCreateDirectoryRecursively(build_dir)) {
            String error = FmtStr(scratch, "Failed to create directory %s!\n", build_dir.Data);
            FatalError(error.Data);
        }
    }
    else if (result == Path_Exist_File) {
        String error = FmtStr(scratch, "%s: Path exist but is a file!\n", build_dir.Data);
        FatalError(error.Data);
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
                String error = FmtStr(scratch, "Failed to create directory %s!\n", intermediate.Data);
                FatalError(error.Data);
            }
        }
        else if (result == Path_Exist_File) {
            String error = FmtStr(scratch, "%s: Path exist but is a file!\n", intermediate.Data);
            FatalError(error.Data);
        }
    }

    // Turn on Optimization if it is forced via command line
    if (config->BuildConfig.ForceOptimization) {
        LogInfo("Optimization turned on forcefully\n");
        config->Optimization = true;
    }

    // TODO: Use the following values
    // Compiler_Config::Kind
    // Compiler_Config::Application
    // 

    switch (compiler) {
        case Compiler_Bit_CL: {
            LogInfo("Compiler MSVC Detected.\n");
            OutFormatted(&out, "cl -nologo -Zi -EHsc -W3 ");
            OutFormatted(&out, "%s ", config->Optimization ? "-O2" : "-Od");

            ForList(String_List_Node, &config->Defines) {
                ForListNode(&config->Defines, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(&out, "-D%s ", it->Data[index].Data);
                }
            }

            ForList(String_List_Node, &config->IncludeDirectories) {
                ForListNode(&config->IncludeDirectories, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(&out, "-I\"%s\" ", it->Data[index].Data);
                }
            }

            ForList(String_List_Node, &config->Sources) {
                ForListNode(&config->Sources, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(&out, "\"%s\" ", it->Data[index].Data);
                }
            }

            ForList(String_List_Node, &config->Flags) {
                ForListNode(&config->Flags, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(&out, "%s ", it->Data[index].Data);
                }
            }

            OutFormatted(&out, "-Fo\"%s/int/\" ", build_dir.Data);
            OutFormatted(&out, "-Fd\"%s/\" ", build_dir.Data);
            OutFormatted(&out, "-link ");
            OutFormatted(&out, "-out:\"%s/%s.exe\" ", build_dir.Data, build.Data);
            OutFormatted(&out, "-pdb:\"%s/%s.pdb\" ", build_dir.Data, build.Data);

            ForList(String_List_Node, &config->LibraryDirectories) {
                ForListNode(&config->LibraryDirectories, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(&out, "-LIBPATH:\"%s\" ", it->Data[index].Data);
                }
            }

            ForList(String_List_Node, &config->Libraries) {
                ForListNode(&config->Libraries, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(&out, "\"%s\" ", it->Data[index].Data);
                }
            }

            ForList(String_List_Node, &config->LinkerFlags) {
                ForListNode(&config->LinkerFlags, MAX_STRING_NODE_DATA_COUNT) {
                    OutFormatted(&out, "%s ", it->Data[index].Data);
                }
            }

            if (PLATFORM_OS_WINDOWS) {
                OutFormatted(&out, "-SUBSYSTEM:%s ", config->Subsystem == Subsystem_Console ? "CONSOLE" : "WINDOWS");
            }
        } break;

        case Compiler_Bit_CLANG: {
            Unimplemented();
            LogInfo("Compiler CLANG Detected.\n");
        } break;

        case Compiler_Bit_GCC: {
            Unimplemented();
            LogInfo("Compiler GCC Detected.\n");
        } break;
    }

    String cmd_line = OutBuildStringSerial(&out, config->Arena);

    if (config->BuildConfig.DisplayCommandLine) {
        LogInfo("Command Line: %s\n", cmd_line.Data);
    }

    LogInfo("Executing compilation\n");
    if (OsExecuteCommandLine(cmd_line))
        LogInfo("Compilation succeeded\n\n");
    else
        LogInfo("Compilation failed\n\n");

	EndTemporaryMemory(&temp);
}

int main(int argc, char *argv[]) {
    InitThreadContext(NullMemoryAllocator(), MegaBytes(512), (Log_Agent){ .Procedure = LogProcedure }, FatalErrorProcedure);

    OsSetupConsole();
    
    Memory_Arena arena = MemoryArenaCreate(MegaBytes(128));

    Compiler_Config config;
    CompilerConfigInit(&config, &arena);

    if (HandleCommandLineArguments(argc, argv, &config.BuildConfig))
        return 0;

    if (config.BuildConfig.DisableLogs)
        ThreadContext.LogAgent.Procedure = LogProcedureDisabled;

	Compiler_Kind compiler = OsDetectCompiler();
    if (compiler == 0) {
        LogError("Failed to detect compiler! Installation of compiler is required...\n");
        if (PLATFORM_OS_WINDOWS)
            OsConsoleWrite("[Visual Studio - MSVC] https://visualstudio.microsoft.com/ \n");
        OsConsoleWrite("[CLANG] https://releases.llvm.org/download.html \n");
        OsConsoleWrite("[GCC] https://gcc.gnu.org/install/download.html \n");
        return 1;
    }

    if (config.BuildConfig.ForceCompiler) {
        if (compiler & config.BuildConfig.ForceCompiler) {
            compiler = config.BuildConfig.ForceCompiler;
            LogInfo("Requested compiler: %s\n", GetCompilerName(compiler));
        }
        else {
            const char *requested_compiler = GetCompilerName(config.BuildConfig.ForceCompiler);
            LogInfo("Requested compiler: %s but %s could not be detected\n",
                requested_compiler, requested_compiler);
        }
    }

    // Set one compiler from all available compilers
    // This priorities one compiler over the other
    if (compiler & Compiler_Bit_CL)
        compiler = Compiler_Bit_CL;
    else if (compiler & Compiler_Bit_CLANG)
        compiler = Compiler_Bit_CLANG;
    else
        compiler = Compiler_Bit_GCC;

    String config_path = { 0,0 };

    const String LocalMudaFile = StringLiteral("build.muda");
    if (OsCheckIfPathExists(LocalMudaFile) == Path_Exist_File) {
        config_path = LocalMudaFile;
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
                String error = FmtStr(scratch, "File %s too large. Max memory: %.3fMB!\n", config_path.Data, max_size);
                FatalError(error.Data);
            }

            Uint8 *buffer = PushSize(scratch, size + 1);
            if (OsFileRead(fp, buffer, size) && size > 0) {
                buffer[size] = 0;
                LogInfo("Parsing muda file\n");
                DeserializeCompilerConfig(&config, buffer, compiler);
                LogInfo("Finished parsing muda file\n");
            } else if (size == 0) {
                LogError("File %s is empty!\n", config_path.Data);
            } else {
                LogError("Could not read the configuration file %s!\n", config_path.Data);
            }

            OsFileClose(fp);
        } else {
            LogError("Could not open the configuration file %s!\n", config_path.Data);
        }

        EndTemporaryMemory(&temp);
    }

    PushDefaultCompilerConfig(&config, true);

    Compile(&config, compiler);

	return 0;
}

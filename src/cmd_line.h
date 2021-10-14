#pragma once

#include "zBase.h"
#include "os.h"
#include "config.h"
#include "version.h"

typedef struct Muda_Option {
    String Name;
    const char *Desc;
    const char *Syntax;
    bool (*Proc)(const char *, const char *[], int count, Build_Config *, struct Muda_Option *);
    Int32 ParameterCount; // Negative means optional, for example: -2 means 2 optional parameters
} Muda_Option;

static bool OptVersion(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);
static bool OptDefault(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);
static bool OptSetup(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);
static bool OptCompiler(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);
static bool OptOptimize(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);
static bool OptCmdline(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);
static bool OptNoLog(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);
static bool OptHelp(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);
static bool OptUse(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);

static const Muda_Option Options[] = {
    { StringLiteralExpand("version"), "Check the version of Muda installed", "", OptVersion, 0 },
    { StringLiteralExpand("default"), "Display default configuration", "", OptDefault, 0 },
    { StringLiteralExpand("setup"), "Setup a Muda build system", "", OptSetup, 0 },
    { StringLiteralExpand("compiler"), "Forces to use specific compiler if the compiler is present", "<compiler_name>", OptCompiler, 1 },
    { StringLiteralExpand("optimize"), "Forces Optimization to be turned on", "", OptOptimize, 0 },
    { StringLiteralExpand("cmdline"), "Displays the command line executed to build", "", OptCmdline, 0 },
    { StringLiteralExpand("nolog"), "Disables logging in the terminal", "", OptNoLog, 0 },
    { StringLiteralExpand("help"), "Muda description and list all the command", "[command/s]", OptHelp, -255 },
    { StringLiteralExpand("use"), "Specify a section to use from the muda file", "[section]", OptUse, 1 },
};

static bool OptVersion(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option) {
    OsConsoleWrite("                        \n               _|   _,  \n/|/|/|  |  |  / |  / |  \n | | |_/ \\/|_/\\/|_/\\/|_/\n\n");
    OsConsoleWrite("Muda v %d.%d.%d\n\n", MUDA_VERSION_MAJOR, MUDA_VERSION_MINOR, MUDA_VERSION_PATCH);
    return true;
}

static bool OptDefault(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option) {
    ThreadContext.LogAgent.Procedure = LogProcedureDisabled;
    OsConsoleWrite(" ___                             \n(|  \\  _ |\\  _,        |\\_|_  ,  \n |   ||/ |/ / |  |  |  |/ |  / \\_\n(\\__/ |_/|_/\\/|_/ \\/|_/|_/|_/ \\/ \n         |)                      \n");
    Compiler_Config def;
    CompilerConfigInit(&def);
    PushDefaultCompilerConfig(&def, 0);

    /* WriteCompilerConfig(&def, false, OsConsoleOut, OsGetStdOutputHandle()); */

    OsConsoleWrite("\n");
    ThreadContext.LogAgent.Procedure = LogProcedure;
    return true;
}

static void OptSetupConfigFileWriter(void *context, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    OsFileWriteFV(*(File_Handle *)context, fmt, args);
    va_end(args);
}

static bool OptSetup(const char *program, const char *arg[], int count, Build_Config *build_config, Muda_Option *option) {
    OptVersion(program, arg, count, build_config, option);

    char read_buffer[512];
    String input;

    String muda_file = StringLiteral("build.muda");
    if (OsCheckIfPathExists(muda_file)) {
        OsConsoleWrite("Muda build file is already present in this directory. Enter [Y] or [y] to replace the file # > ");
        input = StrTrim(OsConsoleRead(read_buffer, sizeof(read_buffer)));
        if (input.Length != 1 || (input.Data[0] != 'y' && input.Data[0] != 'Y')) {
            OsConsoleWrite("Muda build file setup terminated.\n\n");
            return true;
        }
    }

    OsConsoleWrite("Muda Configuration:\n");
    OsConsoleWrite("Press [ENTER] to use the default values. Multiple values must be separated by [SPACE]\n\n");

    Memory_Arena *scratch = ThreadScratchpad();
    Memory_Allocator scratch_allocator = MemoryArenaAllocator(scratch);

    Push_Allocator pushed = PushThreadAllocator(MemoryArenaAllocator(scratch));

    Compiler_Config config;
    CompilerConfigInit(&config);

    ThreadContext.LogAgent.Procedure = LogProcedureDisabled;
    PushDefaultCompilerConfig(&config, 0);
    ThreadContext.LogAgent.Procedure = LogProcedure;

    OsConsoleWrite("Build Executable (default: %s) #\n   > ", OutBuildString(&config.Build, &scratch_allocator).Data);
    input = StrTrim(OsConsoleRead(read_buffer, sizeof(read_buffer)));
    if (input.Length) OutFormatted(&config.Build, "%s", input.Data);

    OsConsoleWrite("Build Directory (default: %s) #\n   > ", OutBuildString(&config.BuildDirectory, &scratch_allocator).Data);
    input = StrTrim(OsConsoleRead(read_buffer, sizeof(read_buffer)));
    if (input.Length) OutFormatted(&config.BuildDirectory, "%s", input.Data);

    /* OsConsoleWrite("Source (default: %s) #\n   > ", OutBuildString(&config.Source, &scratch_allocator).Data); */
    /* input = StrTrim(OsConsoleRead(read_buffer, sizeof(read_buffer))); */
    /* if (input.Length) { */
    /*     OutReset(&config.Source); */
    /*     String_List temp; */
    /*     ReadList(&temp, input, -1); */
    /*     for (String_List_Node* ntr = &temp.Head; ntr && temp.Used; ntr = ntr->Next){ */
    /*         int len = ntr->Next ? 8 : temp.Used; */
    /*         for (int i = 0; i < len; i ++) { */
    /*             if (compiler & Compiler_Bit_CL) */   // NO COMPILER WHAT DO?
    /*                 OutFormatted(&config.Source, "\"%s\" ", ntr->Data[i].Data); */
    /*             else */
    /*                 OutFormatted(&config.Source, "%s ", ntr->Data[i].Data); */
    /*         } */
    /*     } */
    /* } */

    // TODO: More input??

    PopThreadAllocator(&pushed);

    File_Handle fhandle = OsFileOpen(StringLiteral("build.muda"), File_Mode_Write);
    /* WriteCompilerConfig(&config, true, OptSetupConfigFileWriter, &fhandle); */
    OsFileClose(fhandle);

    OsConsoleWrite("Muda build file setup completed.\n\n");

    return true;
}

static bool OptCompiler(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option) {
    if (config->ForceCompiler != 0) {
        LogError("Can't request multiple compilers at once!\n\n");
        return true;
    }

    String suggestion = StringMake(arg[0], strlen(arg[0]));
    if (StrMatchCaseInsensitive(suggestion, StringLiteral("clang")))
            config->ForceCompiler |= Compiler_Bit_CLANG;
    else if (StrMatchCaseInsensitive(suggestion, StringLiteral("gcc")))
            config->ForceCompiler |= Compiler_Bit_GCC;
    else if (StrMatchCaseInsensitive(suggestion, StringLiteral("cl")) ||
             StrMatchCaseInsensitive(suggestion, StringLiteral("msvc")))
            config->ForceCompiler |= Compiler_Bit_CL;
    else {
        LogError("Unknown compiler \"%s\"\n\n", arg[0]);
        return true;
    }

    return false;
}

static bool OptUse (const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option) {
    config->UseSection = StringMake(arg[0], strlen(arg[0]));
    return false;
}

static bool OptOptimize(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option) {
    config->ForceOptimization = true;
    return false;
}

static bool OptCmdline(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option) {
    config->DisplayCommandLine = true;
    return false;
}

static bool OptNoLog(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option) {
    config->DisableLogs = true;
    return false;
}

static bool OptHelp(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option) {
    if (count) {
        for (int i = 0; i < count; ++i) {
            String name = StringMake(arg[i], (Int64)strlen(arg[i]));

            bool found = false;
            for (int opt_i = 0; opt_i < ArrayCount(Options); ++opt_i) {
                if (StrMatchCaseInsensitive(name, Options[opt_i].Name)) {
                    OsConsoleWrite("   %-s:\n\tDesc: %s\n\tSyntax: %s -%s %s\n", 
                        Options[opt_i].Name.Data, Options[opt_i].Desc,
                        program, Options[opt_i].Name.Data, Options[opt_i].Syntax);
                    found = true;
                    break;
                }
            }

            if (!found)
                OsConsoleWrite("   %-15s: Invalid command\n", name.Data);
        }

        OsConsoleWrite("\nUse: \"%s -%s\" for list of all commands present.\n\n", program, option->Name.Data);
    }
    else {
        // Dont judge me pls I was bored
        OptVersion(program, arg, count, config, option);
        OsConsoleWrite("Usage:\n\tpath/to/muda [-flags | build_script]\n\n");
        OsConsoleWrite("Flags:\n");

        for (int opt_i = 0; opt_i < ArrayCount(Options); ++opt_i) {
            OsConsoleWrite("\t-%-10s: %s\n", Options[opt_i].Name.Data, Options[opt_i].Desc);
        }

        OsConsoleWrite("\nFor more information on command, type:\n\t%s -%s %s\n", program, option->Name.Data, option->Syntax);
        OsConsoleWrite("\nRepository:\n\thttps://github.com/IT-Club-Pulchowk/muda\n\n");
    }
    return true;
}

//
//
//

static bool HandleCommandLineArguments(int argc, char *argv[], Build_Config *config) {
    if (argc < 2) return false;

    bool handled = false;

	for (int argi = 1; argi < argc; ++argi) {
		if (argv[argi][0] != '-') {
            Memory_Arena *scratch = ThreadScratchpad();
            String error = FmtStr(scratch, "Command line arguments must begin with \"-\". "
                "For list of command type:\n\t%s -help\n\n", argv[argi]);
            FatalError(error.Data);
		}

		String option_name = StringMake(argv[argi] + 1, strlen(argv[argi] + 1));

        bool cmd_executed = false;

		for (int opt_i = 0; opt_i < ArrayCount(Options); ++opt_i) {
			if (StrMatchCaseInsensitive(option_name, Options[opt_i].Name)) {
				const Muda_Option *const option = &Options[opt_i];

				int number_of_args_left = argc - argi - 1;
                int parameter_count = 0;
                bool not_enough_parameter = false;

				if (number_of_args_left >= option->ParameterCount) {
                    int total_parameter = option->ParameterCount < 0 ? 
                        option->ParameterCount * -1 : option->ParameterCount;
                    total_parameter = Minimum(total_parameter, number_of_args_left);

					for (int pi = 0; pi < total_parameter; ++pi) {
						if (argv[pi + argi + 1][0] == '-')
                            break;
                        parameter_count += 1;
					}

                    if (parameter_count == option->ParameterCount || option->ParameterCount < 0) {
                        handled = option->Proc(argv[0], argv + argi + 1, parameter_count, config, (Muda_Option *)option) || handled;
                        cmd_executed = true;
                    }
                    else {
                        not_enough_parameter = true;
                    }

                    argi += parameter_count;
                }
                else {
                    not_enough_parameter = true;
                }

                if (not_enough_parameter) {
                    Memory_Arena *scratch = ThreadScratchpad();
                    String error = FmtStr(scratch, "Expected %d number of parameters for %s\n\n",
                        option->ParameterCount, option->Name.Data);
                    FatalError(error.Data);
                }

                break;
			}
		}

        if (!cmd_executed) {
            Memory_Arena *scratch = ThreadScratchpad();
            String error = FmtStr(scratch, "Command \"%s\" not found. For list of all available commands, type:\n"
                "\t%s -help\n\n",
                option_name.Data, argv[0]);
            FatalError(error.Data);
        }
	}

    return handled;
}

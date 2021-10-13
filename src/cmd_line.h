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

static const Muda_Option Options[] = {
    { StringLiteralExpand("version"), "Check the version of Muda installed", "", OptVersion, 0 },
    { StringLiteralExpand("default"), "Display default configuration", "", OptDefault, 0 },
    { StringLiteralExpand("setup"), "Setup a Muda build system", "", OptSetup, 0 },
    { StringLiteralExpand("compiler"), "Forces to use specific compiler if the compiler is present", "<compiler_name>", OptCompiler, 1 },
    { StringLiteralExpand("optimize"), "Forces Optimization to be turned on", "", OptOptimize, 0 },
    { StringLiteralExpand("cmdline"), "Displays the command line executed to build", "", OptCmdline, 0 },
    { StringLiteralExpand("nolog"), "Disables logging in the terminal", "", OptNoLog, 0 },
    { StringLiteralExpand("help"), "Muda description and list all the command", "[command/s]", OptHelp, -255 },
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

    WriteCompilerConfig(&def, false, OsConsoleOut, OsGetStdOutputHandle());

    OsConsoleWrite("\n");
    ThreadContext.LogAgent.Procedure = LogProcedure;
    return true;
}

static bool OptSetup(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option) {
    OptVersion(program, arg, count, config, option);

    OsConsoleWrite("Muda Configuration:\n");

    Compiler_Config def;
    CompilerConfigInit(&def);
    PushDefaultCompilerConfig(&def, 0);

    // We take these as default
    // Type = Project
    // Optimization = false

    // TODO: We are not using what we have input from console yet... :(
    // TODO: Remove white spaces at start and end of the string as well

    char read_buffer[256];
    File_Handle fhandle = OsFileOpen(StringLiteral("build.muda"), File_Mode_Write);

    OsFileWriteF(fhandle, "@version %u.%u.%u\n\n", MUDA_VERSION_MAJOR, MUDA_VERSION_MINOR, MUDA_VERSION_PATCH);
    OsFileWrite(fhandle, StringLiteral("# Made With -setup\n\n"));
    OsFileWrite(fhandle, StringLiteral("Type=Project;\n"));
    OsFileWrite(fhandle, StringLiteral("Optimization=false;\n"));

    OsConsoleWrite("Build Directory (default: %s) #\n   > ", def.BuildDirectory.Data);
    OsFileWriteF(fhandle, "BuildDirectory=%s%s", OsConsoleRead(read_buffer, sizeof(read_buffer)).Data, ";\n");

    OsConsoleWrite("Build Executable (default: %s) #\n   > ", def.Build.Data);
    OsFileWriteF(fhandle, "Build=%s;\n", OsConsoleRead(read_buffer, sizeof(read_buffer)).Data);

    OsConsoleWrite("Defines #\n   > ");
    OsFileWriteF(fhandle, "Define=%s;\n", OsConsoleRead(read_buffer, sizeof(read_buffer)).Data);

    OsConsoleWrite("Include Directory #\n   > ");
    OsFileWriteF(fhandle, "IncludeDirectory=%s;\n", OsConsoleRead(read_buffer, sizeof(read_buffer)).Data);

    OsConsoleWrite("Source (default: %s) #\n   > ", def.Source.Head.Data[0].Data);
    OsFileWriteF(fhandle, "Source=%s;\n", OsConsoleRead(read_buffer, sizeof(read_buffer)).Data);

    OsConsoleWrite("Library Directory #\n   > ");
    OsFileWriteF(fhandle, "LibraryDirectory=%s;\n", OsConsoleRead(read_buffer, sizeof(read_buffer)).Data);

    OsConsoleWrite("Input Library #\n   > ");
    OsFileWriteF(fhandle, "Library=%s;\n", OsConsoleRead(read_buffer, sizeof(read_buffer)).Data);

    OsConsoleWrite("\n");
    OsFileClose(fhandle);

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

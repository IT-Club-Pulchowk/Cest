#pragma once

#include "config.h"
#include "os.h"
#include "version.h"
#include "zBase.h"

typedef struct Muda_Option
{
    String      Name;
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
static bool OptConfig(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);
static bool OptLog(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);
static bool OptNoPlug(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);
static bool OptHelp(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option);

static const Muda_Option Options[] = {
    {StringExpand("version"), "Check the version of Muda installed", "", OptVersion, 0},
    {StringExpand("default"), "Display default configuration", "", OptDefault, 0},
    {StringExpand("setup"), "Setup a Muda build system", "", OptSetup, -1},
    {StringExpand("compiler"), "Forces to use specific compiler if the compiler is present", "<compiler_name>",
     OptCompiler, 1},
    {StringExpand("optimize"), "Forces Optimization to be turned on", "", OptOptimize, 0},
    {StringExpand("cmdline"), "Displays the command line executed to build", "", OptCmdline, 0},
    {StringExpand("nolog"), "Disables logging in the terminal", "", OptNoLog, 0},
    {StringExpand("config"), "Specify default or configurations to use from the muda file.", "[configuration/s]",
     OptConfig, -255},
    {StringExpand("log"), "Log to the given file", "<file>", OptLog, 1},
    {StringExpand("noplug"), "Plugin are not loaded", "", OptNoPlug, 0},
    {StringExpand("help"), "Muda description and list all the command", "[command/s]", OptHelp, -255},
};

static bool OptVersion(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option)
{
    OsConsoleWrite("                        \n               _|   _,  \n/|/|/|  |  |  / |  / |  \n | | |_/ "
                   "\\/|_/\\/|_/\\/|_/\n\n");
    OsConsoleWrite("Muda v %d.%d.%d\n\n", MUDA_VERSION_MAJOR, MUDA_VERSION_MINOR, MUDA_VERSION_PATCH);
    return true;
}

static bool OptDefault(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option)
{
    OsConsoleWrite(" ___                             \n(|  \\  _ |\\  _,        |\\_|_  ,  \n |   ||/ |/ / |  |  |  |/ "
                   "|  / \\_\n(\\__/ |_/|_/\\/|_/ \\/|_/|_/|_/ \\/ \n         |)                      \n");
    Compiler_Config def;
    CompilerConfigInit(&def, ThreadScratchpad());
    PushDefaultCompilerConfig(&def, false);

    WriteCompilerConfig(&def, false, OsConsoleOut, OsGetStdOutputHandle());

    OsConsoleWrite("\n");
    return true;
}

static void OptSetupConfigFileWriter(void *context, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    OsFileWriteFV(*(File_Handle *)context, fmt, args);
    va_end(args);
}

static bool OptSetup(const char *program, const char *arg[], int count, Build_Config *build_config, Muda_Option *option)
{
    bool complete_setup = false;

    if (count)
    {
        if (StrMatchCaseInsensitive(StringLiteral("complete"), StringMake(arg[0], strlen(arg[0]))))
        {
            complete_setup = true;
        }
        else
        {
            LogError("Unknown option: %s. %s Only accepts complete\n", arg[0], option->Name.Data);
            return true;
        }
    }

    OptVersion(program, arg, count, build_config, option);

    const Uint32 READ_MAX_SIZE = 512;
    char        *read_buffer   = PushSize(ThreadScratchpad(), READ_MAX_SIZE);

    String       muda_file     = StringLiteral("build.muda");
    if (OsCheckIfPathExists(muda_file) == Path_Exist_File)
    {
        OsConsoleWrite(
            "Muda build file is already present in this directory. Enter [Y] or [y] to replace the file # > ");
        String input = StrTrim(OsConsoleRead(read_buffer, sizeof(read_buffer)));
        if (input.Length != 1 || (input.Data[0] != 'y' && input.Data[0] != 'Y'))
        {
            OsConsoleWrite("Muda build file setup terminated.\n\n");
            return true;
        }
    }

    OsConsoleWrite("Muda Configuration:\n");
    OsConsoleWrite("Press [ENTER] to use the default values. Multiple values must be separated by [SPACE]\n\n");

    Compiler_Config config;
    CompilerConfigInit(&config, ThreadScratchpad());
    PushDefaultCompilerConfig(&config, false);

    for (Uint32 index = 0; index < ArrayCount(CompilerConfigMemberTypeInfo); ++index)
    {
        const Compiler_Config_Member *const info = &CompilerConfigMemberTypeInfo[index];
        if (!CompilerConfigMemberTakeInput[index] && !complete_setup)
            continue;

        switch (info->Kind)
        {
        case Compiler_Config_Member_Enum: {
            Enum_Info *en    = (Enum_Info *)info->KindInfo;

            Uint32    *value = (Uint32 *)((char *)&config + info->Offset);
            Assert(*value < en->Count);

            OsConsoleWrite("%s (", info->Name.Data);
            for (Uint32 index = 0; index < en->Count - 1; ++index)
            {
                if (index == *value)
                    OsConsoleWrite("default:");
                OsConsoleWrite("%s, ", en->Ids[index].Data);
            }
            if (en->Count - 1 == *value)
                OsConsoleWrite("default:");
            OsConsoleWrite("%s) #\n   > ", en->Ids[en->Count - 1].Data);

            String input = StrTrim(OsConsoleRead(read_buffer, READ_MAX_SIZE));
            if (input.Length)
            {
                bool found = false;
                for (Uint32 index = 0; index < en->Count; ++index)
                {
                    if (StrMatch(en->Ids[index], input))
                    {
                        *value = index;
                        found  = true;
                        break;
                    }
                }

                if (!found)
                {
                    OsConsoleWrite("   Invalid value \"%s\". Using default value.\n", input.Data);
                }
            }
        }
        break;

        case Compiler_Config_Member_Bool: {
            bool *in = (bool *)((char *)&config + info->Offset);
            OsConsoleWrite("%s (default:%s) #\n   > ", info->Name.Data, *in ? "True" : "False");

            String input = StrTrim(OsConsoleRead(read_buffer, READ_MAX_SIZE));
            if (input.Length)
            {
                if (StrMatch(StringLiteral("1"), input) || StrMatchCaseInsensitive(StringLiteral("True"), input))
                {
                    *in = true;
                }
                else if (StrMatch(StringLiteral("0"), input) || StrMatchCaseInsensitive(StringLiteral("False"), input))
                {
                    *in = false;
                }
                else
                {
                    OsConsoleWrite("   Invalid value \"%s\". Using default value.\n", input.Data);
                }
            }
        }
        break;

        case Compiler_Config_Member_String: {
            Out_Stream *in    = (Out_Stream *)((char *)&config + info->Offset);
            String      value = OutBuildStringSerial(in, ThreadScratchpad());

            if (value.Length)
                OsConsoleWrite("%s (default:%s) #\n   > ", info->Name.Data, value.Data);
            else
                OsConsoleWrite("%s #\n   > ", info->Name.Data);

            String input = StrTrim(OsConsoleRead(read_buffer, READ_MAX_SIZE));
            if (input.Length)
            {
                OutReset(in);
                OutString(in, input);
            }
        }
        break;

        case Compiler_Config_Member_String_Array: {
            String_List *in = (String_List *)((char *)&config + info->Offset);

            if (StringListIsEmpty(in))
            {
                OsConsoleWrite("%s #\n   > ", info->Name.Data);
            }
            else
            {
                OsConsoleWrite("%s (default:", info->Name.Data);
                ForList(String_List_Node, in)
                {
                    ForListNode(in, MAX_STRING_NODE_DATA_COUNT)
                    {
                        OsConsoleWrite("%s ", it->Data[index].Data);
                    }
                }
                OsConsoleWrite(") #\n   > ");
            }

            String input = StrTrim(OsConsoleRead(read_buffer, READ_MAX_SIZE));
            if (input.Length)
            {
                StringListClear(in);
                ReadList(in, input, -1, config.Arena);
            }
        }
        break;

            NoDefaultCase();
        }
    }

    String      post_value = StringLiteral(":OS.WINDOWS\n"
                                                "# Place values specific to windows here\n\n"
                                                ":OS.LINUX\n"
                                                "# Place values specific to linux here\n\n"
                                                ":OS.MAC\n"
                                                "#Place values specific to mac here\n\n"
                                                ":COMPILER.CL\n"
                                                "#Place values specific to MSVC compiler here\n\n"
                                                ": COMPILER.CLANG\n"
                                                "#Place values specific to CLANG compiler here\n\n"
                                                ":COMPILER.GCC\n"
                                                "#Place values specific to GC compiler here\n\n");

    File_Handle fhandle    = OsFileOpen(StringLiteral("build.muda"), File_Mode_Write);
    WriteCompilerConfig(&config, true, OptSetupConfigFileWriter, &fhandle);
    OsFileWrite(fhandle, post_value);
    OsFileClose(fhandle);

    OsConsoleWrite("Muda build file setup completed.\n\n");

    return true;
}

static bool OptCompiler(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option)
{
    if (config->ForceCompiler != 0)
    {
        LogError("Can't request multiple compilers at once!\n\n");
        return true;
    }

    String suggestion = StringMake(arg[0], strlen(arg[0]));
    if (StrMatchCaseInsensitive(suggestion, StringLiteral("clang")))
        config->ForceCompiler = Compiler_Bit_CLANG;
    else if (StrMatchCaseInsensitive(suggestion, StringLiteral("gcc")))
        config->ForceCompiler = Compiler_Bit_GCC;
    else if (StrMatchCaseInsensitive(suggestion, StringLiteral("cl")) ||
             StrMatchCaseInsensitive(suggestion, StringLiteral("msvc")))
        config->ForceCompiler = Compiler_Bit_CL;
    else
    {
        LogError("Unknown compiler \"%s\"\n\n", arg[0]);
        return true;
    }

    return false;
}

static void OptConfigAdd(Build_Config *config, String name)
{
    if (config->ConfigurationCount < ArrayCount(config->Configurations))
    {
        config->Configurations[config->ConfigurationCount++] = name;
    }
    else
    {
        Uint32 count = (Uint32)ArrayCount(config->Configurations);
        LogWarn("Only %u number of configurations are supported from command line. \"%s\" configuration ignored.\n",
                count, name.Data);
    }
}

static bool OptConfig(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option)
{
    if (count)
    {
        for (int i = 0; i < count; ++i)
            OptConfigAdd(config, StringMake(arg[i], strlen(arg[i])));
    }
    else
    {
        OptConfigAdd(config, StringLiteral("default"));
    }
    return false;
}

static bool OptOptimize(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option)
{
    config->ForceOptimization = true;
    return false;
}

static bool OptCmdline(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option)
{
    config->DisplayCommandLine = true;
    return false;
}

static bool OptNoLog(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option)
{
    config->DisableLogs = true;
    return false;
}

static bool OptLog(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option)
{
    if (config->LogFilePath)
    {
        LogError("Logging to more than one file not supported. Not logging to: \"%s\"\n", arg[0]);
        return false;
    }
    config->LogFilePath = arg[0];
    return false;
}

static bool OptNoPlug(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option)
{
    config->EnablePlugins = false;
    return false;
}

static bool OptHelp(const char *program, const char *arg[], int count, Build_Config *config, Muda_Option *option)
{
    if (count)
    {
        for (int i = 0; i < count; ++i)
        {
            String name  = StringMake(arg[i], (Int64)strlen(arg[i]));

            bool   found = false;
            for (int opt_i = 0; opt_i < ArrayCount(Options); ++opt_i)
            {
                if (StrMatchCaseInsensitive(name, Options[opt_i].Name))
                {
                    OsConsoleWrite("   %-s:\n\tDesc: %s\n\tSyntax: %s -%s %s\n", Options[opt_i].Name.Data,
                                   Options[opt_i].Desc, program, Options[opt_i].Name.Data, Options[opt_i].Syntax);
                    found = true;
                    break;
                }
            }

            if (!found)
                OsConsoleWrite("   %-15s: Invalid command\n", name.Data);
        }

        OsConsoleWrite("\nUse: \"%s -%s\" for list of all commands present.\n\n", program, option->Name.Data);
    }
    else
    {
        // Dont judge me pls I was bored
        OptVersion(program, arg, count, config, option);
        OsConsoleWrite("Usage:\n\tpath/to/muda [-flags | build_script]\n\n");
        OsConsoleWrite("Flags:\n");

        for (int opt_i = 0; opt_i < ArrayCount(Options); ++opt_i)
        {
            OsConsoleWrite("\t-%-10s: %s\n", Options[opt_i].Name.Data, Options[opt_i].Desc);
        }

        OsConsoleWrite("\nFor more information on command, type:\n\t%s -%s %s\n", program, option->Name.Data,
                       option->Syntax);
        OsConsoleWrite("\nRepository:\n\thttps://github.com/IT-Club-Pulchowk/muda\n\n");
    }
    return true;
}

//
//
//

static bool HandleCommandLineArguments(int argc, char *argv[], Build_Config *config)
{
    if (argc < 2)
        return false;

    bool handled = false;

    for (int argi = 1; argi < argc; ++argi)
    {
        if (argv[argi][0] != '-')
        {
            Memory_Arena *scratch = ThreadScratchpad();
            String        error   = FmtStr(scratch,
                                           "Unknown identifier: \"%s\".Command line arguments must begin with \"-\". "
                                                    "For list of command type:\n\t%s -help\n\n",
                                           argv[argi], argv[0]);
            FatalError(error.Data);
        }

        String option_name  = StringMake(argv[argi] + 1, strlen(argv[argi] + 1));

        bool   cmd_executed = false;

        for (int opt_i = 0; opt_i < ArrayCount(Options); ++opt_i)
        {
            if (StrMatchCaseInsensitive(option_name, Options[opt_i].Name))
            {
                const Muda_Option *const option               = &Options[opt_i];

                int                      number_of_args_left  = argc - argi - 1;
                int                      parameter_count      = 0;
                bool                     not_enough_parameter = false;

                if (number_of_args_left >= option->ParameterCount)
                {
                    int total_parameter =
                        option->ParameterCount < 0 ? option->ParameterCount * -1 : option->ParameterCount;
                    total_parameter = Minimum(total_parameter, number_of_args_left);

                    for (int pi = 0; pi < total_parameter; ++pi)
                    {
                        if (argv[pi + argi + 1][0] == '-')
                            break;
                        parameter_count += 1;
                    }

                    if (parameter_count == option->ParameterCount || option->ParameterCount < 0)
                    {
                        handled = option->Proc(argv[0], (const char **)(argv + argi + 1), parameter_count, config,
                                               (Muda_Option *)option) ||
                                  handled;
                        cmd_executed = true;
                    }
                    else
                    {
                        not_enough_parameter = true;
                    }

                    argi += parameter_count;
                }
                else
                {
                    not_enough_parameter = true;
                }

                if (not_enough_parameter)
                {
                    Memory_Arena *scratch = ThreadScratchpad();
                    String        error   = FmtStr(scratch, "Expected %d number of parameters for %s\n\n",
                                                   option->ParameterCount, option->Name.Data);
                    FatalError(error.Data);
                }

                break;
            }
        }

        if (!cmd_executed)
        {
            Memory_Arena *scratch = ThreadScratchpad();
            String        error   = FmtStr(scratch,
                                           "Command \"%s\" not found. For list of all available commands, type:\n"
                                                    "\t%s -help\n\n",
                                           option_name.Data, argv[0]);
            FatalError(error.Data);
        }
    }

    return handled;
}

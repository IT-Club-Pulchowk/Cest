#pragma once

#include "zBase.h"
#include "os.h"
#include "config.h"
#include "version.h"

typedef enum Muda_Option_Argument {
    Muda_Option_Argument_Empty,
    Muda_Option_Argument_Needed,
    Muda_Option_Argument_Optional,
} Muda_Option_Argument;

typedef struct Muda_Option {
    String Name;
    String Desc;
    bool (*Proc)(const char *, const char *, Build_Config *);
    Muda_Option_Argument Argument;
} Muda_Option;

static bool OptVersion(const char *program, const char *arg, Build_Config *config);
static bool OptDefault(const char *program, const char *arg, Build_Config *config);
static bool OptSetup(const char *program, const char *arg, Build_Config *config);
static bool OptOptimize(const char *program, const char *arg, Build_Config *config);
static bool OptCmdline(const char *program, const char *arg, Build_Config *config);
static bool OptHelp(const char *program, const char *arg, Build_Config *config);

static const Muda_Option Options[] = {
    { StringLiteralExpand("version"), StringLiteralExpand("Check the version of Muda installed"), OptVersion, Muda_Option_Argument_Empty },
    { StringLiteralExpand("default"), StringLiteralExpand("Display default configuration"), OptDefault, Muda_Option_Argument_Empty },
    { StringLiteralExpand("setup"), StringLiteralExpand("Setup a Muda build system"), OptSetup, Muda_Option_Argument_Empty },
    { StringLiteralExpand("optimize"), StringLiteralExpand("Forces Optimization to be turned on"), OptOptimize, Muda_Option_Argument_Empty },
    { StringLiteralExpand("cmdline"), StringLiteralExpand("Displays the command line executed to build"), OptCmdline, Muda_Option_Argument_Empty },
    { StringLiteralExpand("help"), StringLiteralExpand("Muda description and list all the command"), OptHelp, Muda_Option_Argument_Optional },
};

static bool OptVersion(const char *program, const char *arg, Build_Config *config) {
    OsConsoleWrite("                        \n               _|   _,  \n/|/|/|  |  |  / |  / |  \n | | |_/ \\/|_/\\/|_/\\/|_/\n\n");
    OsConsoleWrite("Muda v %d.%d.%d\n\n", MUDA_VERSION_MAJOR, MUDA_VERSION_MINOR, MUDA_VERSION_PATCH);
    return true;
}

static bool OptDefault(const char *program, const char *arg, Build_Config *config) {
    OsConsoleWrite(" ___                             \n(|  \\  _ |\\  _,        |\\_|_  ,  \n |   ||/ |/ / |  |  |  |/ |  / \\_\n(\\__/ |_/|_/\\/|_/ \\/|_/|_/|_/ \\/ \n         |)                      \n");
    Compiler_Config def;
    CompilerConfigInit(&def);
    PushDefaultCompilerConfig(&def, Compiler_Kind_NULL);
    PrintCompilerConfig(def);
    OsConsoleWrite("\n");
    return true;
}

static bool OptSetup(const char *program, const char *arg, Build_Config *config) {
    OptVersion(program, arg, config);

    OsConsoleWrite("Muda Configuration:\n");

    Compiler_Config def;
    CompilerConfigInit(&def);
    PushDefaultCompilerConfig(&def, Compiler_Kind_NULL);

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

static bool OptOptimize(const char *program, const char *arg, Build_Config *config) {
    config->ForceOptimization = true;
    return false;
}

static bool OptCmdline(const char *program, const char *arg, Build_Config *config) {
    config->DisplayCommandLine = true;
    return false;
}

static bool OptHelp(const char *program, const char *arg, Build_Config *config) {
    if (arg) {
        String name = StringMake(arg, (Int64)strlen(arg));

        int command_index = -1;
        for (int opt_i = 0; opt_i < ArrayCount(Options); ++opt_i) {
            if (StrMatchCaseInsensitive(name, Options[opt_i].Name)) {
                command_index = opt_i;
                break;
            }
        }

        if (command_index != -1)
            OsConsoleWrite("   %s : %s\n\n", Options[command_index].Name.Data, Options[command_index].Desc.Data);
        else
            OsConsoleWrite("   \"%s\" command is not present!\n"
                "   Use: %s -help for list of all commands present.\n\n", arg, program);
    }
    else {
        // Dont judge me pls I was bored
        OptVersion(program, arg, config);
        OsConsoleWrite("Usage:\n\tpath/to/muda [-flags | build_script]\n\n");
        OsConsoleWrite("Flags:\n");

        for (int opt_i = 0; opt_i < ArrayCount(Options); ++opt_i) {
            OsConsoleWrite("\t-%-10s: %s\n", Options[opt_i].Name.Data, Options[opt_i].Desc.Data);
        }

        OsConsoleWrite("\nRepository:\n\thttps://github.com/IT-Club-Pulchowk/muda\n\n");
    }
    return true;
}

//
//
//

static bool HandleCommandLineArguments(int argc, char *argv[], Build_Config *config) {
    // If there are 2 arguments, then the 2nd argument could be options
    // Options are the strings that start with "-", we don't allow "- options", only allow "-option"
    if (argc > 1 && argv[1][0] == '-') {
        String option_name = StringMake(argv[1] + 1, strlen(argv[1] + 1));

        for (int opt_i = 0; opt_i < ArrayCount(Options); ++opt_i) {
            if (StrMatchCaseInsensitive(option_name, Options[opt_i].Name)) {
                const Muda_Option *const option = &Options[opt_i];

                switch (option->Argument) {
                case Muda_Option_Argument_Empty: {
                    if (argc == 2) {
                        return option->Proc(argv[0], NULL, config);
                    }
                    else {
                        Memory_Arena *scratch = ThreadScratchpad();
                        String error = FmtStr(scratch, "ERROR: %s does not take any argument!\n", option_name.Data);
                        FatalError(error.Data);
                    }
                } break;

                case Muda_Option_Argument_Needed: {
                    if (argc == 3) {
                        return option->Proc(argv[0], argv[2], config);
                    }
                    else {
                        Memory_Arena *scratch = ThreadScratchpad();
                        String error = FmtStr(scratch, "ERROR: %s requires single argument!\n", option_name.Data);
                        FatalError(error.Data);
                    }
                } break;

                case Muda_Option_Argument_Optional: {
                    if (argc <= 3) {
                        return option->Proc(argv[0], argc == 3 ? argv[2] : NULL, config);
                    }
                    else {
                        Memory_Arena *scratch = ThreadScratchpad();
                        String error = FmtStr(scratch, "ERROR: %s accepts at most one argument!\n", option_name.Data);
                        FatalError(error.Data);
                    }
                } break;
                }

                return false;
            }
        }

        LogError("ERROR: Unrecognized option: %s\n", option_name.Data);
        return true;
    }

    return false;
}

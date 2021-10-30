# Muda

* [Usage](#usage)
* [Compilation](#compilation)
* [Command Line](#command-line)
* [Muda File](#muda-file)
* [Plugin](#plugin)

## Opensource Credits
Credits to amosnier for their [single file sha-256 library](https://github.com/amosnier/sha-2).

## Usage
For direct executable downloads, head on to **_release_** directory, the executable for linux is **_muda_**, executable for windows x64 is **_muda.exe_** and the executable for windows x86 is **_x86/muda.exe_**.
For compilation from scratch, [click here](#compilation)

<br/><br/>

## Compilation

### Using the previous version of muda
- Run **_muda -noplug_** from the terminal. The binary will be in **_release_** directory. (replaces the default binary)

### Without using muda
Platform | Process 
------------ | -------------
Windows x64 | Run either **_build.bat optimize_** (cmd) or **_build.ps -optimize_** (powershell) from the root directory. The output will be in **_release/_** directory (replaces the default binary)
Windows x86 | Run **_build32.bat optimize_** (cmd) from the root directory. The output will be in **_release/x86_** directory (replaces the default binary)
Linux | Run the bash command **_build.sh optimize_** from the root directory. The output will be in **_release/_** directory (replaces the default binary)

<br/><br/>

## Command Line
All the commands with their description can be viewed by `muda -help` command line. For help with specific command `muda -help [command/s]` can be used. But here are the most frequently used commands.

Command | Usage | Description 
------------ | ------------- | -------------
cmdline | **_muda -cmdline_** | Displays the command line that was used to perform the build process.
compiler | **_muda -compiler <gcc\|clang\|cl>_** | Uses a specific compiler if available.
optimize | **_muda -optimize_** | Forces optimization to be turned on.

* Note: Several commands can be concatenated. For example: **_muda -cmdline -optimize -compiler clang_** displays command line, forces optimization and uses the CLANG compiler if available.

<br/><br/>

## Muda File
**Description:**<br/>
Muda scans for `build.muda` file in the directory it is run. The `build.muda` file is used to specific configurations for complex build options. For simple projects where there is only c files which needs to get compiled, running just muda without the `build.muda` file will used the default configuration. The default configuration can be viewed in the terminal using the command: `muda -default`. 
<br/>

**Usage:**<br/>
You can either handwrite the `build.muda` file yourself, or use the handy command line to generate the basic structure for your project. Use the `muda -setup` command to generate a basic muda file, or `muda -setup complete` command can be used for a complete setup for your project. Usually complete setup will not be required and `build.muda` file can be tweaked as necessary.
<br/>

**Working:**<br/>
Muda first searches for `build.muda` in the current directory, if not present it checks if the root Solution (will be explained shortly) configuration is present, if not present, searched for `build.muda` file in the user directory. If `build.muda` file is not present in the user directory, then it will create a default configuration and use that to build the source directory. If no source files are present in the current directory, it will output the default compiler that muda will use in the given environment and terminates.
<br/>

**Solution vs Project:**<br/>
The field in muda file `Kind` can have one of 2 values: `Project` and `Solution`. If it is not specified the default value of `Project` is used. The `Project` build kind specifies to search the current directory for the source files, compile them and produce the required binary file. The `Solution` build kind specifies to iterate all the directories present in the current directory and execute muda build in those directories. The configurations present in the Solution muda file will be used if the subdirectories does not have their own muda file. `ProjectDirectories` property can be used in the Solution muda file to specify the directory that is wanted to be iterated, or `IgnoredDirectories` can be used to specific the subdirectories that are to be ignored while iterating the subdirectories.

<br/><br/>

## Plugin
**Directory Structure:**<br/>
Muda is able to use single plugin. The plugin must be name `muda.dll` in windows and `muda.so` in linux and must be placed in `.muda/` directory. The directory could be as follows:

```bash
├── .muda	            # Muda plugin directory
│   ├── muda.dll        # Muda plugin for windows
│   ├── muda.so         # Muda plugin for linux
├── build.muda          # Solution muda build file
├── project_a           # Project A
│   ├── build.muda      # Project A muda build file
├── project_b           # Project B (Using the configuration from root build file)
```
<br/>

**Default Plugin:**<br/>
The default plugin is available in `.muda/` directory for both linux and windows. The default plugin check for the correctness and profiles memory usage and timings for all the builds generated by muda, the collected information is generated as a CSV file in the output directory (see the table below). The plugin can also be used extend the properties available in default muda build. The various properties supported by the default plugin is give in table below. To disable the use of plugins for the build **_muda -noplug_** command can be used.
The data is Dumped in Data folder. Which should contain a input.txt file with correct output values.

Property | Description 
------------ | -------------
ReportPath | Path where the generated CSV file is to be exported. (may end with a filename to rename the default output file name)
ProfileCommandLine | Command Line arguments to be sent to the process to be run
CorrectionCheck | Boolean value that tells whether to perform test for the process or not
CorrectOutput | Path to the file where the correct output is present.

<br/>

**Plugin API:**<br/>
To create your own plugin, you need to use the `plugin.h` header file present in `src/plugin.h`. Including this file, you need to implement the `MudaHandleEvent` macro and build a dyanmic library. The basic structure of plugin code is as follows:
```c
#include "muda/src/plugin.h"

MudaHandleEvent()
{
    if (Event->Kind == Muda_Plugin_Event_Kind_Detection)
    {
        MudaPluginName("MudaXPlugin"); // Set the name of your plugin
        return 0;
    }

    if (Event->Kind == Muda_Plugin_Event_Kind_Prebuild)
    {
        return 0;
    }

    if (Event->Kind == Muda_Plugin_Event_Kind_Parse)
    {
        // May be we are interested in some properties?
        return 1; // we return 1 because we are not handling any unknown properties
    }

    if (Event->Kind == Muda_Plugin_Event_Kind_Postbuild)
    {
        return 0;
    }

    if (Event->Kind == Muda_Plugin_Event_Kind_Destroy)
    {
        return 0;
    }

    return 1;
}
```

The various events in the plugin occurs at various muda execution phase which are explained as follows:

Event | Description 
------------ | -------------
Muda_Plugin_Event_Kind_Detection | Generated when the plugin is detected by muda build. If `-noplug` is flag is sent in the command line, plugin is never detected
Muda_Plugin_Event_Kind_Parse | Generated during parsing phase. If the parser detects an unknown property, it is sent to the plugin, if plugin returns 0, parser assumes that the property got consumed by the plugin
Muda_Plugin_Event_Kind_Prebuild | Generated after the execution Prebuild from the build.muda file and before executing the compilation process
Muda_Plugin_Event_Kind_Postbuild | Generated after the completion of compilation process
Muda_Plugin_Event_Kind_Destroy | Generated before the termination of the muda program

* Note: The `Muda_Plugin_Event_Kind_Prebuild` and `Muda_Plugin_Event_Kind_Postbuild` events are only send for Project Kind configurations

*Please view **src/plugin.h** for more detailed documentation on plugins*

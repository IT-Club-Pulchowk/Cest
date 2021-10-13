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

void LoadCompilerConfig(Compiler_Config *config, Uint8* data) {
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
        else FatalError("Error: Version info missing\n");
    }
    else FatalError("Error: Version tag missing at top of file\n");

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

    while (MudaParseNext(&prsr)) {
        // Temporary
        if (prsr.Token.Kind == Muda_Token_Tag){
            if (prsr.Token.Data.Tag.Value.Data)
                LogInfo("Tag:= %s : %s\n", prsr.Token.Data.Tag.Title.Data, prsr.Token.Data.Tag.Value.Data);
            else
                LogInfo("Tag:= %s\n", prsr.Token.Data.Tag.Title.Data);
        }
        if (prsr.Token.Kind != Muda_Token_Property) continue;

        if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("Type"))){
            if (StrMatch(prsr.Token.Data.Property.Value, StringLiteral("Project")))
                config->Type = Compile_Type_Project;
            else
                config->Type = Compile_Type_Solution;
        }

        else if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("Optimization")))
            config->Optimization = StrMatch(prsr.Token.Data.Property.Value, StringLiteral("true"));

        else if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("BuildDirectory")))
            config->BuildDirectory = StrDuplicate(prsr.Token.Data.Property.Value);

        else if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("Build")))
            config->Build = StrDuplicate(prsr.Token.Data.Property.Value);

        else if (StrMatch(prsr.Token.Data.Property.Key, StringLiteral("Source")))
            ReadList(&config->Source, prsr.Token.Data.Property.Value, -1);

        else {
            int index = CheckIfOptAvailable(prsr.Token.Data.Property.Key); 
            if (index == -1) {
                LogWarn ("Unrecognized option \"%s\" will be skipped\n", prsr.Token.Data.Property.Key.Data);
                continue;
            }

            OptListAdd(&config->Optionals, prsr.Token.Data.Property.Key, prsr.Token.Data.Property.Value, index);
        }
    }
    if (prsr.Token.Kind == Muda_Token_Error)
        LogError("%s at Line %d, Column %d\n", prsr.Token.Data.Error.Desc, prsr.Token.Data.Error.Line, prsr.Token.Data.Error.Column);
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

    LogInfo("Beginning compilation\n");

    if (config->BuildConfig.ForceCompiler) {
        if (compiler & config->BuildConfig.ForceCompiler) {
            compiler = config->BuildConfig.ForceCompiler;
            LogInfo("Requested compiler: %s\n", GetCompilerName(compiler));
        }
        else {
            const char *requested_compiler = GetCompilerName(config->BuildConfig.ForceCompiler);
            LogInfo("Requested compiler: %s but %s could not be detected\n",
                requested_compiler, requested_compiler);
        }
    }

	Assert(config->Type == Compile_Type_Project);

	Temporary_Memory temp = BeginTemporaryMemory(scratch);

    Memory_Allocator scratch_allocator = MemoryArenaAllocator(scratch);

	Out_Stream out;
	OutCreate(&out, scratch_allocator);
        
    Uint32 result = OsCheckIfPathExists(config->BuildDirectory);
    if (result == Path_Does_Not_Exist) {
        if (!OsCreateDirectoryRecursively(config->BuildDirectory)) {
            String error = FmtStr(scratch, "Failed to create directory %s!", config->BuildDirectory.Data);
            FatalError(error.Data);
        }
    }
    else if (result == Path_Exist_File) {
        String error = FmtStr(scratch, "%s: Path exist but is a file!\n", config->BuildDirectory.Data);
        FatalError(error.Data);
    }

    if (compiler & Compiler_Bit_CL) {
        // For CL, we output intermediate files to "BuildDirectory/int"
        String intermediate;
        if (config->BuildDirectory.Data[config->BuildDirectory.Length - 1] == '/') {
            intermediate = FmtStr(scratch, "%sint", config->BuildDirectory.Data);
        }
        else {
            intermediate = FmtStr(scratch, "%s/int", config->BuildDirectory.Data);
        }

        result = OsCheckIfPathExists(intermediate);
        if (result == Path_Does_Not_Exist) {
            if (!OsCreateDirectoryRecursively(intermediate)) {
                String error = FmtStr(scratch, "Failed to create directory %s!", intermediate.Data);
                FatalError(error.Data);
            }
        }
        else if (result == Path_Exist_File) {
            String error = FmtStr(scratch, "%s: Path exist but is a file!\n", intermediate.Data);
            FatalError(error.Data);
        }
    }

    // Turn on Optimization if it is forced via command line
    if (config->BuildConfig.ForceOptimization && !config->Optimization) {
        LogInfo("Optimization turned on forcefully\n");
        config->Optimization = true;
    }

    if (compiler & Compiler_Bit_CL) {
        LogInfo("Compiler: CL Detected\n");

        OutFormatted(&out, "cl -nologo -Zi -EHsc -W3 ");

        if (config->Optimization)
            OutFormatted(&out, "-O2 ");
        else
            OutFormatted(&out, "-Od ");

        for (String_List_Node* ntr = &config->Source.Head; ntr && config->Source.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->Source.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "\"%s\" ", ntr->Data[i].Data);
        }

        // TODO: Make directory if not present, need to add OS api for making directory!
        // Until then make "bin/int" directory manually :(
        OutFormatted(&out, "-Fo\"%s/int/\" ", config->BuildDirectory.Data);
        OutFormatted(&out, "-Fd\"%s/\" ", config->BuildDirectory.Data);
        OutFormatted(&out, "-link ");
        OutFormatted(&out, "-out:\"%s/%s.exe\" ", config->BuildDirectory.Data, config->Build.Data);
        OutFormatted(&out, "-pdb:\"%s/%s.pdb\" ", config->BuildDirectory.Data, config->Build.Data);

        for (Optionals_List_Node* ptr = &config->Optionals.Head; ptr && config->Optionals.Used; ptr = ptr->Next){
            int len = ptr->Next ? 8 : config->Optionals.Used;
            for (int i = 0; i < len; i ++){
                int indx = CheckIfOptAvailable(ptr->Property_Keys[i]);
                for (String_List_Node* ntr = &ptr->Property_Values[i].Head; ntr && ptr->Property_Values[i].Used; ntr = ntr->Next){
                    int len = ntr->Next ? 8 : ptr->Property_Values[i].Used;
                    for (int i = 0; i < len; i ++) OutFormatted(&out, Available_Properties[indx].Fmt_MSVC, ntr->Data[i].Data);
                }
            }
        }
    } else if (compiler & Compiler_Bit_GCC) {
        LogInfo("[Compiler] GCC Detected.\n");
        OutFormatted(&out, "gcc -pipe ");

        if (config->Optimization) OutFormatted(&out, "-O2 ");
        else OutFormatted(&out, "-g ");

        for (String_List_Node* ntr = &config->Source.Head; ntr && config->Source.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->Source.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "%s ", ntr->Data[i].Data);
        }

        if (PLATFORM_OS_LINUX)
            OutFormatted(&out, "-o %s/%s.out ", config->BuildDirectory.Data, config->Build.Data);
        else if (PLATFORM_OS_WINDOWS)
            OutFormatted(&out, "-o %s/%s.exe ", config->BuildDirectory.Data, config->Build.Data);

        for (Optionals_List_Node* ptr = &config->Optionals.Head; ptr && config->Optionals.Used; ptr = ptr->Next){
            int len = ptr->Next ? 8 : config->Optionals.Used;
            for (int i = 0; i < len; i ++){
                int indx = CheckIfOptAvailable(ptr->Property_Keys[i]);
                for (String_List_Node* ntr = &ptr->Property_Values[i].Head; ntr && ptr->Property_Values[i].Used; ntr = ntr->Next){
                    int len = ntr->Next ? 8 : ptr->Property_Values[i].Used;
                    for (int i = 0; i < len; i ++) OutFormatted(&out, Available_Properties[indx].Fmt_GCC, ntr->Data[i].Data);
                }
            }
        }

    } else if (compiler & Compiler_Bit_CLANG) {
        LogInfo("[Compiler] CLANG Detected.\n");
        OutFormatted(&out, "clang -gcodeview -w ");

        if (config->Optimization) OutFormatted(&out, "-O2 ");
        else OutFormatted(&out, "-g ");

        for (String_List_Node* ntr = &config->Source.Head; ntr && config->Source.Used; ntr = ntr->Next){
            int len = ntr->Next ? 8 : config->Source.Used;
            for (int i = 0; i < len; i ++) OutFormatted(&out, "%s ", ntr->Data[i].Data);
        }

        if (PLATFORM_OS_LINUX)
            OutFormatted(&out, "-o %s/%s.out ", config->BuildDirectory.Data, config->Build.Data);
        else if (PLATFORM_OS_WINDOWS)
            OutFormatted(&out, "-o %s/%s.exe ", config->BuildDirectory.Data, config->Build.Data);

        for (Optionals_List_Node* ptr = &config->Optionals.Head; ptr && config->Optionals.Used; ptr = ptr->Next){
            int len = ptr->Next ? 8 : config->Optionals.Used;
            for (int i = 0; i < len; i ++){
                int indx = CheckIfOptAvailable(ptr->Property_Keys[i]);
                for (String_List_Node* ntr = &ptr->Property_Values[i].Head; ntr && ptr->Property_Values[i].Used; ntr = ntr->Next){
                    int len = ntr->Next ? 8 : ptr->Property_Values[i].Used;
                    for (int i = 0; i < len; i ++) OutFormatted(&out, Available_Properties[indx].Fmt_CLANG, ntr->Data[i].Data);
                }
            }
        }
    }

    String cmd_line = OutBuildString(&out, &scratch_allocator);

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
    Memory_Arena arena = MemoryArenaCreate(MegaBytes(128));
    InitThreadContext(MemoryArenaAllocator(&arena), MegaBytes(512), 
        (Log_Agent){ .Procedure = LogProcedure }, FatalErrorProcedure);

    OsSetupConsole();
    
    Compiler_Config config;
    CompilerConfigInit(&config);

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
                LoadCompilerConfig(&config, buffer);
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

    PushDefaultCompilerConfig(&config, compiler);

    Compile(&config, compiler);

	return 0;
}

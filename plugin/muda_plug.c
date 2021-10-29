
#include "../src/plugin.h"
#include "../src/sha-256.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#define BUFSIZE 4096

typedef struct ProcessLaunchInfo
{
    struct
    {
        double   Millisecs;
        uint64_t Cycles;
    } Time;

    struct
    {
        uint64_t PageFaults;
        uint64_t PageMappedUsage;
        uint64_t PageFileUsage;
    } Memory;

    uint32_t Launched;
} ProcessLaunchInfo;

static void OsExecuteCommandLine(struct Thread_Context *Thread, Muda_Plugin_Interface *Interface,
                                 const char *CommandLine, ProcessLaunchInfo *InfoOut);

void        DumpOutput(Muda_Plugin_Config *Config);

BOOL        CheckOutput(Muda_Plugin_Config *Config);

int         DumpCSV(Muda_Plugin_Config *Config, ProcessLaunchInfo Info, int correctness);



MudaHandleEvent()
{
    if (Event->Kind == Muda_Plugin_Event_Kind_Detection)
    {
        MudaPluginName("MudaXPlugin");
        return 0;
    }

    if (Event->Kind == Muda_Plugin_Event_Kind_Prebuild || Event->Kind == Muda_Plugin_Event_Kind_Destroy)
        return 0;

    if (Event->Kind == Muda_Plugin_Event_Kind_Parse)
    {
        // May be we are interested in some properties?
        return 1; // we return 1 because we are not handling any unknown properties
    }

    if (Event->Kind == Muda_Plugin_Event_Kind_Postbuild)
    {
        MudaLog("==> Collecting information...\n");

        Muda_Plugin_Config *Config = MudaGetPostbuildData();

        if (Config->BuildKind == BUILD_KIND_EXECUTABLE)
        {
            if (Config->Succeeded)
            {
                MudaLog("Launching Process: %s\n", Config->MudaDir);

                char CommandLine[256];
                snprintf(CommandLine, sizeof(CommandLine), "%s/%s.%s", Config->BuildDir, Config->Build,
                         Config->BuildExtension);

                ProcessLaunchInfo Info;
                OsExecuteCommandLine(Thread, Interface, CommandLine, &Info);

                //
                // TODO: Write information to file
                //
                FILE *out = stdout;
                if (Info.Launched)
                {
                    fprintf(out, "\n\n===================================\n");
                    fprintf(out, "Launched: true\n");
                    fprintf(out, "Memory:\n");
                    fprintf(out, "\tPage Faults:%zu\n", Info.Memory.PageFaults);
                    fprintf(out, "\tPeak Mapped Memory:%f KB\n", (double)Info.Memory.PageMappedUsage / 1024.0);
                    fprintf(out, "\tPeak Page Memory:%f KB\n", (double)Info.Memory.PageFileUsage / 1024.0);
                    fprintf(out, "Time:\n");
                    fprintf(out, "\tCPU Cycles:%f K\n", (double)Info.Time.Cycles / 1000.0);
                    fprintf(out, "\tTotal Time:%f ms\n", Info.Time.Millisecs);
                    fprintf(out, "===================================\n");
                }





                /// here is code to dump output
                DumpOutput(Config);

                
             

                // code to check if it was correct
                int same = CheckOutput(Config);
                if (same == 0)
                    printf("Sorry! Your Code failed to Produce Desired Output!\n\n");

                printf("Yay! Code Workedd.\n\n");





                // log into muda.csv
                DumpCSV(Config, Info, same);
            }
        }

        MudaLog("==> Finished Collecting information.\n\n");
        return 0;
    }

    return 1;
}

#if (PLATFORM_OS_WINDOWS == 1)
#include <Windows.h>
#include <psapi.h>

static wchar_t *UnicodeToWideChar(Muda_Plugin_Interface *Interface, struct Memory_Arena *Arena, const char *String)
{
    int      WideLength    = MultiByteToWideChar(CP_UTF8, 0, String, -1, NULL, 0);
    wchar_t *WideString    = Interface->PushSize(Arena, (WideLength + 1) * sizeof(wchar_t));

    WideLength             = MultiByteToWideChar(CP_UTF8, 0, String, -1, WideString, WideLength);
    WideString[WideLength] = 0;

    return WideString;
}

static void OsExecuteCommandLine(struct Thread_Context *Thread, Muda_Plugin_Interface *Interface,
                                 const char *CommandLine, ProcessLaunchInfo *InfoOut)
{
    memset(InfoOut, 0, sizeof(*InfoOut));

    struct Memory_Arena *Scratch         = Interface->GetThreadScratchpad(Thread);

    Temporary_Memory     temp            = Interface->BeginTemporaryMemory(Scratch);

    wchar_t             *WideCommandLine = UnicodeToWideChar(Interface, Scratch, CommandLine);

    STARTUPINFOW         Startup         = {sizeof(Startup)};
    PROCESS_INFORMATION  ProcessInfo;
    memset(&ProcessInfo, 0, sizeof(ProcessInfo));

    if (CreateProcessW(NULL, WideCommandLine, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL, NULL, &Startup,
                       &ProcessInfo))
    {

        WaitForSingleObject(ProcessInfo.hProcess, INFINITE);

        DWORD ExitCode;
        GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);

        PROCESS_MEMORY_COUNTERS ProcessMemCounters;
        memset(&ProcessMemCounters, 0, sizeof(ProcessMemCounters));
        ProcessMemCounters.cb = sizeof(ProcessMemCounters);
        GetProcessMemoryInfo(ProcessInfo.hProcess, &ProcessMemCounters, sizeof(ProcessMemCounters));

        InfoOut->Memory.PageFaults      = ProcessMemCounters.PageFaultCount;
        InfoOut->Memory.PageMappedUsage = ProcessMemCounters.PeakWorkingSetSize;
        InfoOut->Memory.PageFileUsage   = ProcessMemCounters.PeakPagefileUsage;

        ULONG64 ProcessClockCycles      = 0;
        QueryProcessCycleTime(ProcessInfo.hProcess, &ProcessClockCycles);

        FILETIME CreationTime, ExitTime, KernelTime, UserTime;
        memset(&CreationTime, 0, sizeof(CreationTime));
        memset(&ExitTime, 0, sizeof(ExitTime));
        memset(&KernelTime, 0, sizeof(KernelTime));
        memset(&UserTime, 0, sizeof(UserTime));

        GetProcessTimes(ProcessInfo.hProcess, &CreationTime, &ExitTime, &KernelTime, &UserTime);

        InfoOut->Time.Millisecs =
            (((LARGE_INTEGER *)&ExitTime)->QuadPart - ((LARGE_INTEGER *)&CreationTime)->QuadPart) / 10000.0;

        InfoOut->Time.Cycles = ProcessClockCycles;

        InfoOut->Launched    = 1;

        CloseHandle(ProcessInfo.hProcess);
        CloseHandle(ProcessInfo.hThread);
    }

    Interface->EndTemporaryMemory(&temp);
}

void DumpOutput(Muda_Plugin_Config *Config)
{
    char assignment_file_name[256];
    snprintf(assignment_file_name, 256, "%s/%s.%s", Config->BuildDir, "output", "txt");

    char CommandLine[256];
    snprintf(CommandLine, sizeof(CommandLine), "%s/%s.%s", Config->BuildDir, Config->Build, Config->BuildExtension);

    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;

    HANDLE              h = CreateFileA(assignment_file_name, FILE_APPEND_DATA, FILE_SHARE_WRITE | FILE_SHARE_READ, &sa,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    PROCESS_INFORMATION pi;
    STARTUPINFO         si;
    BOOL                ret = FALSE;

    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput  = NULL;
    si.hStdError  = h;
    si.hStdOutput = h;

    ret           = CreateProcessA(NULL, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);

    if (!ret)
    {
        printf("Could Not Open a process.\nReturning at line %d\n\n", __LINE__);
        CloseHandle(h);
        return;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(h);
}

BOOL CheckOutput(Muda_Plugin_Config *Config)
{
    // assume .muda/input.txt and .muda/muda.txt
    // input.txt has correct values
    // output.txt has assigned program output
    FILE *input_file;
    FILE *output_file;

    char  input_file_name[256];
    snprintf(input_file_name, 256, "%s/%s.%s", Config->BuildDir, "input", "txt");

    char assignment_file_name[256];
    snprintf(assignment_file_name, 256, "%s/%s.%s", Config->BuildDir, "output", "txt");

    errno_t file_err = fopen_s(&input_file, input_file_name, "r");
    if (input_file == 0)
    {
        printf("Could Not Open %s.\nReturning at line %d\n\n", input_file_name, __LINE__);
        return -1;
    }
    file_err = fopen_s(&output_file, assignment_file_name, "r");
    if (output_file == 0)
    {
        printf("Could Not Open %s.\nReturning at line %d\n\n", assignment_file_name, __LINE__);
        return -1;
    }

    // a big enough number
    // apparently this is the line limit for bash input
    // output maybe higher but I doubt we will ever hit a line this long
    // so it is
    int     line_size   = 4096;
    char   *input_line  = malloc(line_size * sizeof(char));
    char   *output_line = malloc(line_size * sizeof(char));
    int     same        = 1;

    uint8_t input_hash[32];
    uint8_t output_hash[32];

    while (same && fgets(input_line, line_size, input_file) && fgets(output_line, line_size, output_file))
    {
        calc_sha_256(input_hash, (void *)input_line, strlen(input_line));
        calc_sha_256(output_hash, (void *)output_line, strlen(output_line));

        for (int i = 0; i < 32; ++i)
        {
            if (input_hash[i] != output_hash[i])
                same = 0;
        }
    }

    free(input_line);
    free(output_line);
    fclose(input_file);
    fclose(output_file);

    return same;
}

int DumpCSV(Muda_Plugin_Config *Config, ProcessLaunchInfo Info, int correctness)
{
    char log_file_name[256];
    snprintf(log_file_name, 256, "%s/%s.%s", Config->BuildDir, Config->Build, "csv");
    FILE   *log_file;
    errno_t file_err = fopen_s(&log_file, log_file_name, "a");

    if (log_file == 0)
    {
        printf("Could Not open %s.\nReturning at line %d\n\n", log_file_name, __LINE__);
        return -1;
    }
    char log_entry[256];
    snprintf(log_entry, 256, "%d,\"%s\",%zu,%f,%f,%f,%f\n", correctness, Config->MudaDir, Info.Memory.PageFaults,
             (double)Info.Memory.PageMappedUsage / 1024.0, (double)Info.Memory.PageFileUsage / 1024.0,
             (double)Info.Time.Cycles / 1000.0, Info.Time.Millisecs);

    fputs(log_entry, log_file);
    return 0;
}

#endif

#if (PLATFORM_OS_LINUX == 1)
static void OsExecuteCommandLine(struct Thread_Context *Thread, Muda_Plugin_Interface *Interface,
                                 *(8888883y3o3 #        )
                                 const char *CommandLine, ProcessLaunchInfo *InfoOut)
{
    Unimplemented();
}

void DumpOutput(Muda_Plugin_Config *Config)
{
    Unimplemented();
}

BOOL CheckOutput(Muda_Plugin_Config *Config)
{
    Unimplemented();
}

int DumpCSV(Muda_Plugin_Config *Config, ProcessLaunchInfo Info, int correctness)
{
    Unimplemented();
}
#endif


#include "../src/sha-256.c"
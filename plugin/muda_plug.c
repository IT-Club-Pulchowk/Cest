
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





static void OsExecuteCommandLine(struct Thread_Context *Thread, Muda_Plugin_Interface *Interface,
                                 const char *CommandLine, ProcessLaunchInfo *InfoOut);
void        DumpOutput(Muda_Plugin_Config *Config);
BOOL        CheckOutput(Muda_Plugin_Config *Config);
int         DumpCSV(Muda_Plugin_Config *Config, ProcessLaunchInfo* Info, int correctness);


FILE       *input_file;
wchar_t     input_file_name[256];

WCHAR       log_file_name[256];
FILE       *log_file;




MudaHandleEvent()
{
    if (Event->Kind == Muda_Plugin_Event_Kind_Detection)
    {
        MudaPluginName("MudaXPlugin");
    
        // File with standard answers
        _snwprintf_s(input_file_name, 256, _TRUNCATE, L"%hs/%hs.%hs", "Data", "input", "txt");
        _wfopen_s(&input_file, input_file_name, L"r");

        if (input_file == 0)
        {
            printf("Could Not Open %ws.\nReturning at line %d\n\n", input_file_name, __LINE__);
            return -1;
        }

        // The csv file
        _snwprintf_s(log_file_name, 256, _TRUNCATE, L"%hs/%hs.%hs", "Data", "muda", "csv");
        _wfopen_s(&log_file, log_file_name, L"a");

        if (log_file == 0)
        {
            printf("Could Not open %ws.\nReturning at line %d\n\n", log_file_name, __LINE__);
            return -1;
        }

        return 0;
    }

    if (Event->Kind == Muda_Plugin_Event_Kind_Prebuild || Event->Kind == Muda_Plugin_Event_Kind_Destroy)
        return 0;

    if (Event->Kind == Muda_Plugin_Event_Kind_Parse)
    {
        Event->Data.Parse
        // May be we are interested in some properties?
        return 1; // we return 1 because we are not handling any unknown properties
    }

    if (Event->Kind == Muda_Plugin_Event_Kind_Postbuild)
    {
        MudaLog("==> Collecting information...\n");

        Muda_Plugin_Config *Config = MudaGetPostbuildData();

        if (!Config->RootBuild)
        {
            if (Config->Succeeded)
            {
                MudaLog("Launching Process: %s\n", Config->MudaDirName);

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
                    fprintf(out, "===================================\n");
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

                //
                // TODO: Verify the program correctness
                //

                
                /// here is code to dump output
                DumpOutput(Config);


                // code to check if it was correct
                int same = CheckOutput(Config);
                if (same == 0)
                    printf("Sorry! Your Code failed to Produce Desired Output!\n\n");
                else if (same == -1)
                    return -1; // returns one because failed to start process. In this case dont log.
                else
                    printf("Yay! Code Workedd.\n\n");


                // log into muda.csv
                DumpCSV(Config, &Info, same);

            }

            //
            // TODO: Write information to file
            //
        }

        MudaLog("==> Finished Collecting information.\n");

        return 0;
    }

    if (Event->Kind == Muda_Plugin_Event_Kind_Destroy)
    {
        fclose(input_file);
        fclose(log_file);
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
    wchar_t assignment_file_name[256];
    _snwprintf_s(assignment_file_name, 256, _TRUNCATE, L"%hs/%hs.%hs", Config->BuildDir, "output", "txt");

    wchar_t CommandLine[256];
    ZeroMemory(CommandLine, 256);
    _snwprintf_s(CommandLine, 256, _TRUNCATE, L"%hs/%hs.%hs", Config->BuildDir, Config->Build, Config->BuildExtension);

    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;

    HANDLE              h = CreateFileW(assignment_file_name, FILE_APPEND_DATA, FILE_SHARE_WRITE | FILE_SHARE_READ, &sa,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    PROCESS_INFORMATION pi;
    STARTUPINFOW        si;
    BOOL                ret = FALSE;

    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput  = NULL;
    si.hStdError  = h;
    si.hStdOutput = h;

    ret           = CreateProcessW(NULL, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);

    if (!ret)
    {
        char error[1024];
        strerror_s(error, 1024, GetLastError());
        printf("Could Not Open a process.\nError: %s\nReturning at line %d\n\n", error, __LINE__);
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

    wchar_t assignment_file_name[256];
    _snwprintf_s(assignment_file_name, 256, _TRUNCATE, L"%hs/%hs.%hs", Config->BuildDir, "output", "txt");

    FILE *output_file;
    _wfopen_s(&output_file, assignment_file_name, L"r");
    if (output_file == 0)
    {
        printf("Could Not Open %ws.\nReturning at line %d\n\n", assignment_file_name, __LINE__);
        return -1;
    }

    // a big enough number
    int     line_size   = 4096;
    WCHAR  *input_line  = malloc(line_size * sizeof(char));
    WCHAR  *output_line = malloc(line_size * sizeof(char));
    int     same        = 1;

    uint8_t input_hash[32];
    uint8_t output_hash[32];

    while (same)
    {
        fgetws(input_line, line_size, input_file);
        fgetws(output_line, line_size, output_file);
        
        if (feof(input_file)) // fuck i am so dumb
            break;
        if (feof(output_file))
            break;

        calc_sha_256(input_hash, (void *)input_line, wcslen(input_line));
        calc_sha_256(output_hash, (void *)output_line, wcslen(output_line));

        for (int i = 0; i < 32; ++i)
        {
            if (input_hash[i] != output_hash[i])
            {
                same = 0; // this also breaks the outer while
                break;
            }
        }
    }

    free(input_line);
    free(output_line);
    fclose(output_file);

    return same;
}

int DumpCSV(Muda_Plugin_Config *Config, ProcessLaunchInfo* Info, int correctness)
{
    WCHAR log_entry[256];
    _snwprintf_s(log_entry, 256, _TRUNCATE, L"%d,\"%hs\",%zu,%f,%f,%f,%f\n", correctness, Config->MudaDirName,
                 Info->Memory.PageFaults, (double)Info->Memory.PageMappedUsage / 1024.0,
                 (double)Info->Memory.PageFileUsage / 1024.0, (double)Info->Time.Cycles / 1000.0, Info->Time.Millisecs);

    fputws(log_entry, log_file);
    return 0;
}

#endif
#if (PLATFORM_OS_LINUX == 1)
static void OsExecuteCommandLine(struct Thread_Context *Thread, Muda_Plugin_Interface *Interface,
                                 const char *CommandLine, ProcessLaunchInfo *InfoOut)
{
    Unimplemented();
}
#endif

#include "../src/sha-256.c"
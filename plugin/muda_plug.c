
#include "../src/plugin.h"
#include <string.h>
#include <stdio.h>

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
            }

            //
            // TODO: Write information to file
            //
        }

        MudaLog("==> Finished Collecting information.\n");
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

#endif
#if (PLATFORM_OS_LINUX == 1)
static void OsExecuteCommandLine(struct Thread_Context *Thread, Muda_Plugin_Interface *Interface,
                                 const char *CommandLine, ProcessLaunchInfo *InfoOut)
{
    Unimplemented();
}
#endif

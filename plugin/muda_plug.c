
#include "../src/plugin.h"
#include <string.h>
#include <stdio.h>

typedef struct ProcessLaunchInfo {
	struct {
		uint64_t Kernel;
		uint64_t User;
		uint64_t Exit;
		uint64_t Cycles;
	} Time;

	struct {
		uint64_t PageFaults;
		uint64_t PageMappedUsage;
		uint64_t PageFileUsage;
	} Memory;

	uint32_t Launched;
} ProcessLaunchInfo;

static void OsExecuteCommandLine(struct Thread_Context *Thread, Muda_Plugin_Interface *Interface,
	const char *CommandLine, ProcessLaunchInfo *InfoOut);

MUDA_PLUGIN_INTERFACE
void MudaEventHook(struct Thread_Context *Thread, Muda_Plugin_Interface *Interface, Muda_Plugin_Event_Kind Event, const Muda_Plugin_Config *Config) {
	switch (Event) {
	case Muda_Plugin_Event_Kind_Detection: {
		Interface->PluginName = "MudaXPlugin";
	} break;

	case Muda_Plugin_Event_Kind_Prebuild: {
	} break;

	case Muda_Plugin_Event_Kind_Postbuild: {
		Interface->LogInfo(Thread, "==> Collecting information...\n");

		if (Config->BuildKind == BUILD_KIND_EXECUTABLE) {
			if (Config->Succeeded) {
				Interface->LogInfo(Thread, "Launching Process: %s\n", Config->MudaDir);

				char CommandLine[256];
				snprintf(CommandLine, sizeof(CommandLine), "%s/%s.%s", Config->BuildDir, Config->Build, Config->BuildExtension);

				ProcessLaunchInfo Info;
				OsExecuteCommandLine(Thread, Interface, CommandLine, &Info);

				//
				// TODO: Write information to file
				//
				FILE *out = stdout;
				if (Info.Launched) {
					fprintf(out, "===================================\n");
					fprintf(out, "Launched: true\n");
					fprintf(out, "Memory:\n");
					fprintf(out, "\tPage Faults:%zu\n", Info.Memory.PageFaults);
					fprintf(out, "\tPeak Mapped Memory:%f KB\n", (double)Info.Memory.PageMappedUsage / 1024.0);
					fprintf(out, "\tPeak Page Memory:%f KB\n", (double)Info.Memory.PageFileUsage / 1024.0);
					fprintf(out, "Time:\n");
					fprintf(out, "\tCPU Cycles:%f K\n", (double)Info.Time.Cycles / 1000.0);
					fprintf(out, "\tTotal Time:%f ms\n", (double)(Info.Time.Kernel + Info.Time.User + Info.Time.Exit) / 1000.0);
					fprintf(out, "===================================\n");
				}
			}

			//
			// TODO: Write information to file
			//
		}

		Interface->LogInfo(Thread, "==> Finished Collecting information.\n");
	} break;

	case Muda_Plugin_Event_Kind_Destroy: {
	} break;
	}
}

#ifdef PLATFORM_OS_WINDOWS
#include <Windows.h>
#include <psapi.h>

static wchar_t *UnicodeToWideChar(Muda_Plugin_Interface *Interface, struct Memory_Arena *Arena, const char *String) {
	int WideLength = MultiByteToWideChar(CP_UTF8, 0, String, -1, NULL, 0);
	wchar_t *WideString = Interface->PushSize(Arena, (WideLength + 1) * sizeof(wchar_t));

	WideLength = MultiByteToWideChar(CP_UTF8, 0, String, -1, WideString, WideLength);
	WideString[WideLength] = 0;

	return WideString;
}


static void OsExecuteCommandLine(struct Thread_Context *Thread, Muda_Plugin_Interface *Interface,
	const char *CommandLine, ProcessLaunchInfo *InfoOut) {
	memset(InfoOut, 0, sizeof(*InfoOut));

	struct Memory_Arena *Scratch = Interface->GetThreadScratchpad(Thread);

	Temporary_Memory temp = Interface->BeginTemporaryMemory(Scratch);

	wchar_t *WideCommandLine = UnicodeToWideChar(Interface, Scratch, CommandLine);

	STARTUPINFOW Startup = { sizeof(Startup) };
	PROCESS_INFORMATION ProcessInfo;
	memset(&ProcessInfo, 0, sizeof(ProcessInfo));

	if (CreateProcessW(NULL, WideCommandLine, NULL, NULL, FALSE,
		NORMAL_PRIORITY_CLASS, NULL, NULL, &Startup, &ProcessInfo)) {

		WaitForSingleObject(ProcessInfo.hProcess, INFINITE);

		DWORD ExitCode;
		GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);

		PROCESS_MEMORY_COUNTERS ProcessMemCounters;
		memset(&ProcessMemCounters, 0, sizeof(ProcessMemCounters));
		ProcessMemCounters.cb = sizeof(ProcessMemCounters);
		GetProcessMemoryInfo(ProcessInfo.hProcess, &ProcessMemCounters, sizeof(ProcessMemCounters));

		InfoOut->Memory.PageFaults = ProcessMemCounters.PageFaultCount;
		InfoOut->Memory.PageMappedUsage = ProcessMemCounters.PeakWorkingSetSize;
		InfoOut->Memory.PageFileUsage = ProcessMemCounters.PeakPagefileUsage;

		ULONG64 ProcessClockCycles = 0;
		QueryProcessCycleTime(ProcessInfo.hProcess, &ProcessClockCycles);

		FILETIME CreationTime, ExitTime, KernelTime, UserTime;
		memset(&CreationTime, 0, sizeof(CreationTime));
		memset(&ExitTime, 0, sizeof(ExitTime));
		memset(&KernelTime, 0, sizeof(KernelTime));
		memset(&UserTime, 0, sizeof(UserTime));

		GetProcessTimes(ProcessInfo.hProcess, &CreationTime, &ExitTime, &KernelTime, &UserTime);

		InfoOut->Time.Kernel = ((LARGE_INTEGER *)&KernelTime)->QuadPart;
		InfoOut->Time.User = ((LARGE_INTEGER *)&UserTime)->QuadPart;
		InfoOut->Time.Exit = ((LARGE_INTEGER *)&ExitTime)->QuadPart;

		InfoOut->Time.Cycles = ProcessClockCycles;

		InfoOut->Launched = 1;

		CloseHandle(ProcessInfo.hProcess);
		CloseHandle(ProcessInfo.hThread);
	}

	Interface->EndTemporaryMemory(&temp);
}

#endif

#include <stddef.h>
#include <stdint.h>

#if defined(__clang__) || defined(__ibmxl__)
#define COMPILER_CLANG 1
#elif defined(_MSC_VER)
#define COMPILER_MSVC 1
#elif defined(__GNUC__)
#define COMPILER_GCC 1
#elif defined(__MINGW32__) || defined(__MINGW64__)
#define COMPILER_MINGW 1
#elif defined(__INTEL_COMPILER)
#define COMPILER_INTEL 1
#else
#error Missing Compiler detection
#endif

#if !defined(COMPILER_CLANG)
#define COMPILER_CLANG 0
#endif
#if !defined(COMPILER_MSVC)
#define COMPILER_MSVC 0
#endif
#if !defined(COMPILER_GCC)
#define COMPILER_GCC 0
#endif
#if !defined(COMPILER_INTEL)
#define COMPILER_INTEL 0
#endif

#if defined(__ANDROID__) || defined(__ANDROID_API__)
#define OS_ANDROID 1
#elif defined(__gnu_linux__) || defined(__linux__) || defined(linux) || defined(__linux)
#define PLATFORM_OS_LINUX 1
#elif defined(macintosh) || defined(Macintosh)
#define PLATFORM_OS_MAC 1
#elif defined(__APPLE__) && defined(__MACH__)
#defined PLATFORM_OS_MAC 1
#elif defined(__APPLE__)
#define PLATFORM_OS_IOS 1
#elif defined(_WIN64) || defined(_WIN32)
#define PLATFORM_OS_WINDOWS 1
#else
#error Missing Operating System Detection
#endif

#if !defined(PLATFORM_OS_ANDRIOD)
#define PLATFORM_OS_ANDRIOD 0
#endif
#if !defined(PLATFORM_OS_LINUX)
#define PLATFORM_OS_LINUX 0
#endif
#if !defined(PLATFORM_OS_MAC)
#define PLATFORM_OS_MAC 0
#endif
#if !defined(PLATFORM_OS_IOS)
#define PLATFORM_OS_IOS 0
#endif
#if !defined(PLATFORM_OS_WINDOWS)
#define PLATFORM_OS_WINDOWS 0
#endif

#ifndef MUDA_PLUGIN_IMPORT_INCLUDE
#if PLATFORM_OS_WINDOWS == 1
#define MUDA_PLUGIN_INTERFACE __declspec(dllexport)
#else
#define MUDA_PLUGIN_INTERFACE 
#endif

struct Memory_Arena;
struct Thread_Context;

typedef struct Temporary_Memory {
	struct Memory_Arena *Arena;
	size_t Position;
} Temporary_Memory;

#else
#define MUDA_PLUGIN_INTERFACE 
#endif

#define BUILD_KIND_EXECUTABLE		0
#define BUILD_KIND_STATIC_LIBRARY	1
#define BUILD_KIND_DYNAMIC_LIBRARY	2

typedef enum Muda_Plugin_Event_Kind {
	Muda_Plugin_Event_Kind_Detection,
	Muda_Plugin_Event_Kind_Prebuild,
	Muda_Plugin_Event_Kind_Postbuild,
	Muda_Plugin_Event_Kind_Destroy
} Muda_Plugin_Event_Kind;

typedef struct Muda_Plugin_Config {
	const char *Name;
	const char *Build;
	const char *BuildExtension;
	const char *BuildDir;
	const char *MudaDir;
	uint32_t	BuildKind;
	uint32_t	Succeeded;
} Muda_Plugin_Config;

typedef struct Muda_Plugin_Interface {
	struct Memory_Arena *(*GetThreadScratchpad)(struct Thread_Context *);
	void *(*PushSize)(struct Memory_Arena *, uint32_t);
	void *(*PushSizeAligned)(struct Memory_Arena *, uint32_t, uint32_t);
	Temporary_Memory(*BeginTemporaryMemory)(struct Memory_Arena *);
	void (*EndTemporaryMemory)(Temporary_Memory *);

	void (*LogInfo)(struct Thread_Context *, const char *fmt, ...);
	void (*LogError)(struct Thread_Context *, const char *fmt, ...);
	void (*LogWarn)(struct Thread_Context *, const char *fmt, ...);
	void (*FatalError)(struct Thread_Context *, const char *msg);

	char *PluginName;
	void *UserContext;

	struct {
		uint32_t Major, Minor, Patch;
	} Version;
} Muda_Plugin_Interface;

typedef void (*Muda_Event_Hook_Procedure)(struct Thread_Context *Thread, Muda_Plugin_Interface *Interface, Muda_Plugin_Event_Kind Event, const Muda_Plugin_Config *Config);

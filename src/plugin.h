#include <stddef.h>
#include <stdint.h>

#ifndef MUDA_PLUGIN_IMPORT_INCLUDE
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

#if PLATFORM_OS_WINDOWS == 1
#ifdef __cplusplus
#define MUDA_PLUGIN_INTERFACE __declspec(dllexport) extern "C"
#else
#define MUDA_PLUGIN_INTERFACE __declspec(dllexport)
#endif
#else
#if defined(__cplusplus)
#define MUDA_PLUGIN_INTERFACE extern "C"
#else
#define MUDA_PLUGIN_INTERFACE
#endif
#endif

#if defined(__GNUC__)
#define __PROCEDURE__ __FUNCTION__
#elif defined(__DMC__) && (__DMC__ >= 0x810)
#define __PROCEDURE__ __PRETTY_PROCEDURE__
#elif defined(__FUNCSIG__)
#define __PROCEDURE__ __FUNCSIG__
#elif (defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 600)) || (defined(__IBMCPP__) && (__IBMCPP__ >= 500))
#define __PROCEDURE__ __PROCEDURE__
#elif defined(__BORLANDC__) && (__BORLANDC__ >= 0x550)
#define __PROCEDURE__ __FUNC__
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901)
#define __PROCEDURE__ __func__
#elif defined(__cplusplus) && (__cplusplus >= 201103)
#define __PROCEDURE__ __func__
#elif defined(_MSC_VER)
#define __PROCEDURE__ __FUNCSIG__
#else
#define __PROCEDURE__ "_unknown_"
#endif

#if defined(HAVE_SIGNAL_H) && !defined(__WATCOMC__)
#include <signal.h> // raise()
#endif

#if defined(_MSC_VER)
#define TriggerBreakpoint() __debugbreak()
#elif ((!defined(__NACL__)) &&                                                                                         \
       ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))))
#define TriggerBreakpoint() __asm__ __volatile__("int $3\n\t")
#elif defined(__386__) && defined(__WATCOMC__)
#define TriggerBreakpoint() _asm { int 0x03}
#elif defined(HAVE_SIGNAL_H) && !defined(__WATCOMC__)
#define TriggerBreakpoint() raise(SIGTRAP)
#else
#define TriggerBreakpoint() ((int *)0) = 0
#endif

#if defined(COMPILER_GCC)
#define INLINE_PROCEDURE static inline
#else
#define INLINE_PROCEDURE inline
#endif

#if !defined(BUILD_DEBUG) && !defined(BUILD_DEVELOPER) && !defined(BUILD_RELEASE)
#if defined(_DEBUG) || defined(DEBUG)
#define BUILD_DEBUG
#elif defined(NDEBUG)
#define BUILD_RELEASE
#else
#define BUILD_DEBUG
#endif
#endif

#if !defined(ASSERTION_HANDLED)
#define AssertHandle(reason, file, line, proc) TriggerBreakpoint()
#else
void AssertHandle(const char *reason, const char *file, int line, const char *proc);
#endif

#if defined(BUILD_DEBUG) || defined(BUILD_DEVELOPER)
#define DebugTriggerbreakpoint TriggerBreakpoint
#define Assert(x)                                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
            AssertHandle("Assert Failed", __FILE__, __LINE__, __PROCEDURE__);                                          \
    } while (0)
#else
#define DebugTriggerbreakpoint()
#define Assert(x)                                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        0;                                                                                                             \
    } while (0)
#endif

#if defined(BUILD_DEBUG) || defined(BUILD_DEVELOPER)
#define Unimplemented() AssertHandle("Unimplemented procedure", __FILE__, __LINE__, __PROCEDURE__);
#define Unreachable() AssertHandle("Unreachable code path", __FILE__, __LINE__, __PROCEDURE__);
#define NoDefaultCase()                                                                                                \
    default:                                                                                                           \
        AssertHandle("No default case", __FILE__, __LINE__, __PROCEDURE__);                                            \
        break
#else
#define Unimplemented() TriggerBreakpoint();
#define Unreachable() TriggerBreakpoint();
#define NoDefaultCase()                                                                                                \
    default:                                                                                                           \
        TriggerBreakpoint();                                                                                           \
        break
#endif

struct Memory_Arena;
struct Thread_Context;

typedef struct Temporary_Memory
{
    struct Memory_Arena *Arena;
    size_t               Position;
} Temporary_Memory;

#define MUDA_PLUGIN_VERSION_MAJOR 1
#define MUDA_PLUGIN_VERSION_MINOR 9
#define MUDA_PLUGIN_VERSION_PATCH 0

#else
#define MUDA_PLUGIN_INTERFACE
#endif

#define BUILD_KIND_EXECUTABLE 0
#define BUILD_KIND_STATIC_LIBRARY 1
#define BUILD_KIND_DYNAMIC_LIBRARY 2

typedef enum Muda_Parsing_OS
{
    Muda_Parsing_OS_All,
    Muda_Parsing_OS_Windows,
    Muda_Parsing_OS_Linux,
    Muda_Parsing_OS_Mac,
} Muda_Parsing_OS;

typedef enum Muda_Parsing_COMPILER
{
    Muda_Parsing_COMPILER_ALL,
    Muda_Parsing_COMPILER_CL,
    Muda_Parsing_COMPILER_CLANG,
    Muda_Parsing_COMPILER_GCC,
} Muda_Parsing_COMPILER;

typedef struct Muda_Parse_Section
{
    Muda_Parsing_OS       OS;
    Muda_Parsing_COMPILER Compiler;
} Muda_Parse_Section;

// ALERT: This must be synced with String
typedef struct Muda_String
{
    int64_t Length;
    char   *Data;
} Muda_String;

typedef struct Muda_Parser_Token
{
    const char        *MudaDirName;
    const char        *ConfigName;
    Muda_Parse_Section Section;
    Muda_String        Key;
    Muda_String       *Values;
    uint32_t           ValueCount;
} Muda_Parser_Token;

typedef enum Muda_Plugin_Event_Kind
{
    Muda_Plugin_Event_Kind_Detection,
    Muda_Plugin_Event_Kind_Parse,
    Muda_Plugin_Event_Kind_Prebuild,
    Muda_Plugin_Event_Kind_Postbuild,
    Muda_Plugin_Event_Kind_Destroy
} Muda_Plugin_Event_Kind;

typedef enum Command_Line_Flags
{
    Command_Line_Flag_Force_Optimization = 0x1,
    Command_Line_Flag_Display_Command_Line = 0x2,
    Command_Line_Flag_Disable_Logs = 0x4,
} Command_Line_Flags;

typedef struct Command_Line_Config
{
    Muda_Parsing_COMPILER ForceCompiler;
    Command_Line_Flags    Flags;
    Muda_String          *Configurations;
    uint32_t              ConfigurationCount;
    const char           *LogFilePath;
} Command_Line_Config;

typedef struct Muda_Plugin_Config
{
    const char *MudaDirName;
    const char *Name;
    const char *Build;
    const char *BuildExtension;
    const char *BuildDir;
    uint32_t    RootBuild;
    uint32_t Succeeded; // if Prebuild, tells if the prebuild was successful, if Postbuild, tells if the compilation was
                        // successful
} Muda_Plugin_Config;

typedef struct Muda_Plugin_Event
{
    Muda_Plugin_Event_Kind Kind;

    union {
        Muda_Plugin_Config Prebuild;
        Muda_Plugin_Config Postbuild;
        Muda_Parser_Token  Parse;
    } Data;
} Muda_Plugin_Event;

typedef struct Muda_Plugin_Interface
{
    struct Memory_Arena *(*GetThreadScratchpad)(struct Thread_Context *);
    void *(*PushSize)(struct Memory_Arena *, uint32_t);
    void *(*PushSizeAligned)(struct Memory_Arena *, uint32_t, uint32_t);
    Temporary_Memory (*BeginTemporaryMemory)(struct Memory_Arena *);
    void (*EndTemporaryMemory)(Temporary_Memory *);

    void (*LogInfo)(struct Thread_Context *, const char *fmt, ...);
    void (*LogError)(struct Thread_Context *, const char *fmt, ...);
    void (*LogWarn)(struct Thread_Context *, const char *fmt, ...);
    void (*FatalError)(struct Thread_Context *, const char *msg);

    Command_Line_Config CommandLineConfig;

    char *PluginName;
    void *UserContext;

    struct
    {
        uint32_t Major, Minor, Patch;
    } Version;
} Muda_Plugin_Interface;

#define Muda_Event_Hook_Defn(name)                                                                                     \
    int32_t name(struct Thread_Context *Thread, Muda_Plugin_Interface *Interface, Muda_Plugin_Event *Event)
typedef Muda_Event_Hook_Defn((*Muda_Event_Hook_Procedure));

#define MudaHandleEvent()                                                                                              \
    MUDA_PLUGIN_INTERFACE void MudaAcceptVersion(uint32_t *major, uint32_t *minor, uint32_t *patch)                    \
    {                                                                                                                  \
        *major = MUDA_PLUGIN_VERSION_MAJOR;                                                                            \
        *minor = MUDA_PLUGIN_VERSION_MINOR;                                                                            \
        *patch = MUDA_PLUGIN_VERSION_PATCH;                                                                            \
    }                                                                                                                  \
    MUDA_PLUGIN_INTERFACE Muda_Event_Hook_Defn(External_MudaEventHook)

#ifndef MUDA_PLUGIN_IMPORT_INCLUDE

//
// Helpers
//

#define MudaPluginName(name) Interface->PluginName = name
#define MudaLog(fmt, ...) Interface->LogInfo(Thread, fmt, ##__VA_ARGS__)
#define MudaWarn(fmt, ...) Interface->LogWarn(Thread, fmt, ##__VA_ARGS__)
#define MudaError(fmt, ...) Interface->LogError(Thread, fmt, ##__VA_ARGS__)
#define MudaFatalError(msg) Interface->FatalError(Thread, msg)
#define MudaGetThreadScratch Interface->GetThreadScratchpad(Thread)
#define MudaPushSize(sz) Interface->PushSize(sz)
#define MudaPushType(type) Interface->PushSize(sizeof(sz))
#define MudaPushSizeAligned(sz, align) Interface->PushSizeAligned(sz, align)
#define MudaBeginTemporaryMemory(arena) Interface->BeginTemporaryMemory(arena)
#define MudaEndTemporaryMemory(temp) Interface->EndTemporaryMemory(temp)
#define MudaSetUserContext(context) Interface->UserContext = context
#define MudaGetUserContext(context) Interface->UserContext
#define MudaGetEventKind() Event->Kind
#define MudaGetPrebuildData() &Event->Data.Prebuild
#define MudaGetPostbuildData() &Event->Data.Postbuild

#endif

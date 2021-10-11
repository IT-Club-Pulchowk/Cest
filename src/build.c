//
// This file including all the *.c files so that we can simply build using single translation unit
//

#include "main.c"

#include "zBase.c"

#if PLATFORM_OS_WINDOWS == 1
#include "os_windows.c"
#endif
#if PLATFORM_OS_LINUX == 1
#include "os_linux.c"
#endif

#include "os.h"
#include "zBase.h"
#include "lenstring.h"

#include <errno.h>
#include <sys/stat.h>
#include <bits/statx.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio_ext.h>

void OsProcessExit(int code) {
    exit(0);
}

static bool GetInfo(File_Info *info, int dirfd, const String Path, const char * name, const int name_len){
    struct statx stats;
    statx(dirfd, (char *)Path.Data, 0x100, STATX_ALL, &stats);

    info->CreationTime   = stats.stx_btime.tv_nsec;
    info->LastAccessTime = stats.stx_atime.tv_nsec;
    info->LastWriteTime  = stats.stx_mtime.tv_nsec;
    info->Size           = stats.stx_size;

    Memory_Arena *scratch = ThreadScratchpad();
    info->Path.Length = Path.Length;
    info->Path.Data = PushSize(scratch, Path.Length);
    memcpy(info->Path.Data, Path.Data, Path.Length);
    info->Name.Data = info->Path.Data + (Path.Length - name_len - 1);
    info->Name.Length = name_len;

    info->Atribute = 0;
    __u64 attr = stats.stx_attributes;

	if (name[0] == '.')               info->Atribute |= File_Attribute_Hidden;
	if (stats.stx_mode & S_IFDIR)     info->Atribute |= File_Attribute_Directory;
	if (attr & STATX_ATTR_ENCRYPTED)  info->Atribute |= File_Attribute_Encrypted;
	if (attr & STATX_ATTR_IMMUTABLE)  info->Atribute |= File_Attribute_Read_Only;
	if (attr & STATX_ATTR_COMPRESSED) info->Atribute |= File_Attribute_Compressed;

    return (stats.stx_mode & S_IFDIR) == S_IFDIR;
}

static bool IterateInternal(const String path, Directory_Iterator iterator, void *context) {
    DIR *dir = opendir((char*)path.Data);
    if (!dir){ 
        char * err_msg = strerror(errno);
        LogError("Error (%d): %s Path: %s\n", errno, err_msg, path.Data);
        return false;
    }

    int dfd = dirfd(dir);
    struct dirent *dp;

    while ((dp = readdir(dir)) != NULL) {
        if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
            continue;
        int name_len = strlen(dp->d_name);
        Memory_Arena *scratch = ThreadScratchpad();
        Temporary_Memory temp = BeginTemporaryMemory(scratch);

        String new_path = {0, 0};
        new_path.Length = path.Length + name_len + 1;
        new_path.Data = PushSize(scratch, new_path.Length);
        sprintf(new_path.Data, "%s/%s", path.Data, dp->d_name);

        File_Info info;
        bool is_dir = GetInfo(&info, dfd, new_path, dp->d_name, name_len);
        Directory_Iteration iter = iterator(&info, context);

        if (is_dir && iter == Directory_Iteration_Recurse){
            IterateInternal(new_path, iterator, context);
        }
        else if (iter == Directory_Iteration_Break){
            EndTemporaryMemory(&temp);
            break;
        }

        EndTemporaryMemory(&temp);
    }

    closedir(dir);
    return true;
}

bool OsIterateDirectroy(const char *path, Directory_Iterator iterator, void *context) {
	Memory_Arena *scratch = ThreadScratchpad();
	Temporary_Memory temp = BeginTemporaryMemory(scratch);

	size_t path_len = strlen(path);
	String path_str = {0, 0};

    if (path[path_len - 1] ==  '/' || path[path_len - 1] == '\\'){
        path_str.Length = path_len;
        path_str.Data   = PushSize(scratch, path_str.Length);
        memcpy(path_str.Data, path, path_len);
        path_str.Data[path_str.Length - 1] = 0;
    }
    else{
        path_str.Length = path_len + 1;
        path_str.Data   = PushSize(scratch, path_str.Length);
        memcpy(path_str.Data, path, path_len);
        path_str.Data[path_str.Length - 1] = 0;
    }

    for (int i = 0; path_str.Data[i]; i ++){
        if (path_str.Data[i] == '\\')
            path_str.Data[i] = '/';
    }

    bool res = IterateInternal(path_str, iterator ? iterator : DirectoryIteratorPrint, context);

    EndTemporaryMemory(&temp);
    return res;
}

Compiler_Kind OsDetectCompiler() {
    Compiler_Kind compiler = 0;

    if (!system("which gcc > /dev/null 2>&1"))
        compiler |= Compiler_Bit_GCC;

    if (!system("which clang > /dev/null 2>&1"))
        compiler |= Compiler_Bit_CLANG;
    
    return compiler;
}

bool OsExecuteCommandLine(String cmdline) {
    return !system((char *)cmdline.Data);
}

Uint32 OsCheckIfPathExists(String path) {
    struct stat tmp;
    if (stat(path.Data, &tmp) == 0) {
        if ((tmp.st_mode & S_IFDIR) == S_IFDIR) {
            return Path_Exist_Directory;
        }
        else {
            return Path_Exist_File;
        }
    }
    return Path_Does_Not_Exist;
}

bool OsCreateDirectoryRecursively(String path){
    const int len = path.Length;
    for (int i = 0; i < len+1; i++) {
        if (path.Data[i] == '/' ) {
            path.Data[i] = '\0';
			const char* dir = path.Data;
            if (mkdir(dir, S_IRWXU) == -1 && errno != EEXIST) return false;
            path.Data[i] = '/';
        } else if(path.Data[i] == '\0') {
            const char* dir = path.Data;
            mkdir(dir, S_IRWXU);
        }
    }
	return true;
}

String OsGetUserConfigurationPath(String path) {
    Memory_Arena *scratch = ThreadScratchpad();
    return FmtStr(scratch, "~/%s", path.Data);
}

File_Handle OsFileOpen(const String path, File_Mode mode) {
	File_Handle handle;
    if (mode == File_Mode_Read)
        handle.PlatformFileHandle = fopen((char *)path.Data, "rb");
    else if (mode == File_Mode_Append)
        handle.PlatformFileHandle = fopen((char *)path.Data, "ab");
    else if (mode == File_Mode_Write)
        handle.PlatformFileHandle = fopen((char *)path.Data, "wb");
    else {
        Unreachable();
    }
    return handle;
}

Ptrsize OsFileGetSize(File_Handle handle) {
    struct stat stat;
    if(!fstat(fileno(handle.PlatformFileHandle), &stat))
        return stat.st_size;
    else
        return 0;
}

bool OsFileRead(File_Handle handle, Uint8 *buffer, Ptrsize size) {
    fread(buffer, sizeof(Uint8), size, handle.PlatformFileHandle);
    return errno == 0;
}

bool OsFileWrite(File_Handle handle, String data){
    fwrite(data.Data, sizeof(Uint8), data.Length, handle.PlatformFileHandle);
    return errno == 0;
}

bool OsFileWriteFV(File_Handle handle, const char *fmt, va_list args) {
    bool result = (vfprintf(handle.PlatformFileHandle, fmt, args) >= 0);
    return result;
}

bool OsFileWriteF(File_Handle handle, const char *fmt, ...){
	va_list args;
	va_start(args, fmt);
    bool result = (vfprintf(handle.PlatformFileHandle, fmt, args) >= 0);
	va_end(args);
    return result;
}

void OsFileClose(File_Handle handle) {
	fclose(handle.PlatformFileHandle);
}

void OsSetupConsole() {
    // we have nothing to do here :)
}

void *OsGetStdOutputHandle() {
    return stdout;
}

void *OsGetErrorOutputHandle() {
    return stderr;
}

void OsConsoleOut(void *fp, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf((FILE *)fp, fmt, args);
    va_end(args);
}

void OsConsoleOutV(void *fp, const char *fmt, va_list list) {
    vfprintf((FILE *)fp, fmt, list);
}

void OsConsoleWrite(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void OsConsoleWriteV(const char *fmt, va_list list) {
    vprintf(fmt, list);
}

String OsConsoleRead(char *buffer, Uint32 size) {
    fgets(buffer, size, stdin);
	Uint32 len = strlen(buffer);
    if (buffer [len - 1] == '\n')
        len --;
    buffer [len] = 0;
    __fpurge(stdin);

    return StringMake(buffer, len);
}

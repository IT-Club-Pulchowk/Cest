#include "os.h"
#include "zBase.h"
#include "lenstring.h"

#include <errno.h>
#include <sys/stat.h>
#include <bits/statx.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool GetInfo(File_Info *info, int dirfd, const String Path, const char * name, const int name_len){
    struct statx stats;
    statx(dirfd, (char *)Path.Data, AT_SYMLINK_NOFOLLOW, STATX_ALL, &stats);

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
    if (!system("which gcc > /dev/null 2>&1")) {
        LogInfo("GCC Detected\n");
        return Compiler_Kind_GCC;
    }
    if (!system("which clang > /dev/null 2>&1")) {
        LogInfo("CLANG Detected\n");
        return Compiler_Kind_CLANG;
    }

    LogError("Error: Failed to detect compiler! Install one of the compilers from below...\n");
    LogInfo("CLANG: https://releases.llvm.org/download.html \n");
    LogInfo("GCC: https://gcc.gnu.org/install/download.html \n");
    return Compiler_Kind_NULL;
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

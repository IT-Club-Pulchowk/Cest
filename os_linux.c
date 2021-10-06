#include "os.h"
#include "zBase.h"

#include <linux/stat.h>
#include <sys/stat.h>
#include <bits/statx.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool GetInfo(File_Info *info, const String Path, const char * name, const int name_len){
    struct statx stats;
    statx(open(".", O_DIRECTORY), (char *)Path.Data, AT_SYMLINK_NOFOLLOW, STATX_ALL, &stats);

    Memory_Arena *scratch = ThreadScratchpad();
    info->Path.Length = Path.Length;
    info->Path.Data = PushSize(scratch, Path.Length);
    memcpy(info->Path.Data, Path.Data, Path.Length);
    info->Name.Data = info->Path.Data + (Path.Length - name_len);
    info->Name.Length = name_len;

    info->Atribute = 0;

    //TODO : Atributes, times and size

    return (stats.stx_mode & S_IFDIR) == S_IFDIR;
}

static Directory_Iteration DefaultIteratorPrint(File_Info *info, void *context){

    //TODO : Complete this

    LogInfo("%s\n", info->Path.Data);
    return Directory_Iteration_Continue;
}

static bool IterateInternal(const String path, Directory_Iterator iterator, void *context) {
    DIR *dir = opendir((char *)path.Data);
    if (!dir) return false;

    struct dirent *dp;

    while ((dp = readdir(dir)) != NULL) {
        if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
            continue;
        int name_len = strlen(dp->d_name);
        Memory_Arena *scratch = ThreadScratchpad();
        Temporary_Memory temp = BeginTemporaryMemory(scratch);

        String new_path = {0, 0};
        new_path.Length = path.Length + name_len + 2;
        new_path.Data = PushSize(scratch, new_path.Length);
        sprintf(new_path.Data, "%s/%s", path.Data, dp->d_name);

        File_Info info;
        bool is_dir = GetInfo(&info, new_path, dp->d_name, name_len);
        Directory_Iteration iter = iterator ? iterator(&info, context) : DefaultIteratorPrint(&info, context);

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

bool IterateDirectroy(const char *path, Directory_Iterator iterator, void *context) {
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

    bool res = IterateInternal(path_str, iterator, context);

    EndTemporaryMemory(&temp);
    return res;
}

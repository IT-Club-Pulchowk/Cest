#include "os.h"
#include "zBase.h"

#include <sys/stat.h>
#include <bits/statx.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

 // Unlimited seg fault works

static bool GetFileInfo(File_Info *dst, const char *cur_path, const char *name){
    struct statx src;
    int pathlen = strlen(cur_path);
    int dirfd = open(".", O_DIRECTORY);
    statx(dirfd, (char*)cur_path, AT_SYMLINK_NOFOLLOW, STATX_ALL, &src);

    dst->LastWriteTime = src.stx_mtime.tv_nsec;
    dst->LastAccessTime = src.stx_atime.tv_nsec;
    dst->CreationTime = src.stx_ctime.tv_nsec;
    dst->Size = src.stx_size;

    Memory_Arena *scratch = ThreadScratchpad();

    dst->Path.Data = (Uint8 *)PushSize(scratch, pathlen * sizeof(char));
    memcpy(dst->Path.Data, cur_path, pathlen);
    dst->Path.Length = pathlen;
    dst->Name.Data = dst->Path.Data + (pathlen - strlen(name));
    dst->Name.Length = strlen(name);

	__u64 attr = src.stx_attributes;
	dst->Atribute = 0;

	/* if (attr & FILE_ATTRIBUTE_ARCHIVE) dst->Atribute |= File_Attribute_Archive; */
	if (attr & STATX_ATTR_COMPRESSED) dst->Atribute |= File_Attribute_Compressed;
	if (src.stx_mode & S_IFDIR) dst->Atribute |= File_Attribute_Directory;
	if (attr & STATX_ATTR_ENCRYPTED) dst->Atribute |= File_Attribute_Encrypted;
	/* if (attr & FILE_ATTRIBUTE_HIDDEN) dst->Atribute |= File_Attribute_Hidden; */
	/* if (attr & FILE_ATTRIBUTE_NORMAL) dst->Atribute |= File_Attribute_Normal; */
	/* if (attr & FILE_ATTRIBUTE_OFFLINE) dst->Atribute |= File_Attribute_Offline; */
	if (attr & STATX_ATTR_IMMUTABLE) dst->Atribute |= File_Attribute_Read_Only;
	/* if (attr & FILE_ATTRIBUTE_SYSTEM) dst->Atribute |= File_Attribute_System; */
	/* if (attr & FILE_ATTRIBUTE_TEMPORARY) dst->Atribute |= File_Attribute_Temporary; */

    return (src.stx_mode & S_IFDIR) == S_IFDIR;
}

bool IterateDirectroy(const char *path, Directory_Iterator iterator, void *context) {
    DIR *dirp;
    struct dirent *dp;
    dirp = opendir(path);

    while ((dp = readdir(dirp)) != NULL) {
        if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
            continue;
        Memory_Arena *scratch = ThreadScratchpad();
        char* cur_path = (char *)PushSize(scratch, (strlen(path) + strlen(dp->d_name) + 2) * sizeof(char));
        strcpy(cur_path, path);
        strcat(cur_path, "/");
        strcat(cur_path, dp->d_name);

        File_Info info;
        bool isdir = GetFileInfo(&info, cur_path, dp->d_name);

        Directory_Iteration iterate = iterator(&info, context);
        
        if (iterate == Directory_Iteration_Recurse && isdir)
            IterateDirectroy(cur_path, iterator, context);
        else if(iterate == Directory_Iteration_Break) break;
    }

    closedir(dirp);
    return true;
}

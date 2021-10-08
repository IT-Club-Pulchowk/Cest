#include "os.h"
#include<stdio.h>
#define WIN32_MEAN_AND_LEAN
#include <windows.h>

Compiler_Kind DetectCompiler() {
	Memory_Arena *scratch = ThreadScratchpad();

	DWORD length = 0;
	if ((length = SearchPathW(NULL, L"cl", L".exe", 0, NULL, NULL))) {
		Temporary_Memory temp = BeginTemporaryMemory(scratch);
		wchar_t *dir = 0;
		wchar_t *path = PushSize(scratch, sizeof(wchar_t) * (length + 1));
		SearchPathW(NULL, L"cl", L".exe", length, path, &dir);
		LogInfo("CL Detected: \"%S\"\n", path);
		EndTemporaryMemory(&temp);
		return Compiler_Kind_CL;
	}
	
	length = 0;
	if ((length = SearchPathW(NULL, L"clang", L".exe", 0, NULL, NULL))) {
		Temporary_Memory temp = BeginTemporaryMemory(scratch);
		wchar_t *dir = 0;
		wchar_t *path = PushSize(scratch, sizeof(wchar_t) * (length + 1));
		SearchPathW(NULL, L"clang", L".exe", length, path, &dir);
		LogInfo("CLANG Detected: \"%S\"\n", path);
		EndTemporaryMemory(&temp);
		return Compiler_Kind_CLANG;
	}

	LogError("Error: Failed to detect compiler!\n");
	return Compiler_Kind_NULL;
}

static wchar_t *UnicodeToWideChar(const char *msg, int length) {
	Memory_Arena *scratch = ThreadScratchpad();
	wchar_t *result = (wchar_t *)PushSize(scratch, (length + 1) * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, msg, length, result, length + 1);
	result[length] = 0;
	return result;
}

static void ConvertWin32FileInfo(File_Info *dst, WIN32_FIND_DATAW *src, String root) {
	ULARGE_INTEGER converter;

	converter.HighPart = src->ftCreationTime.dwHighDateTime;
	converter.LowPart = src->ftCreationTime.dwLowDateTime;
	dst->CreationTime = converter.QuadPart;

	converter.HighPart = src->ftLastAccessTime.dwHighDateTime;
	converter.LowPart = src->ftLastAccessTime.dwLowDateTime;
	dst->LastAccessTime = converter.QuadPart;

	converter.HighPart = src->ftLastWriteTime.dwHighDateTime;
	converter.LowPart = src->ftLastWriteTime.dwLowDateTime;
	dst->LastWriteTime = converter.QuadPart;

	converter.HighPart = src->nFileSizeHigh;
	converter.LowPart = src->nFileSizeLow;
	dst->Size = converter.QuadPart;

	DWORD attr = src->dwFileAttributes;
	dst->Atribute = 0;

	if (attr & FILE_ATTRIBUTE_ARCHIVE) dst->Atribute |= File_Attribute_Archive;
	if (attr & FILE_ATTRIBUTE_COMPRESSED) dst->Atribute |= File_Attribute_Compressed;
	if (attr & FILE_ATTRIBUTE_DIRECTORY) dst->Atribute |= File_Attribute_Directory;
	if (attr & FILE_ATTRIBUTE_ENCRYPTED) dst->Atribute |= File_Attribute_Encrypted;
	if (attr & FILE_ATTRIBUTE_HIDDEN) dst->Atribute |= File_Attribute_Hidden;
	if (attr & FILE_ATTRIBUTE_NORMAL) dst->Atribute |= File_Attribute_Normal;
	if (attr & FILE_ATTRIBUTE_OFFLINE) dst->Atribute |= File_Attribute_Offline;
	if (attr & FILE_ATTRIBUTE_READONLY) dst->Atribute |= File_Attribute_Read_Only;
	if (attr & FILE_ATTRIBUTE_SYSTEM) dst->Atribute |= File_Attribute_System;
	if (attr & FILE_ATTRIBUTE_TEMPORARY) dst->Atribute |= File_Attribute_Temporary;

	Memory_Arena *scratch = ThreadScratchpad();

	int len = snprintf(NULL, 0, "%s%S", root.Data, src->cFileName);
	Assert(len > 0);

	// Fir directory allocate 2 bytes extra incase we might want to postfix with "/*" when recursively iterating
	// We also zero the extra 2 bytes that are allocated
	bool is_dir = ((dst->Atribute & File_Attribute_Directory) == File_Attribute_Directory);
	dst->Path.Data = (Uint8 *)PushSize(scratch, (len + 1 + 2 * is_dir) * sizeof(char));
	len = snprintf(dst->Path.Data, len + 1 + 2 * is_dir, "%s%S", root.Data, src->cFileName);
	dst->Path.Data[len + 1 * is_dir] = 0;
	dst->Path.Data[len + 2 * is_dir] = 0;
	dst->Path.Length = len;
	dst->Name.Data = dst->Path.Data + root.Length;
	dst->Name.Length = dst->Path.Length - root.Length;
}

// The path parameter of this procedure MUST be the non const string that ends with "/*"
static bool IterateDirectroyInternal(String path, Directory_Iterator iterator, void *context) {
	Memory_Arena *scratch = ThreadScratchpad();

	wchar_t *wpath = UnicodeToWideChar(path.Data, path.Length);

	WIN32_FIND_DATAW find_data;
	HANDLE find_handle = FindFirstFileW(wpath, &find_data);
	if (find_handle == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		DWORD message_length = 256;
		wchar_t *message = (wchar_t *)PushSize(scratch, message_length * sizeof(wchar_t));

		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), message, message_length, NULL);
		path.Data[path.Length - 2] = 0;
		LogError("Error (%d): %S Path: %s", error, message, path.Data);

		return false;
	}

	// Removing "*" from "root_dir/*"
	path.Length -= 1;
	path.Data[path.Length] = '\0';

	while (true) {
		if (wcscmp(find_data.cFileName, L".") != 0 &&
			wcscmp(find_data.cFileName, L"..") != 0) {

			Temporary_Memory temp = BeginTemporaryMemory(scratch);

			File_Info info;
			ConvertWin32FileInfo(&info, &find_data, path);

			Directory_Iteration result = iterator(&info, context);

			if ((info.Atribute & File_Attribute_Directory) && result == Directory_Iteration_Recurse) {
				// ConvertWin32FileInfo procedure allocates 2 bytes more and zered,
				// so adding "/*" to "root_dir" is fine
				info.Path.Data[info.Path.Length] = '/';
				info.Path.Data[info.Path.Length + 1] = '*';
				info.Path.Length += 2;
				IterateDirectroyInternal(info.Path, iterator, context);
			} else if (result == Directory_Iteration_Break) {
				EndTemporaryMemory(&temp);
				break;
			}

			EndTemporaryMemory(&temp);
		}

		if (FindNextFileW(find_handle, &find_data) == 0) break;
	}

	FindClose(find_handle);

	return true;
}

bool IterateDirectroy(const char *path, Directory_Iterator iterator, void *context) {
	Memory_Arena *scratch = ThreadScratchpad();

	Temporary_Memory temp = BeginTemporaryMemory(scratch);

	size_t len = strlen(path);

	String path_normalized = {0, 0};
	if (path[len - 1] == '/' || path[len - 1] == '\\') {
		path_normalized.Data = PushSize(scratch, len + 2);
		memcpy(path_normalized.Data, path, len);
		path_normalized.Data[len] = '*';
		path_normalized.Data[len + 1] = '\0';
		path_normalized.Length = len + 1;
	}
	else {
		path_normalized.Data = PushSize(scratch, len + 3);
		memcpy(path_normalized.Data, path, len);
		path_normalized.Data[len] = '/';
		path_normalized.Data[len + 1] = '*';
		path_normalized.Data[len + 2] = '\0';
		path_normalized.Length = len + 2;
	}

	{
		char *iter = path_normalized.Data;
		while (*iter) {
			if (*iter == '\\') *iter = '/';
			iter += 1;
		}
	}

	if (!iterator) iterator = DirectoryIteratorPrint;

	bool result = IterateDirectroyInternal(path_normalized, iterator, context);

	EndTemporaryMemory(&temp);

	return result;
}


bool OsLaunchCompilation(Compiler_Kind compiler, String cmdline) {
	Assert(compiler == Compiler_Kind_CL);

	wchar_t *wcmdline = UnicodeToWideChar(cmdline.Data, (int)cmdline.Length);

	STARTUPINFOW start_up = { sizeof(start_up) };
	PROCESS_INFORMATION process;
	memset(&process, 0, sizeof(process));

	// TODO: Use security attributes??
	CreateProcessW(NULL, wcmdline, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL, NULL, &start_up, &process);

	WaitForSingleObject(process.hProcess, INFINITE);

	CloseHandle(process.hProcess);
	CloseHandle(process.hThread);

	return true;
}

bool  createDirectoryRecursively( char path1[])
{
	wchar_t *dirPath=NULL;
	const int len=strlen(path1);
    for (int i = 0; i <len+1; i++)
    {
        if (path1[i] == '/'||path1[i]=='\0' )
        {
			dirPath= UnicodeToWideChar(path1,i);
			CreateDirectoryW(dirPath,NULL);
        }
    }
	return true;
}

#include "os.h"

#define WIN32_MEAN_AND_LEAN
#include <Windows.h>

static wchar_t *UnicodeToWideChar(const char *msg, int length) {
	Memory_Arena *scratch = ThreadScratchpad();
	wchar_t *result = (wchar_t *)PushSize(scratch, (length + 1) * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, msg, length, result, length + 1);
	result[length] = 0;
	return result;
}

static int ConvertWin32FileInfo(File_Info *dst, WIN32_FIND_DATAW *src, const char *root, int root_len) {
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

	int len = snprintf(NULL, 0, "%s%S", root, src->cFileName);
	Assert(len > 0 && root_len > 0);

	// Fir directory allocate 1 byte extra incase we might want to postfix with "/*" when recursively iterating
	bool is_dir = ((dst->Atribute & File_Attribute_Directory) == File_Attribute_Directory);
	dst->Path = (char *)PushSize(scratch, (len + 1 + 2 * is_dir) * sizeof(char));
	len = snprintf(dst->Path, len + 1 + 2 * is_dir, "%s%S", root, src->cFileName);
	dst->Path[len + 1 * is_dir] = 0;
	dst->Path[len + 2 * is_dir] = 0;
	dst->Name = dst->Path + root_len;

	return len;
}

static bool IterateDirectroyInternal(char *path_normalized, int len, directory_iterator iterator, void *context) {
	Memory_Arena *scratch = ThreadScratchpad();

	wchar_t *wpath = UnicodeToWideChar(path_normalized, len);

	WIN32_FIND_DATAW find_data;
	HANDLE find_handle = FindFirstFileW(wpath, &find_data);
	if (find_handle == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		DWORD message_length = 256;
		wchar_t *message = (wchar_t *)PushSize(scratch, message_length * sizeof(wchar_t));

		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), message, message_length, NULL);
		LogError("Error (%d): %S", error, message);

		return false;
	}

	// Removing "*" from "root_dir/*"
	len -= 1;
	path_normalized[len] = '\0';

	while (true) {
		if (wcscmp(find_data.cFileName, L".") != 0 &&
			wcscmp(find_data.cFileName, L"..") != 0) {

			Temporary_Memory temp = BeginTemporaryMemory(scratch);

			File_Info info;
			int path_len = ConvertWin32FileInfo(&info, &find_data, path_normalized, len);

			Directory_Iteration result = iterator(&info, context);

			if (result == Directory_Iteration_Break) {
				EndTemporaryMemory(&temp);
				break;
			}

			if ((info.Atribute & File_Attribute_Directory) && result == Directory_Iteration_Recurse) {
				// Adding "/*" to "root_dir/"
				info.Path[path_len] = '/';
				info.Path[path_len + 1] = '*';
				IterateDirectroyInternal(info.Path, path_len + 2, iterator, context);
			}

			EndTemporaryMemory(&temp);
		}

		if (FindNextFileW(find_handle, &find_data) == 0) break;
	}

	FindClose(find_handle);

	return true;
}

bool IterateDirectroy(const char *path, directory_iterator iterator, void *context) {
	Memory_Arena *scratch = ThreadScratchpad();

	Temporary_Memory temp = BeginTemporaryMemory(scratch);

	size_t len = strlen(path);

	char *path_normalized = 0;
	if (path[len - 1] == '/' || path[len - 1] == '\\') {
		path_normalized = PushSize(scratch, len + 2);
		memcpy(path_normalized, path, len);
		path_normalized[len] = '*';
		path_normalized[len + 1] = '\0';
		len += 1;
	}
	else {
		path_normalized = PushSize(scratch, len + 3);
		memcpy(path_normalized, path, len);
		path_normalized[len] = '/';
		path_normalized[len + 1] = '*';
		path_normalized[len + 2] = '\0';
		len += 2;
	}

	{
		char *iter = path_normalized;
		while (*iter) {
			if (*iter == '\\') *iter = '/';
			iter += 1;
		}
	}

	if (!iterator) iterator = DirectoryIteratorPrint;

	bool result = IterateDirectroyInternal(path_normalized, len, iterator, context);

	EndTemporaryMemory(&temp);

	return result;
}

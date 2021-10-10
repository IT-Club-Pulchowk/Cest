#include "os.h"
#include "lenstring.h"

#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <shellapi.h>
#include <UserEnv.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Advapi32.lib")

static wchar_t *UnicodeToWideChar(const char *msg, int length) {
	Memory_Arena *scratch = ThreadScratchpad();
	wchar_t *result = (wchar_t *)PushSize(scratch, (length + 1) * sizeof(wchar_t));
	int wlen = MultiByteToWideChar(CP_UTF8, 0, msg, length, result, length + 1);
	result[wlen] = 0;
	return result;
}

static wchar_t *UnicodeToWideCharLength(const char *msg, int length, int *out_length) {
	Memory_Arena *scratch = ThreadScratchpad();
	wchar_t *result = (wchar_t *)PushSize(scratch, (length + 1) * sizeof(wchar_t));
	*out_length = MultiByteToWideChar(CP_UTF8, 0, msg, length, result, length + 1);
	result[*out_length] = 0;
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

bool OsIterateDirectroy(const char *path, Directory_Iterator iterator, void *context) {
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

Compiler_Kind OsDetectCompiler() {
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

	LogError("Error: Failed to detect compiler! Install one of the compilers from below...\n");
	LogInfo("Visual Studio (MSVC): https://visualstudio.microsoft.com/ \n");
	LogInfo("CLANG: https://releases.llvm.org/download.html \n");

	return Compiler_Kind_NULL;
}

bool OsExecuteCommandLine(String cmdline) {
	wchar_t *wcmdline = UnicodeToWideChar(cmdline.Data, (int)cmdline.Length);

	STARTUPINFOW start_up = { sizeof(start_up) };
	PROCESS_INFORMATION process;
	memset(&process, 0, sizeof(process));

	CreateProcessW(NULL, wcmdline, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL, NULL, &start_up, &process);

	WaitForSingleObject(process.hProcess, INFINITE);

	CloseHandle(process.hProcess);
	CloseHandle(process.hThread);

	return true;
}

Uint32 OsCheckIfPathExists(String path) {
	wchar_t *dir = UnicodeToWideChar(path.Data, path.Length);

	if (PathFileExistsW(dir)) {
		DWORD attr = GetFileAttributesW(dir);
		if (attr != INVALID_FILE_ATTRIBUTES) {
			if (attr & FILE_ATTRIBUTE_DIRECTORY) return Path_Exist_Directory;
			return Path_Exist_File;
		}
		return Path_Exist_File;
	}

	return Path_Does_Not_Exist;
}

bool OsCreateDirectoryRecursively(String path) {
	int len = 0;
	wchar_t *dir = UnicodeToWideCharLength(path.Data, path.Length, &len);

    for (int i = 0; i < len + 1; i++) {
        if (dir[i] == (Uint16)'/' || dir[i] == 0 ) {
			dir[i] = 0;
			if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
			dir[i] = (Uint16)'/';
        }
    }

	return true;
}

String OsGetUserConfigurationPath(String path) {
	Memory_Arena *scratch = ThreadScratchpad();

	HANDLE token = INVALID_HANDLE_VALUE;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &token)) {
		DWORD length = 0;
		GetUserProfileDirectoryW(token, NULL, &length);
		length += 1;
		wchar_t *wpath = PushSize(scratch, length * sizeof(wchar_t));
		if (GetUserProfileDirectoryW(token, wpath, &length)) {
			return FmtStr(scratch, "%S/%s", wpath, path.Data);
		}
	}

	// in case we fail, we'll use this as backup
	return FmtStr(scratch, "C:/%s", path.Data);
}

File_Handle OsOpenFile(const String path) {
	wchar_t *wpath = UnicodeToWideChar(path.Data, path.Length);
	HANDLE whandle = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

	File_Handle handle;
	handle.PlatformFileHandle = whandle;
	return handle;
}

bool OsFileHandleIsValid(File_Handle handle) {
	return handle.PlatformFileHandle != INVALID_HANDLE_VALUE;
}

Ptrsize OsGetFileSize(File_Handle handle) {
	LARGE_INTEGER size;
	if (GetFileSizeEx(handle.PlatformFileHandle, &size)) {
		return size.QuadPart;
	}
	return 0;
}

bool OsReadFile(File_Handle handle, Uint8 *buffer, Ptrsize size) {
	// this is done because ReadFile can with blocks of DWORD and not LARGE_INTEGER
	DWORD read_size = 0;
	if (size > UINT32_MAX)
		read_size = UINT32_MAX;
	else
		read_size = size;

	DWORD read_bytes = 0;
	Uint8 *end_ptr = buffer + size;
	for (Uint8 *read_ptr = (Uint8 *)buffer; read_ptr < end_ptr; read_ptr += read_bytes) {
		// resetting *read_bytes* because docs says to (for next loop)
		read_bytes = 0;
		BOOL res = ReadFile(handle.PlatformFileHandle, read_ptr, read_size, &read_bytes, 0);
		if (res && (read_bytes == 0 || read_bytes == size)) {
			// END OF FILE reached
			break;
		}
		else {
			// if no error is registered, it is regarded as success
			if (GetLastError()) { 
				return false;
				break;
			}
			else {
				break;
			}
		}
	}

	return true;
}

void OsCloseFile(File_Handle handle) {
	CloseHandle(handle.PlatformFileHandle);
}

void OsSetupConsole() {
	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);
}

void *OsGetStdOutputHandle() {
	return GetStdHandle(STD_OUTPUT_HANDLE);
}

void *OsGetErrorOutputHandle() {
	return GetStdHandle(STD_ERROR_HANDLE);
}

void OsConsoleOut(void *fp, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	OsConsoleOutV(fp, fmt, args);
	va_end(args);
}

void OsConsoleOutV(void *fp, const char *fmt, va_list list) {
	Memory_Arena *scratch = ThreadScratchpad();
	Temporary_Memory temp = BeginTemporaryMemory(scratch);
	String out = FmtStrV(scratch, fmt, list);
	int len = 0;
	wchar_t *wout = UnicodeToWideCharLength(out.Data, out.Length, &len);
	DWORD written = 0;
	WriteConsoleW((HANDLE)fp, wout, (DWORD)len, &written, NULL);
	EndTemporaryMemory(&temp);
}

void OsConsoleWrite(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	OsConsoleWriteV(fmt, args);
	va_end(args);
}

void OsConsoleWriteV(const char *fmt, va_list list) {
	Memory_Arena *scratch = ThreadScratchpad();
	Temporary_Memory temp = BeginTemporaryMemory(scratch);
	String out = FmtStrV(scratch, fmt, list);
	int len = 0;
	wchar_t *wout = UnicodeToWideCharLength(out.Data, out.Length, &len);
	DWORD written = 0;
	WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), wout, (DWORD)len, &written, NULL);
	EndTemporaryMemory(&temp);
}

Uint32 OsConsoleRead(char *buffer, Uint32 size) {
	Memory_Arena *scratch = ThreadScratchpad();
	Temporary_Memory temp = BeginTemporaryMemory(scratch);

	DWORD characters_to_read = size / 2 + 1;
	DWORD characters_read = 0;
	wchar_t *wbuffer = PushSize(scratch, characters_to_read);

	ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), wbuffer, characters_to_read, &characters_read, NULL);

	int len = WideCharToMultiByte(CP_UTF8, 0, wbuffer, characters_read, buffer, size, 0, 0);
	if (len >= size) len = size - 1;
	buffer[len] = 0;

	EndTemporaryMemory(&temp);

	return len;
}

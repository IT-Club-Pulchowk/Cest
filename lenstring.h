#pragma once

#include "zBase.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

inline String FmtStrV(Memory_Arena *arena, const char *fmt, va_list list) {
	va_list args;
	va_copy(args, list);
	int len = 1 + vsnprintf(NULL, 0, fmt, args);
	char *buf = (char *)PushSize(arena, len);
	vsnprintf(buf, len, fmt, args);
	va_end(args);
	return StringMake((Uint8 *)buf, len - 1);
}

inline String FmtStr(Memory_Arena *arena, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	String string = FmtStrV(arena, fmt, args);
	va_end(args);
	return string;
}

inline String StrRemovePrefix(String str, Int64 count) {
	Assert(str.Length >= count);
	str.Data += count;
	str.Length -= count;
	return str;
}

inline String StrRemoveSuffix(String str, Int64 count) {
	Assert(str.Length >= count);
	str.Length -= count;
	return str;
}

inline Int64 StrCopy(String src, char *const dst, Int64 dst_size, Int64 count) {
	Int64 copied = (Int64)Clamp(dst_size, src.Length, count);
	memcpy(dst, src.Data, copied);
	return copied;
}

inline String SubStr(String str, Int64 index, Int64 count) {
	Assert(index < str.Length);
	count = (Int64)Minimum(str.Length, count);
	return StringMake(str.Data + index, count);
}

inline int StrCompare(String a, String b) {
	Int64 count = (Int64)Minimum(a.Length, b.Length);
	return memcmp(a.Data, b.Data, count);
}

inline int StrCompareCaseInsensitive(String a, String b) {
	Int64 count = (Int64)Minimum(a.Length, b.Length);
	for (Int64 index = 0; index < count; ++index) {
		if (a.Data[index] != b.Data[index] &&
			a.Data[index] + 32 != b.Data[index] &&
			a.Data[index] != b.Data[index]) {
			return a.Data[index] - b.Data[index];
		}
	}
	return 0;
}

inline bool StrMatch(String a, String b) {
	if (a.Length != b.Length) return false;
	return StrCompare(a, b) == 0;
}

inline bool StrMatchCaseInsensitive(String a, String b) {
	if (a.Length != b.Length) return false;
	return StrCompareCaseInsensitive(a, b) == 0;
}

inline bool StrStartsWith(String str, String sub) {
	if (str.Length < sub.Length) return false;
	return StrCompare(StringMake(str.Data, sub.Length), sub) == 0;
}

inline bool StrStartsWithCaseInsensitive(String str, String sub) {
	if (str.Length < sub.Length) return false;
	return StrCompareCaseInsensitive(StringMake(str.Data, sub.Length), sub) == 0;
}

inline bool StrStartsWithCharacter(String str, Uint8 c) {
	return str.Length && str.Data[0] == c;
}

inline bool StrStartsWithCharacterCaseInsensitive(String str, Uint8 c) {
	return str.Length && (str.Data[0] == c || str.Data[0] + 32 == c || str.Data[0] == c + 32);
}

inline bool StrEndsWith(String str, String sub) {
	if (str.Length < sub.Length) return false;
	return StrCompare(StringMake(str.Data + str.Length - sub.Length, sub.Length), sub) == 0;
}

inline bool StringEndsWithCaseInsensitive(String str, String sub) {
	if (str.Length < sub.Length) return false;
	return StrCompareCaseInsensitive(StringMake(str.Data + str.Length - sub.Length, sub.Length), sub) == 0;
}

inline bool StrEndsWithCharacter(String str, Uint8 c) {
	return str.Length && str.Data[str.Length - 1] == c;
}

inline bool StrEndsWithCharacterCaseInsensitive(String str, Uint8 c) {
	return str.Length && (str.Data[str.Length - 1] == c || str.Data[str.Length - 1] + 32 == c || str.Data[str.Length - 1] == c + 32);
}

inline char *StrNullTerminated(char *buffer, String str) {
	memcpy(buffer, str.Data, str.Length);
	buffer[str.Length] = 0;
	return buffer;
}

inline Int64 StrFind(String str, String key, Int64 pos) {
	Int64 index = Clamp(0, str.Length - 1, pos);
	while (str.Length >= key.Length) {
		if (StrCompare(StringMake(str.Data, key.Length), key) == 0) {
			return index;
		}
		index += 1;
		str = StrRemovePrefix(str, 1);
	}
	return -1;
}

inline Int64 StrFindCharacter(String str, Uint8 key, Int64 pos) {
	for (Int64 index = Clamp(0, str.Length - 1, pos); index < str.Length; ++index)
		if (str.Data[index] == key) return index;
	return -1;
}

inline Int64 StrReverseFind(String str, String key, Int64 pos) {
	Int64 index = Clamp(0, str.Length - key.Length, pos);
	while (index >= 0) {
		if (StrCompare(StringMake(str.Data + index, key.Length), key) == 0)
			return index;
		index -= 1;
	}
	return -1;
}

inline Int64 StrReverseFindCharacter(String str, Uint8 key, Int64 pos) {
	for (Int64 index = Clamp(0, str.Length - 1, pos); index >= 0; --index)
		if (str.Data[index] == key) return index;
	return -1;
}

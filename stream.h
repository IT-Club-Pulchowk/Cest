#pragma once

#include "zBase.h"
#include <string.h>
#include <stdio.h>

#define OSTREAM_BUCKET_SIZE 16384

struct Out_Stream_Bucket {
	Uint8 Data[OSTREAM_BUCKET_SIZE];
	Int64 Used;

	struct Out_Stream_Bucket *Next;
};

typedef struct Out_Stream {
	struct Out_Stream_Bucket  Head;
	struct Out_Stream_Bucket *Tail;
	Int64					  Size;

	Memory_Allocator Allocator;
} Out_Stream;

inline void OutBuffer(Out_Stream *out, const void *ptr, Int64 size) {
	Uint8 *data = (Uint8 *)ptr;

	while (size) {
		if (out->Tail->Used == OSTREAM_BUCKET_SIZE) {
			if (out->Tail->Next == NULL) {
				out->Tail->Next = (struct Out_Stream_Bucket *)MemoryAllocate(sizeof(struct Out_Stream_Bucket), &out->Allocator);
				out->Tail->Next = NULL;
			} else {
				out->Tail = out->Tail->Next;
			}

			out->Tail->Used = 0;
		}

		Int64 write = Minimum(size, OSTREAM_BUCKET_SIZE - out->Tail->Used);
		memcpy(out->Tail->Data + out->Tail->Used, data, write);
		size -= write;
		out->Tail->Used += write;
		out->Size += write;
	}
}

inline void OutFormatted(Out_Stream *out, const char *fmt, ...) {
	Memory_Arena *scratch = ThreadScratchpad();

	Temporary_Memory temp = BeginTemporaryMemory(scratch);
	
	va_list args0, args1;
	va_start(args0, fmt);
	va_copy(args1, args0);

	int len = 1 + vsnprintf(NULL, 0, fmt, args1);
	char *buf = (char *)PushSize(scratch, len);
	vsnprintf(buf, len, fmt, args0);

	va_end(args1);
	va_end(args0);

	OutBuffer(out, buf, len - 1);

	EndTemporaryMemory(&temp);
}

inline Int64 OutGetSize(Out_Stream *out) {
	return out->Size;
}

inline String OutBuildString(Out_Stream *out) {
	String string;
	string.Data = (Uint8 *)MemoryAllocate(out->Size + 1, &ThreadContext.Allocator);
	string.Length = 0;

	struct Out_Stream_Bucket *buk = &out->Head;

	while (buk) {
		Int64 copy = buk->Used;
		memcpy(string.Data + string.Length, buk->Data, copy);
		string.Length += copy;
		buk = buk->Next;
	}

	string.Data[string.Length] = 0;

	return string;
}

inline void OutReset(Out_Stream *out) {
	Assert(out->Tail->Next == NULL);	
	struct Out_Stream_Bucket *buk = &out->Head;
	while (buk) {
		buk->Used = 0;
		buk = buk->Next;
	}
	out->Size = 0;
}

inline void OutCreate(Out_Stream *out, Memory_Allocator allocator) {
	out->Allocator = allocator;
	out->Size = 0;
	out->Head.Next = NULL;
	out->Head.Used = 0;
	out->Tail = &out->Head;
}

inline void OutDestroy(Out_Stream *out) {
	while (out->Head.Next) {
		struct Out_Stream_Bucket *buk = out->Head.Next;
		out->Head.Next = buk->Next;
		MemoryFree(buk, &out->Allocator);
	}
}

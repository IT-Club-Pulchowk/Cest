#pragma once
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "zBase.h"
#include <ctype.h>

#ifdef __linux
    #define INLINE static inline
#endif
#ifdef _WIN32
    #define INLINE inline
#endif

typedef enum {
    Muda_Token_Section,
    Muda_Token_Property,
    Muda_Token_Comment,
    Muda_Token_Error,
}Muda_Token_Kind;

typedef struct Muda_Token {
    union {
        String Section;
        String Comment;
        struct {
            String Key;
            String Value;
            String *Values;
        } Property;
        struct{
            char Desc[256];
            uint32_t Line, Column;
        }Error;
    } Data;

    Muda_Token_Kind Kind;
} Muda_Token;

typedef struct Muda_Parser {
    uint8_t *Ptr;
    uint8_t *Pos;
    int64_t Length;

    Muda_Token Token;
} Muda_Parser;

INLINE Muda_Parser MudaParseInit(uint8_t *data, int64_t length) {
    Muda_Parser parser;
    parser.Ptr = data;
    parser.Pos = parser.Ptr;
    parser.Length = (length < 0) ? strlen((char *)data) : length;
    memset(&parser.Token, 0, sizeof(parser.Token));
    return parser;
}

INLINE bool MudaParseNext(Muda_Parser *p) {
    uint8_t *cur = p->Pos;
    while (*cur && isspace(*cur)) cur += 1;
    uint8_t *start = cur;
    uint8_t *start2;

    if (!*cur) {
        p->Pos = cur;
        return false;
    }
    else if (*cur == '[') {
        // Section
        cur ++;
        while (isspace(*cur)) cur ++;
        start = cur;
        while (*cur && (isalnum(*cur) || *cur > 125) && *cur != ']') cur += 1;
        while (*cur && isspace(*cur) && *cur != ']') cur += 1;

        if (*cur && *cur == ']') {
            cur --;
            while (isspace(*cur)) {
                (void) cur;
                cur --;
            }
            cur ++;
            if (*cur != ']')
                *cur = 0;
            p->Token.Kind = Muda_Token_Section;
            p->Token.Data.Section.Data = start;
            p->Token.Data.Section.Length = cur - start;
            while (*cur != ']') cur ++;
        } else {
            p->Token.Kind = Muda_Token_Error;
            p->Pos = cur;
            if (*cur)
                sprintf(p->Token.Data.Error.Desc, "Only Alpha-Numeric Characters Allowed for Identifiers");
            else
                sprintf(p->Token.Data.Error.Desc, "Missing ']'");
            return false;
        }
        p->Pos = cur + 1;

        return true;
    } else if (*cur == '#') {
        // Comment
        cur ++;
        start = cur;
        while (*cur && !(*cur == '\r' || *cur == '\n')) cur += 1;
        *cur = 0;
        p->Token.Kind = Muda_Token_Comment;
        p->Token.Data.Comment.Data = start;
        p->Token.Data.Comment.Length = cur - start;

        cur += 1;
        if (*cur && (*cur == '\r' || *cur == '\n')) cur += 1;
        p->Pos = cur;
        return true;
    } else if (isalnum(*cur) || *cur > 125) {
        // Property
        while (*cur && (isalnum(*cur) || *cur > 125) && *cur != '=') cur += 1;
        while (*cur && isspace(*cur) && *cur != '=') cur += 1;

        if (!*cur || *cur != '='){
            p->Token.Kind = Muda_Token_Error;
            p->Pos = cur;
            if (*cur)
                sprintf(p->Token.Data.Error.Desc, "Only Alpha-Numeric Characters Allowed for Identifiers");
            else
                sprintf(p->Token.Data.Error.Desc, "Property name without assignment");
            return false;
        }

        cur --;
        while (isspace(*cur)){
            (void) cur;
            cur --;
        }
        cur ++;
        if (*cur != '=')
            *cur = 0;

        p->Token.Kind = Muda_Token_Property;
        p->Token.Data.Property.Key.Data = start;
        p->Token.Data.Property.Key.Length = cur - start;
        while (*cur != '=') cur += 1;

        *cur = 0;
        cur ++;
        while (*cur && isspace(*cur)) cur ++;
        start2 = cur; 
        while (*cur && *cur != ';') cur += 1;
        if (*cur == ';'){
            cur --;
            while (isspace(*cur)){
                (void) cur;
                cur --;
            }
            cur ++;
            if (*cur != ';')
                *cur = 0;
            p->Token.Data.Property.Value.Data = start2;
            p->Token.Data.Property.Value.Length = cur - start2;
            while (*cur != ';') cur += 1;
        } else {
            p->Token.Kind = Muda_Token_Error;
            p->Pos = cur;
            sprintf(p->Token.Data.Error.Desc, "Missing ';'");
            return false;
        }
        *cur = 0;
        p->Pos = cur + 1;
        return true;
    } else {
        p->Pos = cur + 1;
        return true;
    }
}

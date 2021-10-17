#pragma once
#include "zBase.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define MudaParserReportError(p, ...) snprintf(p->Token.Data.Error.Desc, sizeof(p->Token.Data.Error.Desc), __VA_ARGS__)

typedef enum Muda_Token_Kind {
	Muda_Token_Config,
	Muda_Token_Section,
	Muda_Token_Property,
	Muda_Token_Comment,
	Muda_Token_Tag,
	Muda_Token_Error
}Muda_Token_Kind;

typedef struct Muda_Token {
	union {
		String Config;
		String Section;
		String Comment;
		struct {
			String Key;
			String Value;
		} Property;
		struct {
			char Desc[256];
			uint32_t Line, Column;
		}Error;
		struct {
			String Title;
			String Value;
		}Tag;
	} Data;

	Muda_Token_Kind Kind;
} Muda_Token;

typedef struct Muda_Parser {
	uint8_t *Ptr;
	uint8_t *Pos;

	Muda_Token Token;
} Muda_Parser;

INLINE_PROCEDURE Muda_Parser MudaParseInit(uint8_t *data) {
	Muda_Parser parser;
	parser.Ptr = data;
	parser.Pos = parser.Ptr;
	memset(&parser.Token, 0, sizeof(parser.Token));
	return parser;
}

INLINE_PROCEDURE void GetLineNoAndColumn(uint8_t *cur, Muda_Parser *p) {
	uint8_t *cpy = cur;
	p->Token.Data.Error.Column = 1;
	while (*cpy != '\n' && *cpy != '\r' && cpy > p->Ptr) {
		cpy--;
		p->Token.Data.Error.Column++;
	}

	cpy = cur;
	p->Token.Data.Error.Line = 1;
	while (cpy > p->Ptr + 1) {
		p->Token.Data.Error.Line += (*cpy == '\n' || *cpy == '\r');
		cpy--;
	}
}

INLINE_PROCEDURE bool MudaParseNext(Muda_Parser *p) {
	uint8_t *cur = p->Pos;
	while (*cur && isspace(*cur)) cur += 1;
	uint8_t *start = cur;
	uint8_t *start2;

	if (!*cur) {
		p->Pos = cur;
		return false;
	}
	else if (*cur == '[') {
		// Config
		cur++;
		while (isspace(*cur)) cur++;
		start = cur;
		while (*cur && (isalnum(*cur) || *cur > 125 || *cur == '-') && *cur != ']') cur += 1;
		while (*cur && isspace(*cur) && *cur != ']') cur += 1;

		if (*cur && *cur == ']') {
			cur--;
			while (isspace(*cur)) {
				(void)cur;
				cur--;
			}
			cur++;
			if (*cur != ']')
				*cur = 0;
			p->Token.Kind = Muda_Token_Config;
			p->Token.Data.Config.Data = start;
			p->Token.Data.Config.Length = cur - start;
			while (*cur != ']') cur++;
			*cur = 0;
		}
		else {
			p->Token.Kind = Muda_Token_Error;
			p->Pos = cur;
			if (*(cur - 1) != '\n' && *(cur - 1) != '\r') {
				GetLineNoAndColumn(cur, p);
				MudaParserReportError(p, "Only Alpha-Numeric Characters Allowed for Identifiers");
			}
			else {
				cur -= 2;
				GetLineNoAndColumn(cur, p);
				MudaParserReportError(p, "Missing ']'");
			}
			return false;
		}
		p->Pos = cur + 1;

		return true;
	}
	else if (*cur == '#') {
		// Comment
		cur++;
		start = cur;
		while (*cur && !(*cur == '\r' || *cur == '\n')) cur += 1;
		p->Token.Kind = Muda_Token_Comment;
		p->Token.Data.Comment.Data = start;
		p->Token.Data.Comment.Length = cur - start;

		cur += 1;
		if (*cur && (*cur == '\r' || *cur == '\n')) cur += 1;
		p->Pos = cur;
		return true;
	}
	else if (*cur == '@') {
		// Tag
		cur++;
		while (isspace(*cur)) cur++;
		start = cur;
		while (*cur && (isalnum(*cur) || *cur > 125)) cur += 1;

		if (!isspace(*cur)) {
			p->Token.Kind = Muda_Token_Error;
			p->Pos = cur;
			GetLineNoAndColumn(cur, p);
			MudaParserReportError(p, "Only Alpha-Numeric Characters Allowed for Tags");
			return 0;
		}

		p->Token.Kind = Muda_Token_Tag;
		p->Token.Data.Tag.Title.Data = start;
		p->Token.Data.Tag.Title.Length = cur - start;
		if (*cur != '\r' && *cur != '\n') {
			*cur = 0;
			cur++;
		}

		while (*cur && isspace(*cur) && *cur != '\n' && *cur != '\r') cur++;
		if (*cur == '\r' || *cur == '\n') {
			*cur = 0;
			p->Token.Data.Tag.Value.Data = NULL;
			p->Token.Data.Tag.Value.Length = 0;
		}
		else {
			p->Token.Data.Tag.Value.Data = cur;
			while (*cur && *cur != '\r' && *cur != '\n') cur++;
			*cur = 0;
			p->Token.Data.Tag.Value.Length = cur - p->Token.Data.Tag.Value.Data;
		}

		cur += 1;
		if (*cur && (*cur == '\r' || *cur == '\n')) cur += 1;
		p->Pos = cur;
		return true;
	}
	else if (*cur == ':') {
		// Section
		// TODO: Copy paste from Tags, may be merge?
		cur++;
		while (isspace(*cur)) cur++;
		start = cur;
		while (*cur && (isalnum(*cur) || *cur > 125 || *cur == '.')) cur += 1;

		if (!isspace(*cur)) {
			p->Token.Kind = Muda_Token_Error;
			p->Pos = cur;
			GetLineNoAndColumn(cur, p);
			MudaParserReportError(p, "Only Alpha-Numeric Characters and '.' allowed for Sections");
			return 0;
		}

		p->Token.Kind = Muda_Token_Section;
		p->Token.Data.Tag.Title.Data = start;
		p->Token.Data.Tag.Title.Length = cur - start;
		if (*cur != '\r' && *cur != '\n') {
			*cur = 0;
			cur++;
		}

		while (*cur && isspace(*cur) && *cur != '\n' && *cur != '\r') cur++;
		if (*cur == '\r' || *cur == '\n') {
			*cur = 0;
			p->Token.Data.Tag.Value.Data = NULL;
			p->Token.Data.Tag.Value.Length = 0;
		}
		else {
			p->Token.Data.Tag.Value.Data = cur;
			while (*cur && *cur != '\r' && *cur != '\n') cur++;
			*cur = 0;
			p->Token.Data.Tag.Value.Length = cur - p->Token.Data.Tag.Value.Data;
		}

		cur += 1;
		if (*cur && (*cur == '\r' || *cur == '\n')) cur += 1;
		p->Pos = cur;
		return true;
	}
	else if (isalnum(*cur) || *cur > 125) {
		// Property
		while (*cur && (isalnum(*cur) || *cur > 125) && *cur != '=' && *cur != ':' && *cur != ';') cur += 1;
		while (*cur && isspace(*cur) && *cur != '=' && *cur != ':' && *cur != ';') cur += 1;

		if (*cur != ':' && *cur != '=') {
			p->Token.Kind = Muda_Token_Error;
			p->Pos = cur;
			GetLineNoAndColumn(cur, p);
			if (*cur && *cur != ';')
				MudaParserReportError(p, "Only Alpha-Numeric Characters Allowed for Identifiers");
			else
				MudaParserReportError(p, "Property name without assignment");
			return false;
		}

		cur--;
		while (isspace(*cur)) {
			(void)cur;
			cur--;
		}
		cur++;
		if (*cur != '=' && *cur != ':')
			*cur = 0;

		p->Token.Kind = Muda_Token_Property;
		p->Token.Data.Property.Key.Data = start;
		p->Token.Data.Property.Key.Length = cur - start;
		while (*cur != '=' && *cur != ':') cur += 1;

		/* *cur = 0; */
		cur++;
		while (*cur && isspace(*cur)) cur++;
		start2 = cur;
		while (*cur && *cur != ';' && *cur != '=' && *cur != ':' && *cur != '[' && *cur != ']') cur += 1;
		if (*cur == ';') {
			cur--;
			while (isspace(*cur)) {
				(void)cur;
				cur--;
			}
			cur++;
			// TODO(zero): I don't know why this is here, but this produces 
			// p->Token.Data.Property.Value.Length -1, which is not correct
			// when the value is empty, then length of the string needs to be 0
			// I am not removing this code for now, cuz it might break other things
			if (*cur != ';')
				*cur = 0;
			p->Token.Data.Property.Value.Data = start2;
			p->Token.Data.Property.Value.Length = cur - start2;
			while (*cur != ';') cur += 1;
		}
		else {
			p->Token.Kind = Muda_Token_Error;
			cur--;
			for (; *cur != '=' && *cur != ':' && cur > p->Ptr; cur--);
			while (*cur && *cur != '\r' && *cur != '\n') cur++;
			cur--;
			GetLineNoAndColumn(cur, p);
			p->Token.Data.Error.Line++;
			MudaParserReportError(p, "Missing ';'");
			return false;
		}
		*cur = 0;
		p->Pos = cur + 1;
		return true;
	}
	else {
		p->Token.Kind = Muda_Token_Error;
		p->Pos = cur;
		GetLineNoAndColumn(cur, p);
		MudaParserReportError(p, "Bad character, unrecognized");
		return false;
	}
}

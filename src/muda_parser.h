
#pragma once
#include "zBase.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

// For now
#include <stdlib.h>

#define MudaParserReportError(p, ...) snprintf(p->Token.Data.Error.Desc, sizeof(p->Token.Data.Error.Desc), __VA_ARGS__)

typedef enum Muda_Token_Kind {
	Muda_Token_Config,
	Muda_Token_Section,
	Muda_Token_Property,
	Muda_Token_Comment,
	Muda_Token_Tag,
	Muda_Token_Error
}Muda_Token_Kind;

/* typedef enum */
/* { */
/*     Muda_Token_Property_Null, */
/*     Muda_Token_Property_Single, */
/*     Muda_Token_Property_Multiple */
/* } Muda_Token_Property_Types; */

typedef struct Muda_Token {
	union {
		String Config;
		String Section;
		String Comment;
		struct {
			String  Key;
			String *Value;
		        Int64    Count;
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

        uint32_t line;
        uint8_t* line_ptr;
	Muda_Token Token;
} Muda_Parser;

#define IgnoreSpaces(ptr) while(*ptr && isspace(*ptr) && *ptr != '\n') ptr++; // Don't consume newline character .. important for error detection and line information

static uint8_t special_symbols[] = {';','[',']',':','@',' ','\n', '#', '\0'};

bool isSpecial(uint8_t ch)
{
  for (long long unsigned i = 0; i < ArrayCount(special_symbols); ++i)
    if (special_symbols[i] == ch)
      return true;
  return false;
}


INLINE_PROCEDURE Muda_Parser MudaParseInit(uint8_t *data) {
        Muda_Parser parser = {0};
	parser.Ptr = data;
	parser.Pos = parser.Ptr;
	parser.line = 1;
	parser.line_ptr = data; 
	memset(&parser.Token, 0, sizeof(parser.Token));
	return parser;
}

static String GetNextToken(uint8_t* cur,Muda_Parser* p)
{
  IgnoreSpaces(cur);
  uint8_t* hold = cur;
  if (isSpecial(*cur))
    return (String){.Data = cur, .Length = cur - hold};

  // else return strings
  while(!isSpecial(*cur) && *cur) ++cur;
  return (String){.Data = hold, .Length = cur-hold};

  // should we be handling quoted strings differently ? ???? 
}

bool MudaParseKeyValue(uint8_t* cur, Muda_Parser* p)
{
  // Property -> Key : Value
  uint8_t* begin = cur;
  
  String id = GetNextToken(cur,p);

  // First read the key value idiot -> me
  p->Token.Kind = Muda_Token_Property;
  p->Token.Data.Property.Key.Data = id.Data;
  p->Token.Data.Property.Key.Length = id.Length;

  id = GetNextToken(id.Data + id.Length + 1, p);

  Assert(*id.Data == ':');

  uint8_t* save = id.Data;
  
  id = GetNextToken(id.Data + 1, p);
  // New line error detection : TODO 
  if(id.Length == 0 && *id.Data == ';')
  {
    // Null property .. Property with no body
    p->Token.Data.Property.Value = NULL;
    p->Token.Data.Property.Count = 0;
    p->Pos = id.Data + 1;
    return true;
  }

  // Else, its either single, multiple or error
  uint8_t* temp = cur;
  // A sophiscated error handling here
  // 
  // Count total number of valid values
  int64_t count_values = 0;
  while(!isSpecial(*id.Data) || (*id.Data == '\n'))
  {
    int64_t inc; 
    if (*id.Data == '\n') {
      p->line_ptr = id.Data + 1;
      p->line++;
      inc = 1; 
    }
    else
    {
      count_values++;
      inc = id.Length;
    }
    id = GetNextToken(id.Data+inc,p);
  }
  
  if(*id.Data != ';')
  {
    p->Token.Kind = Muda_Token_Error;
    p->Token.Data.Error.Line = p->line;
    p->Token.Data.Error.Column = (uint32_t)(id.Data - p->line_ptr);
    MudaParserReportError(p,"Expected ;");
    return false; 
  }

  p->Token.Data.Property.Count = count_values;
  p->Pos = id.Data + 1; 

  if (!count_values)
     return true; 

  //Todo(Tilak) :->  Replace it with temporary memory
  p->Token.Data.Property.Value = (String*) malloc(sizeof(String)*count_values);

  
  id = GetNextToken(save+1,p);

  int64_t values = 0;

  while(!isSpecial(*id.Data) || (*id.Data == '\n'))
  {
    int64_t inc; 
    if (*id.Data == '\n') {
      // Do nothing
      inc = 1; 
    }
    else 
    {
      p->Token.Data.Property.Value[values++] = id;
      Assert(values<=count_values);
      inc = id.Length;
    }
    id = GetNextToken(id.Data+inc,p);
    // LogWarn("Parsing iteratively");
  }
  
  /* if (id.Length != 0) */
  /* { */
  /*   temp = id.Data + id.Length; */
  /*   String peek = GetNextToken(temp,p); */

  /*   if (peek.Length == 0 && *peek.Data == ';') */
  /*   { */
  /*     // If new line, issue warning  */
  /*     // single worded property value */
  /*     p->Token.Data.Property.Count = 1; */
      
  /*     p->Token.Data.Property.Value = (String*) malloc(sizeof(String)); // Consumer must free this space ..  */
      
  /*     p->Token.Data.Property.Value->Data = id.Data; */
  /*     p->Token.Data.Property.Value->Length = id.Length; */
  /*     //  p->Token.Data.Property.Count = Muda_Token_Property_Single;  */
  /*     p->Pos = peek.Data + 1; */
  /*     // LogWarn("\n\nSingle property \n\n"); */
  /*   } */
  /*   else */
  /*   { */
  /*     // Multiple worded properties */
  /*     // Repeatedly consume properties */
  /*     // Should it be sent in a single buffer or multiple buffers ? */

  /*     /\** */
  /*      // Single Buffer */
  /*      while(!(peek.Length == 0 && *peek.Data == ';')) */
  /*      { */
  /*      // consume even new line here */
  /*      temp = peek.Data + peek.Length; */
  /*      peek = GetNextId(temp,p); */
  /*      } */
      
  /*      p->Token.Data.Property.Value.Data = id.Data; */
  /*      p->Token.Data.Property.Value.Length = temp - id.Data; */
  /*      p->Token.Data.Property.Type = Muda_Token_Property_Multiple; */
  /*      p->Pos = peek.Data + 1; */
  /*     **\/ */
      
  /*     // Multiple buffer */
      
  /*     int max_allocated = 25; // Pre allocate memory for 25 values */
  /*     //      <------- Might need to use arena allocator here ------->  */
  /*     p->Token.Data.Property.Value = (String*)malloc(sizeof(String) * max_allocated);  */
  /*     p->Token.Data.Property.Count = 1; */

  /*     p->Token.Data.Property.Value->Data = id.Data; */
  /*     p->Token.Data.Property.Value->Length = id.Length; */

  /*     // Error handling omitted for now */
      
  /*     while(!(peek.Length == 0 && *peek.Data == ';')) */
  /*     { */
  /* 	Assert(p->Token.Data.Property.Count < max_allocated - 1); */

  /* 	p->Token.Data.Property.Value[p->Token.Data.Property.Count].Data = peek.Data; */
  /* 	p->Token.Data.Property.Value[p->Token.Data.Property.Count].Length = peek.Length; */
  /* 	p->Token.Data.Property.Count++; */

  /* 	temp = peek.Data + peek.Length; */
  /* 	peek = GetNextToken(temp,p); */
  /*     } */
  /*     p->Pos = peek.Data + 1;  */
  /*     LogWarn("\n\nMultiple property \n\n"); */
  /*   } */
  //  return true;
  //  }
  return true; 
  
}




INLINE_PROCEDURE bool MudaParseNext(Muda_Parser* p)
{
  uint8_t *cur = p->Pos;
  uint8_t *hold = cur;

  if(!*cur) {
    p->Pos = cur;
    return false;
  }

  String token = GetNextToken(cur,p);

  while(*token.Data == '\n' && token.Length == 0)
  {
    p->line++;
    p->line_ptr = token.Data + 1;
    token = GetNextToken(token.Data+1,p);
  }

  if (*token.Data == '\0')
  {
    p->Pos = token.Data;
    return false;
  }
  
  if (*token.Data == '[')
  {
    // Start of the [Config] Section
    token = GetNextToken(token.Data+1,p);
    if (*token.Data == ']')
    {
      LogWarn("Empty [Config] Section");
      p->Pos = token.Data + 1; 
      return true;
      // Or it could be reported as Error?? 
      /*
      p->Token.Kind = Muda_Token_Error;
      p->Token.Data.Error.Line = p->line;
      p->Token.Data.Error.Column = token.Data - p->line_ptr;
      MudaParserReportError(p,"Empty [Config Section]");
      return false;
      */
    }
    if (token.Length!=0 && *token.Data != ']')
    {
      // Valid config name
      String peek = GetNextToken(token.Data + token.Length,p);
      while(*peek.Data == ' ')
	peek = GetNextToken(peek.Data+1,p);
      
      if (*peek.Data == ']')
      {
	// Valid config with end closed 
	p->Token.Kind = Muda_Token_Config;
	p->Token.Data.Config.Data = token.Data;
	p->Token.Data.Config.Length = token.Length;
	//      p->Pos = token.Data + token.Length + 1;
	p->Pos = peek.Data + 1;
	return true;
      }
      else if (*peek.Data == '\n')
      {
	p->Token.Data.Error.Line   = p->line;
	p->Token.Data.Error.Column = (uint32_t)(peek.Data - p->line_ptr); 
	MudaParserReportError(p,"Expected ] here ... ");
	p->Token.Kind = Muda_Token_Error;
	// LogWarn("Expected ] here Line : %d. Column %d.\n Discontinuing further parsing ",p->line,peek.Data - p->line_ptr);
	p->Pos = peek.Data; // Not consuming the new line 
	return false; 
      }
    }
    return false; 
  }

  if (*token.Data == '#')
  {
    // Start of the comment section
    p->Token.Kind = Muda_Token_Comment;
    String peek = GetNextToken(token.Data+1,p);
    while(*peek.Data != '\n' && *peek.Data != '\0')
      peek = GetNextToken(peek.Data+1,p);
    if (*peek.Data == '\n')
    {
      p->line++;
      p->line_ptr = peek.Data+1;
    }
    p->Pos = peek.Data + 1;
    return true; 
  }

  if (*token.Data == '@')
  {
    // Start of the tag section
    // Tag has title and value
    String peek = GetNextToken(token.Data+1,p);
    if (peek.Length == 0) // special characters
    {
      MudaParserReportError(p,"Unexpected symbol in @version thingy");
      return false; 
    }
    // else it is the title of the tag
    // Do we need to iteratre over title ??? to only allow alpha numeric character ? Later ..
    p->Token.Kind = Muda_Token_Tag;
    p->Token.Data.Tag.Title.Data = peek.Data;
    p->Token.Data.Tag.Title.Length = peek.Length;

    //  Find the value now
    peek = GetNextToken(peek.Data + peek.Length + 1, p);
    if (peek.Length == 0) // Again special characters
    {
      if (*peek.Data == '\n')
      {
	// p->Token.Data.Tag.Valid.Data = NULL;
	// p->Token.Data.Tag.Valid.Length = 0;
	MudaParserReportError(p,"Unexpected newline here ... Requires literal");
	return false;
      }
      MudaParserReportError(p,"Failed to parse");
      return false; 
    }

    // else it is the value of the tag
    p->Token.Data.Tag.Value.Data = peek.Data;
    p->Token.Data.Tag.Value.Length = peek.Length;
    p->Pos = peek.Data + peek.Length; 
    return true; 
  }

  if (*token.Data == ':')
  {
    // Section section
    // Ignore spaces ? Or shouldn't be ignoring?
    String peek = GetNextToken(token.Data + 1,p);
    if (peek.Length == 0)
    {
      // Special symbols
      p->Token.Kind = Muda_Token_Error;
      p->Pos = peek.Data;
      MudaParserReportError(p, "Empty section name");
      return false; 
    }
    p->Token.Kind = Muda_Token_Section;
    p->Token.Data.Section.Data = peek.Data;
    p->Token.Data.Section.Length = peek.Length;
    p->Pos = peek.Data + peek.Length;
    return true; 
  }

  if (isalnum(*token.Data))
  {
    return MudaParseKeyValue(token.Data,p);
  }

  p->Token.Kind = Muda_Token_Error;
  p->Pos = token.Data;
  p->Token.Data.Error.Line = p->line;
  p->Token.Data.Error.Column = (uint32_t)(token.Data - p->line_ptr);
  MudaParserReportError(p, "Bad character, unrecognized");
  return false; 
}

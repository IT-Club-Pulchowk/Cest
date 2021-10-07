#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "zBase.h"
#include "zBaseCRT.h"
#include "muda_parser.h"

typedef enum Compile_Type {
	Compile_Type_Project,
	Compile_Type_Solution,
} Compile_Type;

typedef struct Compiler_Config {
	Compile_Type Type;
	String BuildDirectory;
	String Build;
	String *Defines;
	Uint32 DefineCount;
	String *Source;
	Uint32 SourceCount;
	String *IncludeDirectory;
	Uint32 IncludeDirectoryCount;
	String *LiraryDirectory;
	Uint32 LibraryDirectoryCount;
	String *Library;
	Uint32 LibraryCount;
	bool Optimization;
} Compiler_Config;

int main(){
    InitThreadContextCrt(MegaBytes(512));
    Memory_Arena *scratch = ThreadScratchpad();
    FILE *fp = fopen("sample.muda", "r");
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    rewind(fp);
    char *content;
    fread(content, sizeof(char), size, fp);
    fclose(fp);
    Muda *dat = Muda_Init(content);

    for(int i = 0; i < dat->sec_count; i++){
        printf("--------------------%s--------------------\n", dat->sections[i].name.Data);
        for(int j = 0; j < dat->prop_count; j++){
            if (dat->properties[j].section == i)
                printf("\t%s : %s\n", dat->properties[j].name.Data, dat->properties[j].value.Data);
        }
    }
}

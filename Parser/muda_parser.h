#pragma once
#include "zBase.h"
#include <string.h>
#define INITIAL_CAP 64

/*
 * Modified off of:
 * https://github.com/mattiasgustavsson/libs/blob/main/ini.h
 */

//TODO : implement getting / setting values
//       maybe add global section

typedef struct{
    int section;
    String name;
    union{
        String value;
        String *values; // TODO: implement parsing lists
    };
}Muda_Property;

typedef struct{
    String name;
}Muda_Section;

typedef struct{
    Muda_Section *sections;
    int sec_count, sec_cap;
    Muda_Property *properties;
    int prop_count, prop_cap;
}Muda;

void Muda_Add_Section(Muda *dst, const char *sec_name, int length){
    Memory_Arena *scratch = ThreadScratchpad();
    if (dst->sec_count >= dst->sec_cap){
        dst->sec_cap += 1;
        Muda_Section *temp = (Muda_Section *)PushSize(scratch, sizeof(Muda_Section) * dst->sec_cap);
        memcpy(temp, dst->sections, sizeof(Muda_Section) * dst->sec_cap);
        dst->sections = temp;
    }
    dst->sections[dst->sec_count].name.Length = length + 1;
    dst->sections[dst->sec_count].name.Data = (Uint8 *)PushSize(scratch, (length + 1) * sizeof(Uint8));
    memcpy(dst->sections[dst->sec_count].name.Data, sec_name, length);
    dst->sections[dst->sec_count].name.Data[length] = 0;
    dst->sec_count ++;
}

void Muda_Add_Property(Muda *dst, const char *name, int l_name, const char *value, int l_value){
    Memory_Arena *scratch = ThreadScratchpad();
    if (dst->prop_count >= dst->prop_cap){
        dst->prop_cap += 1;
        Muda_Property *temp = (Muda_Property *)PushSize(scratch, sizeof(Muda_Property) * dst->prop_cap);
        memcpy(temp, dst->properties, sizeof(Muda_Property) * dst->prop_cap);
        dst->properties = temp;
    }
    dst->properties[dst->prop_count].name.Length = l_name + 1;
    dst->properties[dst->prop_count].name.Data = (Uint8 *)PushSize(scratch, (l_name + 1) * sizeof(Uint8));
    memcpy(dst->properties[dst->prop_count].name.Data, name, l_name);
    dst->properties[dst->prop_count].name.Data[l_name] = 0;
    dst->properties[dst->prop_count].value.Length = l_value + 1;
    dst->properties[dst->prop_count].value.Data = (Uint8 *)PushSize(scratch, (l_value + 1) * sizeof(Uint8));
    memcpy(dst->properties[dst->prop_count].value.Data, value, l_value);
    dst->properties[dst->prop_count].value.Data[l_value] = 0;
    dst->prop_count ++;
}

Muda* Muda_Init(const char* data){
    if (!data){
        LogError("No Data\n");
        return NULL;
    }
    Memory_Arena *scratch = ThreadScratchpad();
    Muda * muda = (Muda *)PushSize(scratch, sizeof(Muda));
    muda->sec_cap = INITIAL_CAP;
    muda->sec_count = 0;
    muda->sections = (Muda_Section *) PushSize(scratch, sizeof(Muda_Section) * INITIAL_CAP);
    muda->prop_cap = INITIAL_CAP;
    muda->prop_count = 0;
    muda->properties = (Muda_Property *) PushSize(scratch, sizeof(Muda_Property) * INITIAL_CAP);

    const char * ptr = data;
    int sec_count = 0;

    while (*ptr){
        for (; *ptr && *ptr <= ' '; ptr ++);
        if (!*ptr) break;
        else if (*ptr == '#')
            for (;*ptr && *ptr != '\n'; ptr ++);
        else if (*ptr == '['){
            ptr ++;
            const char *start = ptr;
            int len = 0;
            while (*ptr && *ptr !=']'){
                if (*ptr == '\n'){
                    LogWarn("Missing ']'");
                    break;
                }
                len++;
                ptr++;
            }
            if (*ptr == ']'){
                Muda_Add_Section(muda, start, len);
                sec_count ++;
                for (; *ptr != '\n'; ptr ++);
                ptr ++;
            }
        }
        else if (sec_count){
            int name_len = 0;
            const char *name = ptr;
            while(*ptr && *ptr != ':'){
                if (*ptr == '\n'){
                    LogWarn("Unassigned property name\n");
                    break;
                }
                ptr ++;
                name_len ++;
            }

            if (*ptr == ':'){
                ptr ++;
                for (;*ptr && *ptr <= ' ' && *ptr != '\n'; ptr ++);
                const char *val = ptr;
                int val_len = 0;
                for (; *ptr != '\n' && *ptr != '#'; ptr ++) val_len ++;
                while (*(--ptr) <= ' '){
                    val_len --;
                    (void) ptr;
                }
                if (*ptr == '#')
                    for (; *ptr != '\n'; ptr ++);
                ptr ++;
                Muda_Add_Property(muda, name, name_len, val, val_len + 1);
            }
        }
    }
    return muda;
}

#!/bin/bash

SOURCEFILES=../main.c
OUTPUTFILE=main.out
GCCFLAGS="-g"
CLANGFLAGS="-gcodeview -Od"

if [ ! -d "./bin" ]; then
    mkdir bin
fi

if [ "$1" == "optimize" ]; then
    GCCFLAGS="-O2"
    CLANGFLAGS="-O2 -gcodeview"
fi

echo ------------------------------
echo Building With GCC
echo ------------------------------
if command -v gcc &> /dev/null
then
    pushd bin
    gcc -DASSERTION_HANDLED -DDEPRECATION_HANDLED -Wno-switch -Wno-pointer-sign -Wno-enum-conversion -Wno-pointer-to-int-cast $GCCFLAGS $SOURCEFILES -o $OUTPUTFILE
    popd
    exit
else
    echo GCC Not Found
    echo ------------------------------
fi

echo ------------------------------
echo Building With Clang
echo ------------------------------
if command -v clang &> /dev/null
then
    pushd bin
    clang -DASSERTION_HANDLED -DDEPRECATION_HANDLED -Wno-switch -Wno-pointer-sign -Wno-enum-conversion -Wno-void-pointer-to-int-cast $SOURCEFILES $COMPILERFLAGS -o $OUTPUTFILE
    popd
    exit
else
    echo Clang Not Found
    echo ------------------------------
fi

@echo off

setlocal

set OutputDirectory=release
set OutputBinary=muda.exe
set SourceFiles=../src/build.c

set MsBuildConfiguration=Debug
set CLFlags=-Od
set CLANGFlags=-g -gcodeview
set GCCFlags=-O

if "%1" neq "optimize" goto DoneConfig
set MsBuildConfiguration=Release
set CLFlags=-O2
set CLANGFlags=-O2 -gcodeview
set GCCFlags=-O2

echo -------------------------------------
echo Optimize Build configured
echo -------------------------------------
:DoneConfig

echo -------------------------------------

if not exist %OutputDirectory% mkdir %OutputDirectory%
pushd %OutputDirectory%

:MSVC
where cl >nul 2>nul
if %ERRORLEVEL% neq 0 goto SkipMSVC
where rc >nul 2>nul
if %ERRORLEVEL% neq 0 goto SkipMSVC
echo Building with Msvc
call cl -nologo -W3 -DASSERTION_HANDLED -DDEPRECATION_HANDLED -D_CRT_SECURE_NO_WARNINGS %SourceFiles% %CompilerFlags% -EHsc -Fe%OutputBinary%
echo -------------------------------------
goto Finished
:SkipMSVC
echo Msvc not found. Skipping build with Msvc
echo -------------------------------------

:CLANG
where clang >nul 2>nul
IF %ERRORLEVEL% NEQ 0 goto SkipCLANG
where llvm-rc >nul 2>nul
IF %ERRORLEVEL% NEQ 0 goto SkipCLANG
echo Building with CLANG
call clang -DASSERTION_HANDLED -DDEPRECATION_HANDLED -Wno-switch -Wno-pointer-sign -Wno-enum-conversion -D_CRT_SECURE_NO_WARNINGS %SourceFiles% %CompilerFlags% -o %OutputBinary% -lShlwapi.lib -lUserenv.lib -lAdvapi32.lib
echo -------------------------------------
goto Finished
:SkipCLANG
echo Clang not found. Skipping build with Clang
echo -------------------------------------

:GCC
where gcc >nul 2>nul
IF %ERRORLEVEL% NEQ 0 goto SkipGCC
where windres >nul 2>nul
IF %ERRORLEVEL% NEQ 0 goto SkipGCC
echo Building with GCC
call gcc -DASSERTION_HANDLED -DDEPRECATION_HANDLED -Wno-switch -Wno-pointer-sign -Wno-enum-conversion %SourceFiles% %CompilerFlags% -o %OutputBinary%  -lShlwapi -lUserenv -lAdvapi32
echo -------------------------------------
goto Finished
:SkipGCC
echo Gcc not found. Skipping build with Gcc
echo -------------------------------------

:Finished
popd

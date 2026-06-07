@echo off
if not exist build\fernbuffer.lib (
    echo ERROR: build\fernbuffer.lib not found. Run 'make dll' first.
    exit /b 1
)

set "PFILES=%ProgramFiles(x86)%"
for /f "usebackq tokens=*" %%i in (`"%PFILES%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
    echo ERROR: Visual Studio not found. Install VS 2017+ with "Desktop development with C++".
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1

cl /nologo /O2 /W3 /EHsc /std:c++17 /DFERNBUFFER_IMPORT_DLL /I. test\cli.cpp /Fo:build\ build\fernbuffer.lib /Fe:build\fernbuffer_cli.exe

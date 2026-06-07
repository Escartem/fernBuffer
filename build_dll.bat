@echo off
if not exist build mkdir build

rem Capture ProgramFiles(x86) into a plain variable first.
rem This works around a CMD parser bug where %ProgramFiles(x86)% fails
rem to expand inside a for /f backtick command due to the parentheses.
set "PFILES=%ProgramFiles(x86)%"

for /f "usebackq tokens=*" %%i in (`"%PFILES%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSDIR=%%i

if not defined VSDIR (
    echo ERROR: Visual Studio not found. Install VS 2017+ with "Desktop development with C++".
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1

cl /nologo /O2 /W3 /LD /EHsc /std:c++17 fernbuffer.cpp /Fo:build\ /Fe:build\fernbuffer.dll /link /IMPLIB:build\fernbuffer.lib

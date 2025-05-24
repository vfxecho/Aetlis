@echo off
setlocal enabledelayedexpansion

echo Building uSockets library...

REM Check for Visual Studio tools
where cl.exe >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Visual Studio tools not found in PATH.
    echo Please run this script from a Visual Studio developer command prompt.
    exit /b 1
)

REM Create directory for object files
if not exist "uWebSockets-0.17.0\uSockets\obj" mkdir "uWebSockets-0.17.0\uSockets\obj"

REM Compile each C file in uSockets
set "INCLUDE_DIR=%cd%\uWebSockets-0.17.0\uSockets\src"
set "OBJDIR=%cd%\uWebSockets-0.17.0\uSockets\obj"
set "SOURCES="

echo Compiling uSockets C files...

for %%f in (uWebSockets-0.17.0\uSockets\src\*.c) do (
    echo Compiling %%~nxf...
    cl.exe /c /I"%INCLUDE_DIR%" /DLIBUS_NO_SSL /Fo"%OBJDIR%\%%~nf.obj" "%%f"
    if !ERRORLEVEL! neq 0 goto error
    set "SOURCES=!SOURCES! %OBJDIR%\%%~nf.obj"
)

REM Compile eventing files
for %%f in (uWebSockets-0.17.0\uSockets\src\eventing\*.c) do (
    echo Compiling eventing\%%~nxf...
    cl.exe /c /I"%INCLUDE_DIR%" /DLIBUS_NO_SSL /Fo"%OBJDIR%\%%~nf.obj" "%%f"
    if !ERRORLEVEL! neq 0 goto error
    set "SOURCES=!SOURCES! %OBJDIR%\%%~nf.obj"
)

REM Create static library
echo Creating uSockets.lib...
lib.exe /OUT:uSockets.lib %SOURCES%
if %ERRORLEVEL% neq 0 goto error

REM Copy header files to include directory
if not exist "include" mkdir "include"
copy "uWebSockets-0.17.0\uSockets\src\libusockets.h" "include\"

REM Copy header files from uWebSockets to a central location
if not exist "include\uwebsockets" mkdir "include\uwebsockets"
for %%f in (uWebSockets-0.17.0\src\*.h) do (
    copy "%%f" "include\uwebsockets\"
)

echo uSockets library built successfully!
echo Library saved to: %cd%\uSockets.lib
echo Headers saved to: %cd%\include
exit /b 0

:error
echo Error building uSockets library
exit /b 1 
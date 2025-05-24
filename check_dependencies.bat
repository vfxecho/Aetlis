@echo off
echo Checking uWebSockets and uSockets dependencies...

echo.
echo Checking for uSockets library...
if exist "vcpkg\installed\x64-windows\lib\uSockets.lib" (
    echo [OK] uSockets.lib found in vcpkg\installed\x64-windows\lib
) else if exist "uSockets.lib" (
    echo [OK] uSockets.lib found in project root
) else (
    echo [ERROR] uSockets.lib not found!
    echo You need to build uSockets or install it via vcpkg.
    exit /b 1
)

echo.
echo Checking for libuv library...
if exist "vcpkg\installed\x64-windows\lib\uv.lib" (
    echo [OK] uv.lib found in vcpkg\installed\x64-windows\lib
) else (
    echo [ERROR] uv.lib not found!
    echo You need to install libuv via vcpkg:
    echo vcpkg install libuv:x64-windows
    exit /b 1
)

echo.
echo Checking for uWebSockets header files...
if exist "uWebSockets-0.17.0\src\App.h" (
    echo [OK] uWebSockets headers found in uWebSockets-0.17.0\src
) else (
    echo [WARNING] uWebSockets headers not found in the expected location
    echo If you have the headers in another location, please update the include paths.
)

echo.
echo Checking for ws2_32 library (Windows Sockets)...
echo [INFO] ws2_32.lib is part of Windows SDK and should be available.

echo.
echo All dependencies appear to be available.
echo You should be able to build the project now.

echo.
echo If you still encounter build errors:
echo 1. Make sure Visual Studio is properly configured to find the dependencies.
echo 2. Check that all include paths and library paths are correct in the project settings.
echo 3. Ensure you have all the necessary headers copied to the include folder.
echo.

exit /b 0 
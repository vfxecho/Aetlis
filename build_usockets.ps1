# Navigate to the uSockets directory
Set-Location -Path "uWebSockets-0.17.0"

# Check if git is available
if (!(Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host "Error: git is not installed or not in the PATH."
    exit 1
}

# Check if git submodule is initialized
if (-not (Test-Path -Path "uSockets" -PathType Container)) {
    Write-Host "Initializing git submodule..."
    git submodule update --init --recursive
}

# Navigate to uSockets directory
Set-Location -Path "uSockets"

# For Windows, we'll use Visual Studio to build
Write-Host "Building uSockets..."
if (Test-Path "Makefile") {
    # Check if make is available
    if (Get-Command mingw32-make -ErrorAction SilentlyContinue) {
        mingw32-make
    } elseif (Get-Command make -ErrorAction SilentlyContinue) {
        make
    } else {
        Write-Host "Warning: Neither make nor mingw32-make found. Will create a simple static library manually."
        
        # Manual build for Windows (basic approach)
        # Create a obj directory if it doesn't exist
        if (-not (Test-Path -Path "obj" -PathType Container)) {
            New-Item -Path "obj" -ItemType Directory
        }
        
        # Compile the C files
        $cfiles = Get-ChildItem -Path "src" -Filter "*.c" -Recurse
        foreach ($file in $cfiles) {
            Write-Host "Compiling $($file.FullName)..."
            cl.exe /c /I"src" /DLIBUS_NO_SSL /Fo"obj\$($file.BaseName).obj" $file.FullName
        }
        
        # Create static library
        Write-Host "Creating static library..."
        $objfiles = Get-ChildItem -Path "obj" -Filter "*.obj"
        lib.exe /OUT:uSockets.lib $objfiles
    }
}

# Check if library was built
if (Test-Path -Path "uSockets.lib" -PathType Leaf) {
    Copy-Item -Path "uSockets.lib" -Destination "../../" -Force
    Write-Host "uSockets library built and copied successfully!"
} elseif (Test-Path -Path "libuSockets.a" -PathType Leaf) {
    Copy-Item -Path "libuSockets.a" -Destination "../../" -Force
    Write-Host "uSockets library built and copied successfully!"
} else {
    Write-Host "Error: Failed to build uSockets library"
    exit 1
}

# Return to original directory
Set-Location -Path "../.."

# Now copy all the header files from uWebSockets-0.17.0/src to uwebsockets/ folder
if (-not (Test-Path -Path "uwebsockets" -PathType Container)) {
    New-Item -Path "uwebsockets" -ItemType Directory
}

$headerFiles = Get-ChildItem -Path "uWebSockets-0.17.0/src/*.h"
foreach ($file in $headerFiles) {
    Copy-Item -Path $file.FullName -Destination "uwebsockets/" -Force
}

Write-Host "Header files copied to uwebsockets/ directory" 
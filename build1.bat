@echo off
echo =========================================
echo   VTape24 - Build Script (Windows)
echo =========================================
echo.

REM Check if JUCE exists
if not exist "JUCE\" (
    echo WARNING: JUCE not found. Cloning JUCE framework...
    git clone https://github.com/juce-framework/JUCE.git
    echo JUCE cloned successfully (latest version, JUCE 8 compatible)
) else (
    echo JUCE found
)

echo Cleaning old build folder...
if exist build (
    rmdir /s /q build
)

REM Create build directory
if not exist "build\" (
    mkdir build
    echo Build directory created
)

cd build

echo.
echo Generating build files for Visual Studio...
cmake .. -G "Visual Studio 18 2026"

if %errorlevel% neq 0 (
    echo.
    echo ERROR: CMake generation failed!
    echo Make sure you have Visual Studio 2019 installed with C++ tools
    pause
    exit /b 1
)

echo.
echo Build files generated successfully
echo.
echo Building...
cmake --build . --config Release

if %errorlevel% neq 0 (
    echo.
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo =========================================
echo   Build Complete!
echo =========================================
echo.
echo Run with: VTape24_artefacts\Release\VTape24.exe
echo.
pause

@echo off
setlocal EnableDelayedExpansion
title Build Hawthorne
color 0E
echo.
echo  =============================================
echo     HAWTHORNE - build it on this PC
echo  =============================================
echo.
echo  Just leave this window open. The first time it installs
echo  the tools it needs (about 20-40 minutes), after that a
echo  build takes a few minutes. Hawthorne opens when it's done.
echo.

set "WORK=%USERPROFILE%\HawthorneBuild"
set "SRC=%WORK%\hawthorne"
set "PATH=%ProgramFiles%\Git\cmd;%ProgramFiles%\CMake\bin;%PATH%"

REM ---------------------------------------------------------------- tools
where winget >nul 2>&1
if errorlevel 1 (
    echo  This PC doesn't have "winget". Install "App Installer" from the
    echo  Microsoft Store, then run this again.
    pause & exit /b 1
)

where git >nul 2>&1
if errorlevel 1 (
    echo  [1/5] Installing Git...
    winget install -e --id Git.Git --silent --accept-package-agreements --accept-source-agreements
) else ( echo  [1/5] Git is installed )

where cmake >nul 2>&1
if errorlevel 1 (
    echo  [2/5] Installing CMake...
    winget install -e --id Kitware.CMake --silent --accept-package-agreements --accept-source-agreements
) else ( echo  [2/5] CMake is installed )

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "HAVEVS="
if not exist "%VSWHERE%" goto novswhere
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "HAVEVS=%%i"
:novswhere
if defined HAVEVS goto havevs
echo  [3/5] Installing the Microsoft C++ compiler ^(big download, be patient^)...
winget install -e --id Microsoft.VisualStudio.2022.BuildTools --silent --accept-package-agreements --accept-source-agreements --override "--quiet --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
goto vsdone
:havevs
echo  [3/5] C++ compiler is installed
:vsdone

REM ---------------------------------------------------------------- source
if not exist "%WORK%" mkdir "%WORK%"
if exist "%SRC%\.git" (
    echo  [4/5] Getting the latest Hawthorne...
    git -C "%SRC%" pull --ff-only
    git -C "%SRC%" submodule update --init --recursive
) else (
    echo  [4/5] Downloading Hawthorne ^(GitHub may ask you to sign in^)...
    git clone --recursive https://github.com/ShallowMouse/hawthorne.git "%SRC%"
)
if not exist "%SRC%\CMakeLists.txt" (
    echo.
    echo  Couldn't download Hawthorne from GitHub. Check you're signed in and try again.
    pause & exit /b 1
)

REM ASIO (for the MOTU and other pro cards)
set "ASIO="
if not exist "%WORK%\asiosdk" (
    powershell -NoProfile -Command "try { Invoke-WebRequest -Uri 'https://www.steinberg.net/asiosdk' -OutFile '%WORK%\asiosdk.zip' -MaximumRedirection 10; Expand-Archive '%WORK%\asiosdk.zip' -DestinationPath '%WORK%\asiosdk' -Force } catch { }" >nul 2>&1
)
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "$h = Get-ChildItem '%WORK%\asiosdk' -Recurse -Filter iasiodrv.h -ErrorAction SilentlyContinue | Select-Object -First 1; if ($h) { $h.Directory.Parent.FullName -replace '\\','/' }"`) do set "ASIO=%%i"
if defined ASIO ( echo        ASIO support: yes ) else ( echo        ASIO support: no ^(couldn't download the ASIO kit^) )

REM ---------------------------------------------------------------- build
echo  [5/5] Building Hawthorne...
cmake -S "%SRC%" -B "%SRC%\build-local" -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DHAWTHORNE_ASIO_SDK="%ASIO%" >"%WORK%\configure.log" 2>&1
if errorlevel 1 (
    echo.
    echo  Setting up the build failed. Details: %WORK%\configure.log
    echo  ^(If the compiler was only just installed, restart the PC and run this again.^)
    pause & exit /b 1
)
cmake --build "%SRC%\build-local" --config Release --target Hawthorne --parallel >"%WORK%\build.log" 2>&1
if errorlevel 1 (
    echo.
    echo  The build failed. Details: %WORK%\build.log
    pause & exit /b 1
)

set "EXE=%SRC%\build-local\Hawthorne_artefacts\Release\Hawthorne.exe"
if not exist "%EXE%" (
    echo  Built, but couldn't find Hawthorne.exe. Details: %WORK%\build.log
    pause & exit /b 1
)

REM a shortcut on the desktop
powershell -NoProfile -Command "$s = (New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Desktop') + '\Hawthorne (local build).lnk'); $s.TargetPath = '%EXE%'; $s.Save()" >nul 2>&1

echo.
echo  Done! Opening Hawthorne. There's a "Hawthorne (local build)" shortcut on your desktop.
echo  Run this file again any time to get the newest version.
start "" "%EXE%"
timeout /t 8 >nul

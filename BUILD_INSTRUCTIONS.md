# VTape24 - Complete Build Instructions (WITH RECORDING FIX)

## 📦 What's Included

This package contains VTape24 with the **RECORDING FIX APPLIED**.

### Source Files (23 files)
```
Source/
├── AudioEngine.cpp          ← FIXED - Input channels now work!
├── AudioEngine.h
├── DiskStreamingThread.h
├── Main.cpp
├── MainWindow.cpp
├── MainWindow.h
├── MainWindow_new.cpp
├── MainWindow_new.h
├── OSCController.cpp
├── OSCController.h
├── RingBuffer.h
├── Settings.cpp
├── Settings.h
├── SettingsDialog.h
├── TapeMachineUI.cpp
├── TapeMachineUI.h
├── TapeUI.h
├── TimelineComponent.h
├── Track.cpp
├── Track.h
├── TrackComponent.h
├── TransportComponent.h
└── VintageLookAndFeel.h
```

### Build Files
- `CMakeLists.txt` - CMake build configuration
- `build.bat` - Windows build script
- `build.sh` - Linux/Mac build script
- `.gitignore` - Git ignore rules

### Documentation
- `README.md` - Project overview
- `BUILD_INSTRUCTIONS.md` - This file
- `RECORDING_FIX_APPLIED.md` - Details about the fix

---

## 🎯 What Was Fixed

**Problem:** Focusrite (and other ASIO drivers) were providing 0 input channels to VTape24, causing recordings to be silent with no waveforms.

**Solution:** Modified `AudioEngine.cpp` to explicitly request 64 input channels instead of trusting the driver's defaults.

**Lines changed:** 111-133 in AudioEngine.cpp

---

## 📋 Prerequisites

### Windows
1. **Visual Studio 2022** (Community Edition is fine)
   - Download: https://visualstudio.microsoft.com/downloads/
   - During installation, select:
     - ✅ Desktop development with C++
     - ✅ CMake tools for Windows
     - ✅ MSVC v143 build tools
     - ✅ Windows 10/11 SDK

2. **CMake** (usually comes with VS2022)
   - Or download: https://cmake.org/download/

3. **ASIO SDK** (for ASIO support)
   - Download from Steinberg: https://www.steinberg.net/developers/
   - Extract to: `C:\SDKs\ASIOSDK`
   - Or set environment variable: `ASIO_SDK_DIR`

### Linux
```bash
sudo apt-get install build-essential cmake
sudo apt-get install libasound2-dev libjack-jackd2-dev
sudo apt-get install libfreetype6-dev libx11-dev libxinerama-dev libxrandr-dev libxcursor-dev libxcomposite-dev
```

### macOS
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install CMake
brew install cmake
```

---

## 🔨 Build Instructions

### Windows (Recommended Method)

#### Option 1: Using build.bat (Easiest)

1. **Open Command Prompt** (or PowerShell)

2. **Navigate to VTape24_FIXED directory**
   ```cmd
   cd path\to\VTape24_FIXED
   ```

3. **Run the build script**
   ```cmd
   build.bat
   ```

4. **Find the executable**
   ```
   build\VTape24_artefacts\Release\VTape24.exe
   ```

#### Option 2: Using Visual Studio IDE

1. **Open Visual Studio 2022**

2. **File → Open → CMake...**

3. **Select** `VTape24_FIXED/CMakeLists.txt`

4. **Wait for CMake to configure** (bottom right status)

5. **Build → Build All** (Ctrl+Shift+B)

6. **Find the executable in:**
   ```
   out\build\x64-Release\VTape24_artefacts\Release\VTape24.exe
   ```

#### Option 3: Manual CMake (Advanced)

```cmd
cd VTape24_FIXED
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

---

### Linux

```bash
cd VTape24_FIXED
chmod +x build.sh
./build.sh
```

Or manually:
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j4
```

Executable will be in: `build/VTape24_artefacts/Release/VTape24`

---

### macOS

```bash
cd VTape24_FIXED
chmod +x build.sh
./build.sh
```

Or manually:
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j4
```

---

## 🎯 After Building

### Running VTape24

**Windows:**
```cmd
cd build\VTape24_artefacts\Release
VTape24.exe
```

**Linux/Mac:**
```bash
cd build/VTape24_artefacts/Release
./VTape24
```

### Verify the Fix

1. **Launch VTape24**

2. **Open the log file:**
   - Windows: `C:\Users\<YourName>\AppData\Roaming\VTape24\VTape24.log`
   - Linux: `~/.config/VTape24/VTape24.log`
   - macOS: `~/Library/Application Support/VTape24/VTape24.log`

3. **Look for these lines:**
   ```
   AudioEngine: Requesting 64 input channels and 2 output channels
   AudioEngine: Device configured successfully!
   AudioEngine: Active input channels = 8  ← Should be > 0!
   AudioEngine: Active output channels = 2
   ```

4. **Test Recording:**
   - ARM Track 1
   - Click RECORD
   - Make noise into your interface
   - Check log for:
     ```
     ARMED - Mapped to input channel: 0
     Track 0 is recording
     ```
     (WITHOUT "writing silence")

---

## 🔧 Troubleshooting

### Build Fails: "Cannot open include file: 'juce_audio_basics/juce_audio_basics.h'"

**Problem:** JUCE framework not found

**Solution:** CMake should automatically download JUCE via FetchContent. If it fails:
1. Check your internet connection
2. Try deleting `build` folder and rebuilding
3. Make sure CMake version is 3.15 or higher

### Build Fails: "ASIO SDK not found"

**Problem:** ASIO SDK not detected (Windows only)

**Solution:**
1. Download ASIO SDK from Steinberg
2. Extract to: `C:\SDKs\ASIOSDK`
3. Or set environment variable:
   ```cmd
   set ASIO_SDK_DIR=C:\path\to\ASIOSDK
   ```

### Build Fails: "MSBuild not found"

**Problem:** Visual Studio not in PATH

**Solution:**
1. Open "Developer Command Prompt for VS 2022"
2. Navigate to VTape24_FIXED
3. Run `build.bat`

### Recording Still Silent After Build

**Problem:** Fix not applied or interface issue

**Solution:**
1. Check the log file for:
   ```
   AudioEngine: Active input channels = X
   ```
   X must be greater than 0!

2. If X is still 0:
   - Check your audio interface is connected
   - Try a different USB port
   - Open the ASIO control panel and enable inputs
   - Restart your computer

3. Verify you built the FIXED version:
   ```cmd
   findstr /C:"Requesting 64 input channels" Source\AudioEngine.cpp
   ```
   Should return a match!

### Can't Find VTape24.exe After Building

**Problem:** Build output location varies by build method

**Try these locations:**
- `build\VTape24_artefacts\Release\VTape24.exe`
- `build\Release\VTape24_artefacts\Release\VTape24.exe`
- `out\build\x64-Release\VTape24_artefacts\Release\VTape24.exe`

---

## 📁 Project Structure

```
VTape24_FIXED/
├── Source/              ← All C++ source and header files
├── build/              ← Created during build (don't commit)
├── CMakeLists.txt      ← CMake configuration
├── build.bat           ← Windows build script
├── build.sh            ← Linux/Mac build script
├── README.md           ← Project README
└── BUILD_INSTRUCTIONS.md ← This file
```

---

## 🚀 Building for Distribution

### Windows Release Build

```cmd
cd VTape24_FIXED
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release --parallel
```

The executable will be in: `build\VTape24_artefacts\Release\VTape24.exe`

This can be distributed as a standalone executable (copy ASIO DLLs if needed).

---

## ⚙️ Build Configuration Options

### Change Sample Rate
Edit `Source/Settings.cpp`:
```cpp
double Settings::getSampleRate() const
{
    return 48000.0;  // Change to 44100, 96000, etc.
}
```

### Change Buffer Size
Edit `Source/Settings.cpp`:
```cpp
int Settings::getBufferSize() const
{
    return 256;  // Change to 128, 512, 1024, etc.
}
```

### Change Number of Tracks
Edit `Source/Settings.cpp`:
```cpp
int Settings::getNumTracks() const
{
    return 16;  // Change to 8, 24, 32, 64, etc.
}
```

---

## 📊 What Changed in This Build

### AudioEngine.cpp - Lines 111-133

**BEFORE (Broken):**
```cpp
setup.useDefaultInputChannels = true;
setup.useDefaultOutputChannels = true;
```

**AFTER (Fixed):**
```cpp
setup.useDefaultInputChannels = false;
setup.useDefaultOutputChannels = false;
setup.inputChannels.setRange(0, 64, true);  // Explicitly request inputs!
setup.outputChannels.setRange(0, 2, true);
```

### Added Diagnostic Logging

```cpp
int numInputs = device->getActiveInputChannels().countNumberOfSetBits();
int numOutputs = device->getActiveOutputChannels().countNumberOfSetBits();
juce::Logger::writeToLog("AudioEngine: Active input channels = " + juce::String(numInputs));
juce::Logger::writeToLog("AudioEngine: Active output channels = " + juce::String(numOutputs));
```

---

## ✅ Expected Build Output

**Successful build looks like:**
```
-- Build files have been written to: .../VTape24_FIXED/build
-- Configuring done
-- Generating done
-- Build files written to: .../build
[100%] Building CXX object VTape24_artefacts/CMakeFiles/VTape24.dir/Source/AudioEngine.cpp.obj
[100%] Linking CXX executable Release\VTape24.exe
[100%] Built target VTape24

BUILD SUCCESSFUL!
VTape24.exe is ready at: build\VTape24_artefacts\Release\VTape24.exe
```

---

## 📞 Need Help?

If you encounter build issues:

1. **Check this file** - Most common issues are covered above
2. **Check the log file** - Look for specific error messages
3. **Check CMake version** - Must be 3.15 or higher
4. **Check Visual Studio version** - Must be 2022 or 2019
5. **Contact support** - Provide:
   - Your OS and version
   - CMake output (first 50 lines)
   - Error messages
   - Log file location

---

**This build includes the recording fix. Your audio inputs will now work correctly!**

Build date: February 2026
Fix applied: Input channel initialization (AudioEngine.cpp)

# VTape24 - Vintage Multitrack Tape Recorder

Professional multitrack recording DAW with vintage tape machine aesthetics.

## Features

- **8/16/32/64 track recording** with ASIO support
- **Static playhead** - tape moves past the playhead (authentic tape machine behavior)
- **Punch-in recording** - record over existing audio at any position
- **Session management** - automatic session folders and file organization
- **OSC control** - remote control via OSC protocol
- **Input/output routing** - flexible routing matrices
- **Low latency** - ASIO drivers for professional recording

## Quick Start

### Build

```cmd
D:
cd \Music\Software\vt24\VTape24
build.bat
```

### Run

```cmd
cd build\VTape24_artefacts\Release
VTape24.exe
```

### First Use

1. Click **Settings** → **Audio** tab
2. Select your ASIO device
3. Choose buffer size (128-256 recommended)
4. Go to **Session** tab
5. Set track count (8/16/32/64)
6. Click **Apply**

### Recording

1. Click **ARM** button on tracks you want to record
2. Click **RECORD** (red button)
3. Perform your take
4. Click **STOP** when done

Files save to: `Documents/VTape24/Sessions/[SessionName]/Recordings/`

## OSC Commands

See [OSC_COMMANDS.md](OSC_COMMANDS.md) for full OSC reference.

**Quick examples:**
```
/transport/play        # Start playback
/transport/stop        # Stop
/transport/record      # Start recording
/track/1/arm 1         # Arm track 1
```

Default OSC port: **8000**

## Architecture

- **JUCE 8** - Modern C++ audio framework
- **In-memory track buffers** - 3 minutes per track, allocated on first recording
- **WAV format** - 48kHz, 24-bit files
- **Punch-in capable** - true tape machine overdub workflow

See [ARCHITECTURE.md](ARCHITECTURE.md) for technical details.

## Files

- `Track_01.wav` to `Track_64.wav` - Individual track recordings
- `session.vtape` - Session metadata (XML)

## Requirements

- Windows 10/11
- ASIO audio interface (or ASIO4ALL)
- Visual Studio 2022 build tools

## Troubleshooting

**No audio device:**
- Install ASIO drivers for your interface
- Or use ASIO4ALL: http://www.asio4all.org/

**Crash on startup:**
- Make sure ASIO drivers are installed
- Try running as Administrator

**Files not saving:**
- Check Settings → Session → Recording Path
- Ensure you have write permissions

## Credits

Built with JUCE 8 framework.

---

**VTape24** - Professional multitrack recording with vintage character.

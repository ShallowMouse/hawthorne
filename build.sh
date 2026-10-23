#!/bin/bash

# VTape24 Quick Start Build Script

echo "========================================="
echo "  VTape24 - Build Script"
echo "========================================="
echo ""

# Check if JUCE exists
if [ ! -d "JUCE" ]; then
    echo "⚠️  JUCE not found. Cloning JUCE framework..."
    git clone https://github.com/juce-framework/JUCE.git
    echo "✅ JUCE cloned successfully (latest version, JUCE 8 compatible)"
else
    echo "✅ JUCE found"
fi

# Create build directory
if [ ! -d "build" ]; then
    mkdir build
    echo "✅ Build directory created"
fi

cd build

# Detect OS and generate build files
echo ""
echo "🔧 Generating build files..."

if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    cmake .. -G Xcode
    echo ""
    echo "✅ Build files generated for Xcode"
    echo ""
    echo "📦 Building..."
    cmake --build . --config Release
    echo ""
    echo "✅ Build complete!"
    echo ""
    echo "🚀 Run with: open VTape24_artefacts/Release/VTape24.app"
    
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Linux
    cmake ..
    echo ""
    echo "✅ Build files generated"
    echo ""
    echo "📦 Building..."
    make -j$(nproc)
    echo ""
    echo "✅ Build complete!"
    echo ""
    echo "🚀 Run with: ./VTape24_artefacts/Release/VTape24"
    
else
    # Windows (Git Bash/MSYS2)
    cmake .. -G "Visual Studio 16 2019"
    echo ""
    echo "✅ Build files generated for Visual Studio"
    echo ""
    echo "📦 Building..."
    cmake --build . --config Release
    echo ""
    echo "✅ Build complete!"
    echo ""
    echo "🚀 Run with: VTape24_artefacts/Release/VTape24.exe"
fi

echo ""
echo "========================================="
echo "  Build Complete! 🎉"
echo "========================================="

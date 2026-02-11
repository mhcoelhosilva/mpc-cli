#!/bin/bash

# Build the project
echo "Building cpp-test..."
cd "$(dirname "$0")"
mkdir -p build
cd build
cmake .. || exit 1
make -j8 || exit 1

echo ""
echo "Build successful!"
echo ""
echo "========================================"
echo "IMPORTANT: Accessibility Permissions"
echo "========================================"
echo "This app needs Accessibility permissions to capture keyboard input."
echo ""
echo "If you see an error about event tap failing, do this:"
echo "1. Open System Settings (System Preferences)"
echo "2. Go to Security & Privacy > Privacy > Accessibility"
echo "3. Click the lock to make changes"
echo "4. Add Terminal (or your terminal app) to the list"
echo "5. Make sure it's checked/enabled"
echo "6. Restart Terminal and run this script again"
echo ""
echo "========================================"
echo ""

# Run the executable from project root (so it finds samples/ directory)
echo "Starting cpptestbin..."
cd ..
./build/cpptestbin

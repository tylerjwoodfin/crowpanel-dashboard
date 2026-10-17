#!/bin/bash
# Build the EventKit helper the grocery server shells out to.
# The default SDK on this machine is newer than the Command Line Tools linker,
# so the helper links against the 26.5 SDK the linker understands.
set -euo pipefail
cd "$(dirname "$0")"
sdk="/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk"
app="GroceryList.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS"
cp Info.plist "$app/Contents/Info.plist"
clang -fobjc-arc -mmacosx-version-min=14.0 -isysroot "$sdk" \
    -framework Cocoa -framework EventKit \
    -o "$app/Contents/MacOS/GroceryList" GroceryList.m
codesign --force --sign - --identifier com.tyler.crowpanel-grocery "$app"

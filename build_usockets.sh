#!/bin/bash

# Navigate to the uSockets directory
cd uWebSockets-0.17.3/uSockets || exit 1

# Check if git submodule is initialized, if not, initialize it
if [ ! -d ".git" ]; then
    cd ..
    git submodule update --init --recursive
    cd uSockets || exit 1
fi

# Build uSockets
make

# Copy the built library to a central location
cp libuSockets.a ../../

echo "uSockets library built successfully!" 
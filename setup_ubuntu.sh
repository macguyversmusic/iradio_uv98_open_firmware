#!/bin/sh
# Install what is needed to build and flash on Ubuntu 24.04 (native or WSL2).
set -e
sudo apt-get update
sudo apt-get install -y make gcc-arm-none-eabi libnewlib-arm-none-eabi binutils-arm-none-eabi python3 python3-serial
echo
echo "Done. Build: cd firmware && make"
echo "Optional, only to regenerate the screen font (make font): sudo apt-get install -y python3-pil fonts-dejavu-core"

#!/bin/sh
# Install everything needed to build, flash and control the radio, on Ubuntu 24.04 (native or WSL2).
# Run from the repository folder: ./setup_ubuntu.sh
set -e
cd "$(dirname "$0")"
sudo apt-get update
sudo apt-get install -y make gcc-arm-none-eabi libnewlib-arm-none-eabi binutils-arm-none-eabi \
    python3 python3-venv
python3 -m venv .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install -r tracker/requirements.txt
sudo usermod -aG dialout "$USER"
echo
echo "Done."
echo "  Build:   cd firmware && make"
echo "  Flash:   .venv/bin/python tools/flash.py flash --model uv98 --image bin/<image>.bin --yes"
echo "  Tracker: .venv/bin/python tracker/server.py   then open http://localhost:8766"
echo "Log out and in again (or restart WSL) once, so the serial port permission takes effect."

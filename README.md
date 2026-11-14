# iRadio UV-98 Plus open firmware AkA Radtel RT-880G

I got sick of people selling simple knowledge or "personalised" firmware.  open it up and share dont be greedy.

Open firmware for the iRadio UV-98 Plus handheld (Radtel RT-880G family, FCC ID 2AZSA-6D880), written from scratch. This repository also holds the tools and notes needed to repeat the work from zero.

**Status: experimental test firmware (test 8). It is not yet a usable everyday radio.** It has been run on one UV-98 Plus only. That radio's calibration values (frequency correction words, transmit deviation) are built into `firmware/src/main.c`. On another radio the frequency may be slightly off until the firmware reads each radio's own calibration from its SPI flash; [FINDINGS.md](FINDINGS.md) lists where those values are.

| Works on that radio | Test |
|---|---|
| Display, keypad, the three side keys and the top (alarm) button | 1-5 |
| Receive on radio chip 1 (BK4819), RSSI display, serial control commands | 4 |
| Speaker audio | 6 |
| Noise squelch (tested on NFM, PMR446) | 7 |
| Transmit a 1 kHz tone on PMR446, heard on another radio | 7 |
| Voice transmit at minimum power on PMR446 (heard on another radio, but the audio is low) | 8 |

Next steps: finish transmit (power levels, amplifier per band, time-out timer, CTCSS/DCS), channels from the radio's SPI flash, settings and menus, the second radio chip and FM broadcast.

## Known problem: switching off while transmitting

If the radio is switched off while it is transmitting, the radio chip keeps its transmit settings. The next power-on into bootloader mode (PTT held) then transmits until the radio is switched off again. A normal power-on clears it, because the open firmware turns the transmitter off first thing. **Release PTT before switching off.** A fix is being worked on.

## Transmit limits in this firmware

- PMR446 only (446.000-446.200 MHz); PTT is refused on any other frequency.
- Minimum power: radio chip power level 8 (register 0x36 = 0x20FF), external UHF power amplifier off.
- 5-second limit per press, then locked out until PTT is released. Watchdog on.

Check your local rules before transmitting.

## What you need

- The radio. The UV-98 Plus bootloader uses checksum seed 0x00. Radtel RT-880 radios use 0x52 (`flash.py --model rt880`), but this firmware has not been tried on one.
- A programming cable: a USB-serial adapter wired to the radio's USB-C data pins. From the pin map, D- is the radio's transmit line (processor PA11, USART6 TX) and D+ its receive line (PA12, USART6 RX), plus ground. Ours is an FTDI FT232R cable, home-made from another radio's programming cable and a USB-C plug. A plain USB-C cable does nothing: no USB device appears. The processor runs at 3.3 V.
- A computer with Ubuntu 24.04, either native or in WSL2 on Windows.

## Setup from zero

On Ubuntu, from this folder:

```sh
./setup_ubuntu.sh
```

It installs `make`, `gcc-arm-none-eabi`, `libnewlib-arm-none-eabi` and `python3-serial` with apt. These are the versions we built with: gcc-arm-none-eabi 13.2.rel1, newlib 4.4.0, Python 3.12, pyserial 3.5. The font header is included, so Pillow and the DejaVu font are needed only to regenerate it (`make font`).

On native Linux, add yourself to the `dialout` group to use the serial port: `sudo usermod -aG dialout $USER`, then log in again.

On WSL2, the cable has to be passed through from Windows with usbipd-win:

1. On Windows, install usbipd-win: `winget install usbipd`.
2. In an administrator PowerShell, run `usbipd list` and note the cable's bus ID (FTDI, `0403:6001`).
3. Run `usbipd bind --busid <id>`. Add `--force` if USBPcap is installed.
4. Run `usbipd attach --wsl --busid <id>`. Repeat this after every unplug or reboot.
5. In Ubuntu, the cable appears as `/dev/ttyUSBn`. If several USB-serial adapters are attached, the numbers can swap; the tools find the FTDI cable by its USB ID.

## 1. Back up the radio (stock firmware)

The open firmware does not answer the stock programming protocol, so make this backup while the stock firmware is still on the radio. Switch the radio on normally with the cable connected, then run:

```sh
python3 tools/backup_spi.py my_uv98_spiflash.bin
```

This reads all 4 MB of the radio's SPI flash, including its channels, settings and calibration. It uses the same read command we used for our backup, but this script itself has not been run on the radio yet.

## 2. Get the official firmware (to go back to stock)

Download iRadio's "UV98plus V1_16 Upgrade Tool 250915" for the UV-98 Plus, then extract the firmware from it. The tool accepts the zip or the .exe inside it: https://www.qziradio.com/filedownload/1020912 https://www.qziradio.com/downloads page 2

```sh
python3 tools/extract_upgrade_tool.py "UV98plus+V1_16+Upgrade+Tool+250915__Slow.zip" uv98plus_V1.16_250915.bin
```

The result should be 251,904 bytes with sha256 `bf71002d58b5130269d2c0837c513645e8c4898e06a2fd89e91ee8ab187620ed` and version text "UV Radio V1.16 2025-09-15". Radtel's RT-880 V1.16 has identical code; only the model name differs.

## 3. Bootloader mode

Switch the radio on normally, then switch it off **without pressing PTT** (see the known problem above). Then hold PTT1 and switch on. The screen stays white: the bootloader is waiting.

## 4. Flash

```sh
python3 tools/flash.py probe --model uv98                  # optional: connect only, erases nothing
python3 tools/flash.py flash --model uv98 --image bin/uv98plus_openfw_test8.bin --yes
```

The flash takes about 30 seconds: 246 blocks of 1 KB, each acknowledged. When it prints `final reply 6`, switch the radio off and then on normally.

To go back to stock, flash `uv98plus_V1.16_250915.bin` the same way. The flasher writes only the application area (from 0x08002800), never the bootloader. We have gone back to stock V1.16 this way.

## 5. Build from source

```sh
cd firmware
make
```

This produces `firmware/build/uv98plus_openfw.bin`. The code is identical to `bin/uv98plus_openfw_test8.bin`; only the build time that the `VER` command prints differs.

## Using the test firmware

| Key | Action |
|---|---|
| 0-9, then # | Enter a frequency in kHz and tune to it (red bar clears the entry) |
| * | PMR446 channel 1, 446.00625 MHz NFM |
| Up / down | Step 12.5 kHz |
| Green bar | Cycle the mode: FM, NFM, AM |
| PTT2 or monitor (side keys) | Open the squelch while held |
| PTT1 | Transmit (PMR446 only, see the limits above) |

The same cable gives a serial console at 115200 baud: `PING`, `VER`, `F [MHz]`, `M FM|NFM|AM`, `SQ 0-9`, `VOL 0-63`, `RSSI`, `ADC`, `R1 xx` / `R2 xx` (read register xx of radio chip 1 or 2) and `HELP`.

## Files

| Path | What it is |
|---|---|
| `firmware/src/main.c` | The firmware |
| `firmware/rt880_app.ld`, `firmware/Makefile` | Linker script (app at 0x08002800, stack 0x20009000) and build |
| `firmware/src/font.h`, `firmware/tools/mkfont.py` | 16x24 screen font and its generator |
| `firmware/tools/check_image.py` | Checks an image before it is flashed (size, stack pointer, reset vector) |
| `firmware/lib/at32f423/` | The parts of Artery's AT32F423 library the build uses (see [ATTRIBUTION.md](ATTRIBUTION.md)) |
| `bin/uv98plus_openfw_test8.bin` | Test 8 image, the one flashed to our radio (`bin/SHA256SUMS`) |
| `tools/flash.py` | Bootloader flasher (`probe` / `flash`, `--model uv98` or `rt880`) |
| `tools/backup_spi.py` | 4 MB SPI flash backup (stock firmware) |
| `tools/extract_upgrade_tool.py` | Extracts the official firmware from iRadio's upgrade tool |
| `tools/radtel_vfo.py` | Sets the VFO frequency and mode on the stock firmware by rewriting settings blocks |
| `analysis/` | Ghidra scripts and the hardware-usage script used to study the stock firmware |
| [FINDINGS.md](FINDINGS.md) | Everything worked out about the hardware and protocols |

Not included: Radtel/iRadio firmware, vendor tools, decompiled stock code, datasheets, and backups from our radio. [analysis/README.md](analysis/README.md) explains how to regenerate the decompiled code.

## Licence

The code in this repository is licensed under the GNU General Public License version 3 ([LICENSE](LICENSE)).

Additional permission under GNU GPL version 3 section 7: if you modify this program, or any covered work, by linking or combining it with the Artery AT32F423 firmware library (`firmware/lib/at32f423`) or with bitmap glyphs generated from the DejaVu fonts (`firmware/src/font.h`), containing parts covered by the terms of their own licences, the licensors of this program grant you additional permission to convey the resulting work.

Third-party parts keep their own licences; see [ATTRIBUTION.md](ATTRIBUTION.md).

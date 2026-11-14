# iRadio UV-98 Plus open firmware AkA Radtel RT-880G

I got sick of people selling simple knowledge or "personalised" firmware.  open it up and share dont be greedy.

Open firmware for the iRadio UV-98 Plus handheld (Radtel RT-880G family, FCC ID 2AZSA-6D880), written from scratch, plus the tools to flash it and control the radio from a computer over the programming cable, and a satellite tracker that tunes the radio to downlinks.

**Status: experimental (test 14), run on one UV-98 Plus only.** That radio's calibration values (frequency correction, transmit deviation) are built into `firmware/src/main.c`, so on another radio the frequency may be slightly off until the firmware reads each radio's own calibration from its SPI flash. [FINDINGS.md](FINDINGS.md) lists where those values are.

## Features

- **Receive:** 18-660 MHz and 840-1300 MHz in FM, NFM, AM and USB. Noise squelch, CTCSS and DCS tone squelch, signal meter.
- **Transmit:** amateur bands only (all three ITU regions), plus marine VHF capped at MID power. Power MIN, LOW, MID and HIGH from the radio's own calibration. CTCSS and DCS, a 1750 Hz burst on the alarm button, and a time-out timer.
- **On the radio:** boot splash, high-contrast screen with smooth fonts, and a menu with number shortcuts. Band presets: 10 m, 6 m, air (receive only), 2 m, marine, 220 MHz, 70 cm, 23 cm. The LED is green when receiving, yellow when a signal is held back by the tone squelch, and red when transmitting.
- **From a computer:** serial commands over the programming cable, and the satellite tracker in [tracker/](tracker/README.md).
- **Not included:** one VFO only for now, no FM broadcast, no channels yet. Settings are not kept after power-off yet.

## What you need

- The radio.
- A programming cable: a USB-serial adapter (FTDI FT232R, 3.3 V logic) wired to the radio's side USB-C port, with D- to the radio's transmit line (processor PA11), D+ to its receive line (PA12), and ground. A plain USB-C cable does nothing. Ours is home-made from another radio's programming cable and a USB-C plug.
- A computer with Ubuntu 24.04, either native or in WSL2 on Windows 10/11.

## Setup

### On Windows (WSL2)

1. In an administrator PowerShell, install Ubuntu and the USB pass-through tool, then restart Windows:
   ```
   wsl --install -d Ubuntu-24.04
   winget install usbipd
   ```
2. Open Ubuntu and get this repository:
   ```sh
   sudo apt-get install -y git
   git clone https://github.com/macguyversmusic/iradio_uv98_open_firmware.git
   cd iradio_uv98_open_firmware
   ./setup_ubuntu.sh
   ```
   Then run `wsl --shutdown` in PowerShell and open Ubuntu again, so the serial port permission takes effect.
3. Pass the cable through to Ubuntu. In an administrator PowerShell:
   ```
   usbipd list                          # note the bus ID of "USB Serial Converter" (0403:6001)
   usbipd bind --busid <id>             # once; add --force if USBPcap is installed
   usbipd attach --wsl --busid <id>     # every time the cable is plugged in or Windows restarts
   ```
   In Ubuntu the cable appears as `/dev/ttyUSB0` (or another number). The tools find it by its USB ID.

### On native Linux (Ubuntu 24.04)

```sh
git clone https://github.com/macguyversmusic/iradio_uv98_open_firmware.git
cd iradio_uv98_open_firmware
./setup_ubuntu.sh
```
Log out and in again once.

`setup_ubuntu.sh` installs the ARM compiler, `make` and Python with apt, and creates `.venv` with pyserial, skyfield and numpy for the flasher, the tools and the tracker.

## Flashing

### 1. Back up the radio first (stock firmware)

The open firmware does not answer the stock programming protocol, so make this backup while the stock firmware is still on the radio. Switch the radio on normally with the cable connected:

```sh
.venv/bin/python tools/backup_spi.py my_uv98_spiflash.bin
```

This reads all 4 MB of the radio's SPI flash: channels, settings and calibration. It uses the read command we used for our own backup, but this script itself has not been run on a radio yet.

### 2. Get the official firmware, to go back to stock

Download iRadio's "UV98plus V1_16 Upgrade Tool 250915" for the UV-98 Plus (https://www.qziradio.com/filedownload/1020912, or https://www.qziradio.com/downloads page 2) and extract the firmware from it. The tool accepts the zip or the .exe inside it:

```sh
.venv/bin/python tools/extract_upgrade_tool.py "UV98plus+V1_16+Upgrade+Tool+250915__Slow.zip" uv98plus_V1.16_250915.bin
```

The result should be 251,904 bytes, sha256 `bf71002d58b5130269d2c0837c513645e8c4898e06a2fd89e91ee8ab187620ed`, version text "UV Radio V1.16 2025-09-15".

### 3. Bootloader mode

Switch the radio on normally, then off **without pressing PTT** (see the known problem below). Hold PTT1 and switch on. The screen stays white: the bootloader is waiting.

### 4. Flash

```sh
.venv/bin/python tools/flash.py probe --model uv98                 # optional: connect only, erases nothing
.venv/bin/python tools/flash.py flash --model uv98 --image bin/uv98plus_openfw_test14.bin --yes
```

It takes about 30 seconds: 246 blocks of 1 KB, each acknowledged. When it prints `final reply 6`, switch the radio off and on normally. To go back to stock, flash `uv98plus_V1.16_250915.bin` the same way.

The flasher checks the image before sending it and writes only the application area (from 0x08002800), never the bootloader. Every 1 KB block carries a checksum and is acknowledged by the bootloader, and the bootloader only starts an application whose stack pointer is valid. So bootloader mode is always there to flash again. We have flashed one radio dozens of times, including back to stock.

## Using the radio

| Key | Action |
|---|---|
| 0-9, then # | Type a frequency in kHz and tune to it (red bar clears the entry) |
| * | Switch between 145.500 and 433.500 MHz |
| Up / down | Tune by the step set in the menu (12.5 kHz by default); held, it repeats |
| Green bar | Open the menu |
| PTT2 or monitor (side keys) | Open the squelch while held |
| PTT1 | Transmit |
| Alarm button (top) | Transmit a 1750 Hz burst while held; pressed while PTT1 is held, it replaces the voice until released |

**Menu.** In the menu:
- Green bar: opens the menu, edits the selected item, and keeps a change. Red bar: undoes a change, or leaves the menu.
- Up/down: picks an item, or changes its value, which takes effect immediately. Number keys jump straight to an item.
- `#` in a tone item: jumps to the next tone type (off, CTCSS, DCS normal, DCS inverted).

The items are:
1. Band: 10 m, 6 m, AIR, 2 m, MARINE, 220, 70 cm, 23 cm. Each sets its own mode and step and remembers its last frequency.
2. Power
3. Mic gain
4. TX tone
5. RX tone
6. Squelch
7. Step
8. Mode
9. Time-out
10. Backlight (key 0)

**Serial commands** (115200 baud, over the programming cable):

| Command | Action |
|---|---|
| `PING`, `VER`, `HELP`, `CLK` | Check the link, firmware version, command list, system clock |
| `F [MHz]` | Show or set the frequency (to 10 Hz) |
| `M FM\|NFM\|AM\|USB` | Set the mode |
| `SQ 0-9`, `VOL 0-63` | Squelch level (0 = open), receive audio level |
| `MIC [0-31]` | Show or set the mic gain (default 25); works while transmitting |
| `POW [MIN\|LOW\|MID\|HIGH]` | Show or set the transmit power (default MIN) |
| `T [OFF\|88.5\|D023N\|D023I]` | Transmit tone: CTCSS in Hz, or DCS normal / inverted |
| `RT [OFF\|88.5\|D023N\|D023I]` | Receive tone squelch (FM / NFM) |
| `RSSI`, `ADC` | Signal strength; side-key and battery readings |
| `R1 xx`, `R2 xx` | Read register xx of radio chip 1 or 2 |
| `FONT 1\|2`, `EDGE 0-3`, `PAL 1\|2` | Screen style (defaults FONT 2, EDGE 2, PAL 2) |

There is no serial transmit command yet; transmitting is only from the radio's own keys.

## Satellite tracker

`tracker/` is a web page that lists the amateur satellites passing over you (those heard in the last 24 hours on AMSAT's status page) and tunes the radio to a downlink with one click, Doppler-corrected, over the programming cable. Set your location in `tracker/config.json`, then:

```sh
.venv/bin/python tracker/server.py
```

and open http://localhost:8766. Details, including WSL networking and loading your own keps: [tracker/README.md](tracker/README.md).

## Building from source

```sh
cd firmware
make
```

This produces `firmware/build/uv98plus_openfw.bin`, built with gcc-arm-none-eabi 13.2 and newlib 4.4 from Ubuntu 24.04. The font headers are included, so regenerating them (`make font`) is only needed after changing fonts; it needs `python3-pil` and `fonts-dejavu-core`.

## Transmit limits

- **Allowed:** amateur bands of all three ITU regions that the radio chip can tune: 17 m, 15 m, 12 m, 10 m, 8 m, 6 m, 4 m, 2 m, 1.25 m, 70 cm, 33 cm and 23 cm. See `tx_ranges` in `firmware/src/main.c`.
- **Marine VHF** 156.000-162.025 MHz, capped at MID power. Marine use normally needs a marine licence and type-approved equipment.
- **Everything else is refused,** including the air band. Whether the radio actually radiates on HF, 6 m, 4 m, 33 cm and 23 cm has not been tested.
- **Power:** MIN by default (radio chip only, no amplifier). LOW, MID and HIGH use the radio's calibration with the band's amplifier on, as stock does.
- **Time-out timer:** 60 seconds by default (menu), then locked out until the key is released. Watchdog on.

Check your local rules before transmitting.

## Known problem: switching off while transmitting

If the radio is switched off while transmitting, the radio chip keeps its transmit settings, and the next power-on into bootloader mode transmits until switched off. A normal power-on clears it. **Release PTT before switching off.** The stock firmware has the same gap.

## Files

| Path | What it is |
|---|---|
| `setup_ubuntu.sh` | Installs everything (compiler, Python packages in `.venv`) |
| `firmware/` | Source (`src/main.c`), build (`Makefile`, `rt880_app.ld`), fonts (`src/fonts_aa.h`, made by `tools/mkfont_aa.py`), image checker, and the parts of Artery's AT32F423 library the build uses |
| `bin/` | The current firmware image and its `SHA256SUMS` |
| `tools/flash.py` | Bootloader flasher (`probe` / `flash`, `--model uv98`; `--model rt880` for Radtel RT-880, untested) |
| `tools/backup_spi.py` | 4 MB SPI flash backup (stock firmware) |
| `tools/extract_upgrade_tool.py` | Extracts the official firmware from iRadio's upgrade tool |
| `tracker/` | Satellite tracker |
| [FINDINGS.md](FINDINGS.md) | Everything worked out about the hardware and protocols, with every test result |
| [TODO.md](TODO.md) | What is planned |

Not included: Radtel/iRadio firmware, vendor tools, decompiled stock code and datasheets. The stock firmware was studied in Ghidra (raw binary, ARM Cortex, base address 0x08002800); the results are in FINDINGS.md.

## Licence

The code in this repository is licensed under the GNU General Public License version 3 ([LICENSE](LICENSE)).

Additional permission under GNU GPL version 3 section 7: if you modify this program, or any covered work, by linking or combining it with the Artery AT32F423 firmware library (`firmware/lib/at32f423`) or with bitmap glyphs generated from the DejaVu fonts (`firmware/src/fonts_aa.h`), containing parts covered by the terms of their own licences, the licensors of this program grant you additional permission to convey the resulting work.

Third-party parts keep their own licences; see [ATTRIBUTION.md](ATTRIBUTION.md).

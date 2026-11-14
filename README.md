# iRadio UV-98 Plus open firmware AkA Radtel RT-880G

I got sick of people selling simple knowledge or "personalised" firmware.  open it up and share dont be greedy.

Open firmware for the iRadio UV-98 Plus handheld (Radtel RT-880G family, FCC ID 2AZSA-6D880), written from scratch, plus the tools to flash it and control the radio from a computer over the programming cable, and a satellite tracker that tunes the radio to downlinks.

**Status: experimental (test 22), run on one UV-98 Plus only.** The firmware reads the radio's own calibration (frequency correction, deviation, power levels) from its SPI flash, with our radio's values built into `firmware/src/main.c` as a fallback.

## Features

Each feature says whether it has been tested on the radio, and with which test build. "Not tested yet" means no test on the radio has been recorded. Test 22 is on our radio now.

### Receiving

- **Receive 18-660 MHz and 840-1300 MHz in FM, NFM, AM and USB.** Type the frequency in kHz and press `#` (for example `145500#`); the red bar clears a half-typed frequency. Up/down step by the menu step and repeat when held. Change the mode in menu RX/TX, Mode. Over the cable: `F 145.5`, `M USB`.
  *Tested at 446 MHz with a UV-5R: FM and NFM (tests 6-7), USB (test 11). Not tested yet: AM, and the other bands.*
- **Band presets:** 10 m, 6 m, AIR, 2 m, MARINE, 220, 70 cm and 23 cm. Each sets its usual mode, step and repeater offset (Region 2), turns the shift off, and comes back to the last frequency you used in that band. Menu RX/TX, Band.
  *Not tested yet.*
- **Noise squelch:** silences the speaker until a signal opens it. Levels 0 (always open) to 9, default 3. Menu RX/TX, Squelch, or `SQ 0-9`. Hold PTT2 or the monitor side key to open it by hand.
  *Tested (test 7).*
- **Receive tone squelch (CTCSS and DCS):** the speaker opens only for a signal carrying the tone you set: CTCSS, DCS normal or DCS inverted. Menu RX/TX, RX tone; in a tone item, `#` jumps to the next tone type. Over the cable: `RT 88.5`, `RT D023N`, `RT D023I`, `RT OFF`.
  *Tested (test 11).*
- **Signal meter:** a CB-style S-meter at the bottom of the screen. Over the cable: `RSSI`.
  *Known problem: not calibrated. An empty channel reads about S9+10.*
- **LED colours:** green when receiving, yellow when a signal is held back by the tone squelch, red when transmitting.
  *Tested (tests 12-13).*
- **Automatic gain control for USB and AM:** turns the receiver gain down on strong signals and back up when they go. On by default. Menu Audio, AGC, or `AGC ON` / `AGC OFF`.
  *Not tested yet on a real SSB signal.*
- **Dual watch:** the radio's second receiver chip listens on the other VFO, shown on the small line under the main frequency. When it hears a signal while the main receiver is quiet, the main receiver listens there too and the line turns green; the top of the screen and transmit stay on the main VFO or channel. It comes back 2 seconds after the signal ends, or at once on any key, PTT or a serial command that changes something. On at start-up, also in channel mode. Menu Settings, Dual watch, or `DW ON` / `DW OFF`.
  *Partly tested: it starts and watches the other VFO (test 18, over the cable). Not tested yet with a signal on the other VFO.*
- **Band scan:** in VFO mode, steps through the current band preset by the menu step. It stops on a signal and carries on 3 seconds after the signal goes. Menu Scan, Scan, or `SCAN ON`. Any key or PTT stops it.
  *Not tested yet.*
- **Channel scan:** in channel mode, Scan steps through the used channels instead, skipping channels marked "scan remove" in CHIRP, each with its own receive tone. It stops on a signal and carries on 3 seconds after the signal goes. Menu Scan, Scan, or `SCAN ON`. Any key or PTT stops it.
  *Not tested yet (new in test 20).*
- **Frequency detect:** finds the frequency of a nearby transmitter. Start it, then key the other radio close by. The radio tunes to what it finds, to the nearest 1 kHz, or shows NOTHING FOUND after 20 seconds. Menu Scan, Freq detect, or `FDET`. Any key stops it.
  *Not tested yet.*

### Transmitting

- **Amateur bands of all three ITU regions:** PTT1 transmits voice on 17 m, 15 m, 12 m, 10 m, 8 m, 6 m, 4 m, 2 m, 1.25 m, 70 cm, 33 cm and 23 cm. Any other frequency is refused, including the air band. See [Transmit limits](#transmit-limits).
  *Tested: 2 m and 70 cm, heard on a UV-5R (tests 8-11). Not tested yet: the other bands.*
- **Marine VHF, MID power at most:** 156.000-162.025 MHz.
  *Not tested yet.*
- **CB, MID power at most:** 26.965-27.405 MHz (the 40 channels), narrow FM.
  *Not tested yet.*
- **PMR446, LOW power only:** 446.0-446.2 MHz.
  *Transmit on 446.00625 was tested at LOW in tests 8-11; the LOW-only limit is new in test 22.*
- **Power LOW, MID, HIGH:** each level comes from the radio's own calibration, with the band's amplifier on, as stock does. Default MID (when no settings are saved). CB and marine are limited to MID, PMR446 to LOW. Menu RX/TX, Power, or `POW MID`. In channel mode each channel uses its own power, within the same limits.
  *Partly tested: the battery voltage drops more at each level (test 11). The output in watts has not been measured.*
- **Transmit tone (CTCSS and DCS):** menu RX/TX, TX tone, with `#` jumping to the next tone type. Over the cable: `T 88.5`, `T D023N`, `T D023I`, `T OFF`.
  *Tested: each decoded by a UV-5R (test 11).*
- **1750 Hz tone burst:** hold the alarm button on top of the radio. Pressed while PTT1 is held, it replaces your voice until you let go.
  *Tested (tests 10-11).*
- **Repeater shift and offset:** menu RX/TX, Shift (off, + or -) and Offset (100 kHz, 500 kHz, 600 kHz, 1, 1.6, 5, 6, 7.6 or 12 MHz), or `SHIFT -` and `OFFSET 0.6`.
  *Partly tested: the transmit frequency was checked over the cable (test 15). Not tested yet on the air.*
- **Reverse:** with a shift set, holding `#` for 1.5 seconds listens on the repeater input; hold it again to swap back. On a split channel it swaps receive and transmit.
  *Not tested yet.*
- **Mic gain:** 0-31, default 25. Menu Audio, Mic gain, or `MIC 25`; over the cable it also works while transmitting.
  *Tested at 25 (test 11).*
- **Time-out timer:** stops transmitting after 15 seconds to 10 minutes, default 60 seconds; transmit then stays locked out until you release the key. Menu RX/TX, Time-out.
  *Tested (test 11).*
- **Transmit cut-off at switch-off:** if the supply drops while transmitting, for example when you switch off with PTT held, the firmware stops transmitting at once.
  *Not tested yet. Until it is, release PTT before switching off (see [Known problems](#known-problems)).*

### On the radio

- **VFO A and VFO B:** two VFOs, each with its own frequency, mode, step, shift, offset and tones. The main one is at the top; the other is on a small line under the main frequency, receive only. A short press of `*` swaps them. Over the cable: `VFO A` / `VFO B`.
  *Tested: switching over the cable (test 15), the `*` key by you (test 21).*
- **Menu in groups:** see [Using the radio](#using-the-radio).
  *Not tested yet.*
- **Tuning step:** 0.1, 1, 2.5, 5, 6.25, 10, 12.5, 25, 50 or 100 kHz, default 12.5 kHz. Menu RX/TX, Step.
  *Not tested yet.*
- **Backlight:** five brightness levels. Menu Settings, Backlight.
  *Not tested yet.*
- **Backlight timeout:** turns the screen and keypad light off after 10, 20, 30 or 60 seconds without use, or never (the default). The first key press only wakes the screen. Menu Settings, Light off.
  *Not tested yet.*
- **Key lock:** hold `*` (the key with the lock symbol) for 1.5 seconds to lock or unlock the keys. PTT still works while locked.
  *Tested by you (test 21).*
- **Low battery warning:** shows LOW BATTERY when the battery drops to 6.8 V.
  *Not tested yet.*
- **Boot splash and screen:** M0FCC inverted in the middle, then "iRadio UV98+" and "Open Firmware v1.0". High-contrast screen with smooth fonts. The screen style can be changed over the cable with `FONT 1|2`, `EDGE 0-3` and `PAL 1|2`.
  *Tested (tests 12-13).*
- **Saved settings:** both VFOs and which one is selected, power, mic gain, squelch, time-out, backlight, light timeout, band memories, channel mode and channel, and screen style are saved in a spare sector of the radio's SPI flash (0xFC000) 3 seconds after a change, and loaded at switch-on. `SAVE` saves at once; `SETTINGS` shows whether settings were loaded at start-up.
  *Tested over the cable: test 18 loaded what test 17 saved.*
- **Calibration from the radio itself:** frequency correction, deviation and power levels are read from the radio's own calibration in its SPI flash, with built-in values as a fallback. `CAL` shows which were used.
  *Tested over the cable (test 16), on one radio.*

### Channels

- **Channel mode:** uses the channels already in the radio, as programmed by the stock software or CHIRP: frequency, split, mode, tones, power and name. A short press of `#` (the V/M key), or menu Memory, VFO / CH, switches between VFO and channel mode; menu Memory, Channel steps through the used channels. Up/down step through the used channels; type a channel number and `#` to jump to it. Over the cable: `CH 17`, `CHLIST`, `VFOMODE`.
  Channels marked "Only Rx" in CHIRP never transmit.
  *Partly tested: `CHLIST` lists the radio's channels (test 18, over the cable); V/M on `#` tested by you (tests 21-22). Not tested yet: up/down and a number + `#` in channel mode.*
- **Programming with CHIRP or the stock CPS:** the firmware answers the stock programming protocol, so CHIRP (model Radtel RT-880G) and iRadio's CPS can read and write the channels. After a write, the radio restarts.
  *Not tested yet.*

### From a computer

- **Serial control:** the settings above can be read and set over the programming cable at 115200 baud. `HELP` lists the commands; see [Serial commands](#serial-commands).
  *Tested (tests 15-18).*
- **Transmit from the computer:** `PTT ON` keys the radio and `PTT OFF` unkeys it. `PTT ON` must be repeated at least every 2 seconds, or transmit stops by itself. Band limits, power and the time-out timer still apply.
  *Tested (test 15).*
- **Hamlib rig control:** see [Rig control](#rig-control-hamlib).
  *Tested (test 15).*
- **Satellite tracker:** see [Satellite tracker](#satellite-tracker).
  *Partly tested: tuning and Doppler follow on five passes (nothing heard, radio indoors). Not tested yet: Auto.*
- **Register access for experiments:** `R1 xx` / `R2 xx` read a register on radio chip 1 or 2; `W1 reg value` writes one on chip 1 (transmit registers are refused). `SPIID` and `SPIRD` read the flash chip.
  *Tested (test 16).*

**Not included:** LSB, and the LW, MW, SW and FM broadcast reception listed in the radio's specification.

## What you need

- The radio.
- A programming cable: a USB-serial adapter (FTDI FT232R, 3.3 V logic) wired to the radio's side USB-C port, with D- to the radio's transmit line (processor PA11), D+ to its receive line (PA12), and ground. A plain USB-C cable does nothing. Ours is home-made from another radio's programming cable and a USB-C plug. thanks to https://www.youtube.com/watch?v=ZtfCeFaHtGo
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

Make this backup while the stock firmware is still on the radio. (Test 17 and later answer the same read command, but a backup taken before changing firmware is the one to keep.) Switch the radio on normally with the cable connected:

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

Switch the radio on normally, then off **without pressing PTT** (see [Known problems](#known-problems)). Hold PTT1 and switch on. The screen stays white: the bootloader is waiting.

### 4. Flash

```sh
.venv/bin/python tools/flash.py probe --model uv98                 # optional: connect only, erases nothing
.venv/bin/python tools/flash.py flash --model uv98 --image bin/uv98plus_openfw_test22.bin --yes
```

It takes about 30 seconds: 246 blocks of 1 KB, each acknowledged. When it prints `final reply 6`, switch the radio off and on normally. To go back to stock, flash `uv98plus_V1.16_250915.bin` the same way.

The flasher checks the image before sending it and writes only the application area (from 0x08002800), never the bootloader. Every 1 KB block carries a checksum and is acknowledged by the bootloader, and the bootloader only starts an application whose stack pointer is valid. So bootloader mode is always there to flash again. We have flashed one radio dozens of times, including back to stock.

## Using the radio

| Key | Action |
|---|---|
| 0-9, then # | Type a frequency in kHz and tune to it (red bar clears the entry). In channel mode: go to that channel number |
| # (nothing typed, V/M) | Switch between VFO mode and channel mode |
| # held 1.5 s | Reverse: listen on the repeater input (or on a split channel's transmit frequency); hold again to swap back |
| * | Swap VFO A and VFO B (each keeps its frequency, mode, step, shift, offset and tones). The main VFO is at the top; the other is on the small line under the main frequency, receive only. In channel mode it goes back to VFO mode first |
| * held 1.5 s (lock symbol) | Lock or unlock the keys (PTT still works) |
| Up / down | Tune by the step set in the menu (12.5 kHz by default); held, it repeats. In channel mode: next / previous used channel |
| Green bar | Open the menu |
| Red bar | Clear a half-typed frequency or channel number |
| PTT2 or monitor (side keys) | Open the squelch while held |
| PTT1 | Transmit |
| Alarm button (top) | Transmit a 1750 Hz burst while held; pressed while PTT1 is held, it replaces the voice until released |

**Menu.** The green bar opens the menu at its groups. In the menu:
- Up/down picks a row. The green bar, or the row's number key, opens a group, starts an action, or edits a value. While editing, up/down change the value, which takes effect immediately; the green bar keeps it and the red bar undoes it.
- The red bar goes back a level, and leaves the menu from the top level.
- `#` in a tone item: jumps to the next tone type (off, CTCSS, DCS normal, DCS inverted).

The groups and their items:
1. RX/TX: Band, Mode, Power, Step, Squelch, TX tone, RX tone, Shift, Offset, Time-out
2. Audio: Mic gain, AGC (USB and AM)
3. Memory: VFO / CH (VFO mode or channel mode), Channel
4. Scan: Scan, Freq detect. Scan is the band scan in VFO mode and the channel scan in channel mode
5. Settings: Dual watch (on at start-up), Backlight, Light off

## Serial commands

115200 baud, over the programming cable. Commands are not case sensitive. The firmware also prints a `FREQ` line by itself at start-up, after a frequency, VFO or channel change from the keypad, when a scan stops on a signal, and when frequency detect finds one.

| Command | Action |
|---|---|
| `PING`, `VER`, `HELP`, `CLK` | Check the link, firmware version, command list, system clock |
| `F [MHz]` | Show or set the frequency (to 10 Hz) |
| `M FM\|NFM\|AM\|USB` | Set the mode |
| `SQ 0-9`, `VOL 0-63` | Squelch level (0 = open), receive audio level |
| `MIC [0-31]` | Show or set the mic gain (default 25); works while transmitting |
| `POW [LOW\|MID\|HIGH]` | Show or set the transmit power (default MID) |
| `T [OFF\|88.5\|D023N\|D023I]` | Transmit tone: CTCSS in Hz, or DCS normal / inverted |
| `RT [OFF\|88.5\|D023N\|D023I]` | Receive tone squelch (FM / NFM) |
| `SHIFT [OFF\|+\|-]`, `OFFSET [MHz]` | Repeater shift and offset |
| `VFO [A\|B]` | Show or select the VFO |
| `PTT ON`, `PTT OFF`, `PTT` | Transmit from the computer. `PTT ON` must be repeated at least every 2 seconds or transmit stops (`PTT TIMEOUT`); band limits, power and the time-out timer apply |
| `DW [ON\|OFF]`, `SCAN ON\|OFF`, `FDET` | Dual watch, scan (band in VFO mode, channels in channel mode), frequency detect |
| `CH [n]`, `CHLIST [n]`, `VFOMODE` | Show or go to channel n; list the next 10 used channels from n; back to VFO mode |
| `SAVE`, `SETTINGS` | Save settings now; show whether settings were loaded at start-up |
| `CAL`, `SPIID`, `SPIRD addr [len]`, `AGC [ON\|OFF]`, `W1 reg value` | Calibration source, flash chip ID, read flash (hex), USB/AM AGC, write a radio-chip register (transmit registers refused) |
| `RSSI`, `ADC` | Signal strength; side-key and battery readings |
| `R1 xx`, `R2 xx` | Read register xx of radio chip 1 or 2 |
| `FONT 1\|2`, `EDGE 0-3`, `PAL 1\|2` | Screen style (defaults FONT 2, EDGE 2, PAL 2) |

## Satellite tracker

`tracker/` is a web page that lists the amateur satellites passing over you (those heard in the last 24 hours on AMSAT's status page) and tunes the radio to a downlink with one click, Doppler-corrected, over the programming cable, and with Follow Doppler on keeps retuning during the pass. Set your location in `tracker/config.json`, then:

```sh
.venv/bin/python tracker/server.py
```

and open http://localhost:8766. Details, including WSL networking and loading your own keps: [tracker/README.md](tracker/README.md).

## Rig control (Hamlib)

`tools/rigctld.py` makes the radio look like a Hamlib network rig, so programs such as gpredict, WSJT-X, fldigi and CQRLOG can tune and key it over the programming cable:

```sh
.venv/bin/python tools/rigctld.py            # listens on localhost port 4532
```

In the program, choose Hamlib model 2 ("NET rigctl") at `localhost:4532`. It supports frequency, mode (USB, AM, FM, narrow FM), VFO A/B, PTT and signal strength. While keyed it repeats the firmware's keep-alive, and it unkeys if the program disconnects. The firmware's band limits, power and time-out timer still apply. Run either the tracker or the bridge at a time; both use the same cable.

## Transmit limits

- **Allowed:** amateur bands of all three ITU regions that the radio chip can tune: 17 m, 15 m, 12 m, 10 m, 8 m, 6 m, 4 m, 2 m, 1.25 m, 70 cm, 33 cm and 23 cm. See `tx_ranges` in `firmware/src/main.c`.
- **Marine VHF** 156.000-162.025 MHz, capped at MID power. Marine use normally needs a marine licence and type-approved equipment.
- **CB** 26.965-27.405 MHz, capped at MID power, narrow FM.
- **PMR446** 446.0-446.2 MHz, LOW power only.
- **Channels marked "Only Rx"** never transmit.
- **Everything else is refused,** including the air band. Whether the radio actually radiates on HF, CB, 6 m, 4 m, 33 cm and 23 cm has not been tested.
- **Power:** MID by default. LOW, MID and HIGH use the radio's calibration with the band's amplifier on, as stock does.
- **Time-out timer:** 60 seconds by default (menu), then locked out until the key is released. Watchdog on.

Check your local rules before transmitting.

## Known problems

- **Switching off while transmitting.** If the radio is switched off while transmitting, the radio chip keeps its transmit settings, and the next power-on into bootloader mode transmits until switched off. A normal power-on clears it. **Release PTT before switching off.** The stock firmware has the same gap. Test 16 and later stop transmitting when the supply voltage drops, which should close the gap, but this has not been tested on the radio yet.
- **Signal meter not calibrated.** The reading follows the receiver gain, so an empty channel shows about S9+10. Calibrating it needs a gain correction and a reference signal.

## Building from source

```sh
cd firmware
make
```

This produces `firmware/build/uv98plus_openfw.bin`, built with gcc-arm-none-eabi 13.2 and newlib 4.4 from Ubuntu 24.04. The font headers are included, so regenerating them (`make font`) is only needed after changing fonts; it needs `python3-pil` and `fonts-dejavu-core`.

## Files

| Path | What it is |
|---|---|
| `setup_ubuntu.sh` | Installs everything (compiler, Python packages in `.venv`) |
| `firmware/` | Source (`src/main.c`), build (`Makefile`, `rt880_app.ld`), fonts (`src/fonts_aa.h`, made by `tools/mkfont_aa.py`), image checker, and the parts of Artery's AT32F423 library the build uses |
| `bin/` | The current firmware image and its `SHA256SUMS` |
| `tools/flash.py` | Bootloader flasher (`probe` / `flash`, `--model uv98`; `--model rt880` for Radtel RT-880, untested) |
| `tools/backup_spi.py` | 4 MB SPI flash backup (stock firmware) |
| `tools/extract_upgrade_tool.py` | Extracts the official firmware from iRadio's upgrade tool |
| `tools/rigctld.py` | Hamlib network (rigctld) bridge |
| `tracker/` | Satellite tracker |
| [FINDINGS.md](FINDINGS.md) | The chips, the hardware, the protocols and the tooling, as worked out from the stock firmware and the radio |
| [TODO.md](TODO.md) | Where the work stands: what has been tested and what is next |

Not included: Radtel/iRadio firmware, vendor tools, decompiled stock code and datasheets. The stock firmware was studied in Ghidra (raw binary, ARM Cortex, base address 0x08002800); the results are in FINDINGS.md.

## Licence

The code in this repository is licensed under the GNU General Public License version 3 ([LICENSE](LICENSE)).

Additional permission under GNU GPL version 3 section 7: if you modify this program, or any covered work, by linking or combining it with the Artery AT32F423 firmware library (`firmware/lib/at32f423`) or with bitmap glyphs generated from the DejaVu fonts (`firmware/src/fonts_aa.h`), containing parts covered by the terms of their own licences, the licensors of this program grant you additional permission to convey the resulting work.

Third-party parts keep their own licences; see [ATTRIBUTION.md](ATTRIBUTION.md).

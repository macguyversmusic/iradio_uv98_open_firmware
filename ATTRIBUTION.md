# Attribution and third-party licences

The firmware (`firmware/src/main.c`, `firmware/rt880_app.ld`, `firmware/Makefile`), the tools (`tools/`, `firmware/tools/`), the analysis scripts (`analysis/`) and the documents were written for this project. They are under the GNU GPL version 3, with the additional permission given in [README.md](README.md#licence).

## Included in this repository, and built into the firmware

### Artery AT32F423 firmware library
- Where: `firmware/lib/at32f423/`, and `firmware/src/at32f423_conf.h` (Artery's configuration template with the modules this firmware uses switched on).
- From: https://github.com/ArteryTek/AT32F423_Firmware_Library, commit a60472b (2026-07-16). Only the CMSIS core headers, device support files and peripheral drivers are included, unmodified.
- Built into the firmware image: `system_at32f423.c`, `startup_at32f423.s` and the crm, gpio, usart, spi, dac, misc, wdt and adc drivers.
- Licence: the Clear BSD License, Copyright (c) 2023, Artery-MCU ([LICENSES/BSD-3-Clause-Clear_Artery.txt](LICENSES/BSD-3-Clause-Clear_Artery.txt), also in `firmware/lib/at32f423/LICENSE`). The file headers also carry Artery's notice allowing use, copying and distribution for development with Artery microcontrollers; this firmware runs only on an Artery AT32F423.

### Arm CMSIS core headers
- Where: `firmware/lib/at32f423/cmsis/cm4/core_support/` (part of Artery's library).
- Licence: Apache License 2.0, Copyright (c) 2009-2020 Arm Limited ([LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt)).

### DejaVu fonts (screen font)
- Where: `firmware/src/font.h` holds 16x24 bitmaps of printable ASCII, generated from DejaVu Sans Mono Bold by `firmware/tools/mkfont.py`. The font file itself is not included.
- Licence: Bitstream Vera fonts copyright (c) 2003 Bitstream, Inc.; DejaVu changes are in the public domain; Arev glyphs copyright (c) Tavmjong Bah ([LICENSES/DejaVu-fonts.txt](LICENSES/DejaVu-fonts.txt)).

## Credits: information used, no code copied

- **nicsure**: [RT880-FlasherX](https://github.com/nicsure/RT880-FlasherX) (no licence file) and [RT880-WebSerial-Firmware-Flasher](https://github.com/nicsure/RT880-WebSerial-Firmware-Flasher) (GPL-2.0). `tools/flash.py` is our own Python, but it follows the bootloader packet sequence these flashers use.
- **rampa069**: [RT880-misc](https://github.com/rampa069/RT880-misc). It provided Radtel's RT-880 upgrade tool scripts and protocol notes, the processor part number, and the UV-98 Plus V1.03 firmware as Intel HEX, which we used for comparison.
- **DualTachyon**: [radtel-rt-890-oefw](https://github.com/DualTachyon/radtel-rt-890-oefw) (Apache-2.0), open firmware for the related RT-890. We used its BK4819 register values as a reference to identify the radio-chip driver in the stock firmware. A line-by-line comparison finds no shared code beyond standard Artery library calls.
- **CHIRP**: https://github.com/kk7ds/chirp (GPL-3.0). `tools/radtel_vfo.py` uses the memory layout from CHIRP's Radtel RT-880G driver.
- **Radtel and iRadio**: we worked out the hardware details (pins, radio-chip register values, calibration layout, programming protocols) by reading their stock firmware in Ghidra. No stock firmware, vendor tool or decompiled stock code is included in this repository.

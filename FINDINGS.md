# UV-98 Plus / RT-880G: findings (2026-11-13)

Sources: Radtel RT-880 stock firmware V1.16 (`RT-880-V1.16.bin` from https://github.com/nicsure/RT880-WebSerial-Firmware-Flasher, "RT-880 Radio V1.16 2025-09-15"), the radio itself over the programming cable, Artery's AT32F423 datasheet V2.02 (from Artery's website), DualTachyon's RT-890 open firmware (https://github.com/DualTachyon/radtel-rt-890-oefw).
The official UV-98 Plus V1.16 has the same code as the RT-880 V1.16 (see "Official UV-98 Plus V1.16" below). Function addresses (0x80xxxxx) refer to V1.16. Decompilations are not published; they were made with Ghidra (raw binary, language ARM:LE:32:Cortex, base address 0x08002800, functions seeded from the interrupt vector table).

## Processor and memory
- AT32F423-family register map (clock 0x40023800, flash controller 0x40023C00, GPIO A/B/C at 0x40020000/0400/0800). No AT32F421 or AT32F403 addresses in the firmware.
- Application at 0x08002800, ending exactly at 0x08040000: 10 KB bootloader + 246 KB application = 256 KB flash. Fits the AT32F423 xC parts (e.g. AT32F423RCT7); package not known.
- Initial stack pointer 0x200099C8, so at least 39 KB RAM.
- External SPI flash: 4 MB. The clone protocol reads all 4096 x 1 KB blocks; addresses wrap every 0x1000 blocks. We keep a full dump of our radio (not published: it holds that radio's calibration; `tools/backup_spi.py` makes one). It holds data only (no firmware copy, no ARM code density).

## Serial ports (from the interrupt vectors and handlers)
- USART6 (vector 71, handler 0x8024624) = programming port. Pins PA11 = USART6_TX, PA12 = USART6_RX (mux 8; datasheet lists USART6 on these pins). PA11/PA12 are also the chip's USB OTG FS D-/D+, so the side USB-C data lines go to the processor's USB pins.
- USART2 (vector 38, handler 0x80244E4) = GPS. RX on PA3 (mux 7). Parses NMEA `$...*cs`, checks for RMC. When a flag is set, each received GPS byte is forwarded through the send routine 0x801C028 (the same routine that sends the programming-port replies), i.e. GPS pass-through to the PC.

## Programming-port command set (complete, from handler 0x8024624 and block handler 0x801BDC8)
| First byte | Frame | Action |
|---|---|---|
| 0x34 | 5 bytes, byte sum | `34 52 05 10 9B` enter (reply 06); `34 52 05 EE 79` exit. After writes, exit saves a calibration or settings backup and reboots. |
| 0x52 'R' | 4 bytes `52 hi lo sum` | Read 1 KB of SPI flash, reply `52 hi lo` + 1024 + sum |
| 0x90-0xA3, 0x40 '@' | 1028 bytes `cmd hi lo` + 1024 + sum | Write a flash region; block 0 erases the whole region first; reply 06 |
| 0x2A '*' | 14 bytes `*YYMMDDhhmmss#`, ASCII digits, no checksum | Set the clock (year >= 24, month <= 12, day <= 31, hour <= 23, min/sec <= 59) |
| anything else | | reply FF |

There is no command for live frequency or mode.

## Settings bytes (settings region, block 0)
- 0xD6 GPS On-off, 0xD7 GPS baud index, 0xD8 UTC zone: confirmed against the vendor app (Off / 9600 / UTC 0 = 0 / 1 / 0).
- 0xD9: next item in the radio's GPS menu, "PC Monitor" (options "Audio Mode", "Program Mode"). Not in the vendor app or CHIRP. Value on this radio: 0. Meaning of 0 not confirmed.
- 0xCEE, 0xCEF: unnamed in CHIRP, next to the APRS fields; candidates for "TX Data Upload" / "RX Data Upload". Both 0. Not confirmed.

## Pins (mux assignments, from calls to the mux function 0x80254F0, checked against the datasheet)
| Pins | Mux | Function |
|---|---|---|
| PA11, PA12 | 8 | USART6 TX/RX (programming port) |
| PA3 | 7 | USART2 RX (GPS) |
| PB13, PB14, PB15 | 5 | SPI2 SCK/MISO/MOSI (likely the SPI flash) |
| PA15, PB3, PB5 | 6 | SPI3 CS/SCK/MOSI, no MISO (likely the display) |
| PC0, PC1 | 7 | I2C1/I2C3 or USART6/7 per datasheet; not resolved |

- Input reads (function 0x80254DE): PA2, PA10, PB4, PB6, PC10, PC11, PC12, PD2.
- Driven outputs seen: PA0, PA1, PA6, PA8, PA9, PA13, PA14, PB0-PB2, PB7, PB9, PB10, PB12, PB15, PC3-PC9, PC13-PC15, PF0, PF1, PF6, PF8.
- PA13/PA14 (the SWD debug pins) are driven as outputs. On the RT-890 they are the red/green LEDs.

## Radio chips (from the Ghidra decompilation of V1.16)
Two BK48x9-type chips, each on its own bit-banged 3-wire bus. Protocol for both: select low, register byte (OR 0x80 to read), then 16-bit value, MSB first; clock low, set data, delay, clock high, delay. Reads switch the data pin to input and clock in 16 bits.

| Chip | Select (SCN) | Clock (SCK) | Data (SDA) | Write / read functions |
|---|---|---|---|---|
| BK4819 #1 | PA8 | PF6 | PC9 | 0x801E660 / 0x801E620 |
| BK48x9 #2 | PC7 | PC8 | PC6 | 0x801E6D4 / 0x801E698 |

- 0x801E660 is called 35 times with exactly the RT-890 source's BK4819 register/value pairs (e.g. 0x71 = 0x25F3, 0x72 = 0x41DC), which identifies it as the BK4819 write.
- On the RT-890 the BK4819 uses PB5 SDA, PB8 CS, PB9 SCL; the RT-880 wiring is different.
- 0x80093E2 (134 calls) draws text on the display, not a chip driver.

## Transmit path
- TX start for chip 1: 0x80053F0 (sets PF0 by band state, enables TX via 0x80052D4, then calls 0x8005354, sets power via register 0x36, marks state 3). TX start for chip 2: 0x8003F58 (TX enable via 0x8003ED6).
- 0x80052D4 = BK4819 TX enable: reg 0x37 = 0x1D0F, reg 0x52 = 0x028F, reg 0x30 = 0x0200, then reg 0x30 = 0xC1FE or 0xC3FA (same as the RT-890 driver). Receive is reg 0x30 = 0xBFF1.
- 0x8005354 = TX amplifier path on, called only from the two TX start routines: clears PC15 and PB11, then by band number: 1 -> PC15 high; 2 -> PC15 and PB11 high; 3 -> BK4819 reg 0x33 bit 3; 4, 5 -> BK4819 reg 0x33 bit 2.
- 0x8005318 = amplifier off: clears PC15 and PB11.
- PF0 is switched in TX start and in other functions (band/filter path select).
- To keep a test firmware from transmitting: hold PC15 and PB11 low, never write reg 0x30 with 0xC1FE/0xC3FA, keep reg 0x33 bits 2 and 3 clear.

## Display
- ST7789 controller (init sequence in 0x8017E48: 11, 36=A8, 3A=05, B2, B7, BB, C0, C2, C3, C4, C6, D0, E0, E1 are ST7789 commands). 16-bit colour, landscape.
- SPI3: PB3 SCK, PB5 MOSI, PA15 CS. Data/command = PC13 (low = command, 0x8018184). Reset = PC14 (high, low pulse, high, 120 ms wait). Byte send = 0x80181A8. A DMA interrupt (vector 11) is in use, probably for pixel transfers.
- Backlight: DAC channel 1, output PA4 (datasheet: PA4 = DAC1_OUT). 0x801FC44 writes brightness level 0-4 as 60, 65, 70, 76, 255 to 0x40007410 (DAC 8-bit data register, by the STM32-style DAC layout; not yet checked in Artery's reference manual).
- 0x801E26C (display wake) applies the brightness and, if settings byte 0x60 is non-zero, sets PA9. 0x60 sits before brightness (0x61) and LCD timer (0x62), so PA9 is probably the keypad LED.

## Other chips and functions
- APRS modem (probably a CML part on its C-BUS interface): chip select PF1. 0x8005C04 sends reset 0x01, then 3-byte register writes to 0xE0, 0xE1, 0xE2 (one value from settings byte 0xF2). Byte send 0x8005E90; PA0/PA1 are toggled with PF1 in small functions and are likely its clock/data. That the chip is fitted in this radio is not confirmed.
- Key-beep generator: run from the TMR1 interrupt (0x80182BC); tones 0x1B8 = 440 Hz and 0x2E4 = 740 Hz via 0x80059AC; at the end of a countdown it sets PF8 (possibly the audio amplifier).
- Keypad scanning runs from the TMR1 interrupt (0x8021204 calls the column driver 0x801FBA0).

## Open firmware results on this radio (2026-11-13)
- The bootloader only starts an app whose initial stack pointer is below the end of RAM (0x2000C000 was refused; 0x20009000 runs). Stock images use 0x20009290-0x2000ADC8.
- The bootloader leaves USART6 and its interrupt enabled; the app must disable the NVIC lines and reset USART6 before using it, or the first received byte lands in the default handler.
- Display drawing that matches stock: controller column = 319 - y, row = x, pixels per x column bottom to top. PA14 is the red LED.
- Both BK48x9 chips answer register 0x00 = 0x4819.
- Receive audio (test 6): speaker plays after the app sets the stock start-up pin states (0x8011AC8: PB7 high; PA0, PA1, PA9, PB9, PC3, PC4, PC5, PF1 low), with PF8 high = speaker amplifier on, 0x48 = 0xB32F (FM) and 0x47 audio mode 1. Stock drops PB7 and raises PC3 only in the APRS beacon sender (0x80216F0); voice transmit leaves PB7 high and PC3 low.
- Squelch readings on PMR 1 (446.00625 NFM), this radio: empty channel RSSI about -80 (raw 160), noise (0x65 & 0x7F) 78-83, glitch (0x63) 60-88; UV-5R keyed nearby: RSSI about -29, noise 13-15, glitch 0. Stock squelch state is BK4819 0x0C bit 1 (0x80051E4), thresholds 0x4F / 0x78 from calibration.
- Test 7: noise squelch (FM/NFM opens below noise 70 - 5 * level, closes 10 above) is quiet on an empty PMR channel and opens on the UV-5R; confirmed on this radio.
- Test 4: receive on BK4819 #1 with the stock init (0x8004DC0 minus 0x8005698), stock tuning (frequency + calibration word - 0x8000 into 0x38/0x39), stock RX path (0x33 bits + PF0), RSSI from 0x67. Tone TX (1 kHz, low power 0x36 = 0x58FF, deviation 0x40 = 0x34E2, 0x30 = 0xC3FA, UHF amplifier on 0x33 bit 2) on 446.00625 MHz was heard on the UV-5R; three keyings ended on PTT release; registers returned to 0x30 = BFF1, 0x33 = 0020, 0x36 = 007F, 0x70 = 0000. PTT outside PMR446 was refused.

- Switching off while transmitting (checked by the user, 2026-11-13, test 7): if the radio is switched off with the power knob while the open firmware is transmitting, every following power-on into the bootloader (PTT held, white screen) transmits (the test 7 tone was heard on a UV-5R on PMR 1) until the radio is switched off. It stops after one normal power-on, whose start-up writes TX off (PC15, PB11 low; 0x36 = 0x007F, 0x33 = 0, 0x30 = 0). Holding PTT at power-off brought it back. So BK4819 #1 keeps its registers through a power-off (how it stays powered is not known), and the bootloader does not reset it.
- Test 8 (voice transmit at minimum power, 0x30 = 0xC1FE, 0x36 = 0x20FF, external amplifier off): flashed 2026-11-13. Heard on the UV-5R on PMR 1: breath on the mic is audible, but the audio is low.
- Test 9 (stock transmit audio settings added: 0x44/0x45 = 0x8F88/0x3201, 0x74 = 0xE61C, 0x54/0x55 = 0x8F88/0x3201, 0x75 = 0xFA02, 0x31 bits 1 and 3 cleared, 0x28 = 0x6B38, 0x29 = 0xAB40, 0x51 = 0, 0x71 = 0x68DC; chip 2 reset and put to sleep at start-up; transmit allowed on the amateur bands): at mic gain 18 (0x7D = 0xE952, the stock value from settings 0xAD) the voice is audible only with the UV-5R turned up loud; at mic gain 31 (0x7D = 0xE95F) the voice is heard. The UV-5R was on narrow, the open firmware on NFM.

- Test 10 (flashed 2026-11-13): 1750 Hz burst on the alarm button (tone 1, 0x71 = 175 * 0x67, 0x70 = 0xC000, 0x47 = 0x6340, 0x30 = 0xC3FA) heard on the UV-5R (user); the serial log shows five bursts from the alarm button alone (the first three on 145.800 MHz FM, the old start frequency, at minimum power). The test 11 log later showed PTT1 presses and the burst switching in and out while PTT1 was held.
- Test 11 (flashed 2026-11-13; tested 2026-11-13/14 with the UV-5R on test channels 123-127 at the top of its memory):
  - Power: at MIN (level 8, no amplifier) the UV-5R's CTCSS decoder opens only intermittently and voice is poor; at LOW (446 MHz: 0x36 = 0x58FF with the UHF amplifier, 0x33 bit 2) voice is crystal clear at mic gain 25. So the low audio of tests 8 and 9 was mostly the weak minimum-power signal.
  - Power levels at 446.00625 MHz, 1750 Hz burst about 2 s each, battery input PA7 (ADC1 channel 7) idle 1380: MIN 1374, LOW (0x58FF) 1318, MID (0x84FF) 1295, HIGH (0xB0FF) 1258 during transmit. The sag grows with each level (6 / 62 / 85 / 122 counts, roughly 1 : 1.4 : 2 in current from LOW to HIGH), so the amplifier and calibrated levels work. With the stock formula ADC * 3 / 51 + 2 (0.1 V units, 0x80121A8) that is 8.3 V idle and about 7.6 V at HIGH. No RF power meter was used.
  - Transmit tones at LOW: CTCSS 88.5 opens the UV-5R's 88.5 tone-squelch channel; with the tone off the UV-5R shows the carrier but stays muted. DCS 023 normal opens the D023N channel and not the D023I channel; DCS 023 inverted the reverse.
  - Receive tone squelch (0x0C bit 10 / 14 / 15), UV-5R transmitting: RT 88.5, RT D023N and RT D023I each open only on the matching UV-5R channel and stay shut on the others.
  - 2 m: 145.500 NFM at LOW (calibration entry 0x14, VHF amplifier 0x33 bit 3): voice and the 1750 Hz burst heard on the UV-5R.
  - Time-out timer: holding the alarm button, the log shows TX ON 1750 then TX OFF TOT 60 S.
  - USB receive at 446.00525 MHz: the UV-5R's plain carrier on 446.00625 gives a steady 1 kHz whistle, plus a short burst (the UV-5R has squelch tail elimination on, which sends a tail tone at release; it also settles at key-up). Transmit stays FM in USB mode.
  - Starts on 446.00625 NFM.
- Clock (test 14): the bootloader starts the app on the 8 MHz internal clock (HICK); tests 1-13 ran at 8 MHz. Stock's clock setup (0x8021124: flash psr 0x154, PWC LDO 1.3 V, LICK and HICK on, PLL from HICK with ns 96, ms 1, fr 8, AHB /1, APB2 /1, APB1 /2) gives 48 MHz, because this chip's PLL output is halved (system_at32f423.c: HICK * ns / (ms * fr) / 2). The open firmware now uses the same setup; with fr 4 instead of 8 it would be 96 MHz (VCO 768 MHz either way).

- Test 15 (flashed 2026-11-14): repeater transmit writes the TX frequency (receive + or - offset, with the TX band's calibration word) to 0x38/0x39 before transmit is enabled, and checks band limits, power and amplifier on the TX frequency; tx_stop retunes receive. Checked over serial: VFO A 145.500 / B 433.500 swap; with SHIFT - and OFFSET 0.6 on 145.500, a serial PTT read back 0x39:0x38 = 0x00DD19AA = 144.90000 MHz + 0x1A (the 136-174 MHz correction), 0x30 = 0xC1FE; one PTT ON with no repeat gave PTT TIMEOUT and TX OFF within about 2 s. The Hamlib bridge (tools/rigctld.py) tuned, switched mode and VFO, held PTT on its 1 s keep-alive, and unkeyed when the client disconnected.
- RSSI: an empty channel reads about -80 dBm with raw / 2 - 160 (raw about 160), well above a real noise floor, so S-readings built on it read high (S9+10 on an empty 2 m channel with S9 = -93 dBm). Needs a reference signal to calibrate.
- RSSI against receive gain (test 16, 145.500 NFM, empty channel, W1 0x13, 2026-11-14): 0x03BE (stock) -83 dBm; 0x03B8 -114; 0x0398 -115; 0x0378 -119; 0x0338 -128; 0x0318 -132; 0x00BE -106; back to 0x03BE -83. Noise (0x65) stayed 0x40-0x51 throughout. 0x67 follows the gain setting, so it is measured after the receive gain and is not the level at the antenna; a dBm reading needs a gain correction as well as a reference signal.
- Satellite listens (test 16, tracker with Doppler follow, stock antenna, RSSI once a second, 2026-11-14 Pisac): Meteor-M2-4 137.900 FM, ISS 145.825 NFM (max elevation 45°), AO-73 145.935 USB (41°), SONATE-2 145.825 NFM (89°), FO-29 435.795 USB (79°). No pass rose more than 2 dB above its noise floor (-76 to -82 dBm). One reading a second can miss short packet bursts, and a transponder is only heard while someone uses it, so this does not show whether the satellites were transmitting.

- Calibration map (stock 0x8012A30, edges inclusive, checked in this order): 136-174 MHz block 0 (base 135), 400-480 block 1 (400), 18-64 block 2 (15), 64-136 block 3 (60), 174-240 block 4 (170), 240-320 block 5 (240), 320-400 block 6 (320), 480-560 block 7 (480), 560-660 block 8 (560), 840-920 block 9, 920-1000 block 10, 1000-1080 block 11, 1080-1160 block 12, 1160-1240 and 1240-1300 block 13. Block 15 is empty on this radio.

- Test 16 (flashed 2026-11-14): SPI flash on SPI2 with chip select PB12 confirmed: JEDEC ID 0x856016 (Puya, 32 Mbit). Calibration loaded from flash (CAL FROM SPI FLASH); header 0x0000, block 1 at 0x0090 and settings at 0x2000 read live match the 2026-11-13 backup. AGC stays at step 0 on an empty channel; W1 refuses 0x30 and 0x36.
- SPI flash use by stock V1.16: programming regions (0x801BDC8, sector = 4 KB) 0x90 0x2000, 0x91 0x4000-0xFFFF, 0x92 0x1C000-0x3BFFF, 0x93 0x5C000-0x6BFFF, 0x94 0x7C000-0x7EFFF, 0x95 0x82000-0x84FFF, 0x96 0x88000-0x93FFF, 0x97 0x94000, 0x98 0xC6000-0xCDFFF, 0x99 0xD6000, 0x9A 0x100000-0x125FFF, 0x9B 0x126000-0x129FFF, 0x9C 0x14C000-0x163FFF, 0x9D 0x164000-0x187FFF, 0x9E 0x188000-0x197FFF, 0x9F 0x198000-0x19BFFF, 0xA0 0x19C000-0x19DFFF, 0xA1 0x19E000-0x342FFF, 0xA2 0x343000-0x351FFF, 0xA3 0x352000-0x3FFFFF; 0x40 = calibration sector 0. Own writes: 0x0000, 0x1000, 0x2000, 0x3000, 0x4000+ with backup at 0x10000+, 0x88000-0x93FFF, 0xD6000, 0xD7000, 0x12E000-0x14BFFF. The open firmware keeps its settings in sector 0xFC000, outside all of these.

## Radio chip settings used by the open firmware (BK4819 #1)
- Start-up (stock 0x8004DC0 without its call to 0x8005698): 0x00 = 0x8000, 10 ms, 0x00 = 0; 0x37 = 0x1D0F; 0x13 = 0x03BE; 0x12 = 0x037B; 0x11 = 0x027B; 0x10 = 0x007A; 0x14 = 0x0019; 0x49 = 0x2A38; 0x7B = 0x8420; 0x33, 0x34, 0x35 = 0; 0x1E = 0x4C58; 0x1F = 0xA656; 0x3E = 0xA037; 0x7D = 0xE950; 0x26 = 0x13A0.
- Tuning (frequency f in 10 Hz units): f + correction word - 0x8000; low 16 bits to 0x38, high bits to 0x39. Band edges and selectors from stock 0x8012A30. The correction words are per radio, read from the SPI flash calibration at 0x20 + band * 0x70. On this radio they are 0x801A (136-174 MHz), 0x8037 (174-240), 0x801E (320-400), 0x8040 (400-480) and 0x8000 elsewhere; `firmware/src/main.c` has this radio's values built in.
- Receive on: 0x37 = 0x1F0F, 10 ms, 0x30 = 0x0200, 0x30 = 0xBFF1.
- Receive filter (stock 0x8004FF0): 0x33 bit by band selector (1, 2: bit 0; 3: bit 4; 4: bit 5; 5: bit 1); PF0 low for selectors 3 and 4, high otherwise.
- Bandwidth 0x43: 0x3028 wide (FM, AM), 0x4048 narrow (NFM, USB). USB also sets 0x73 bit 4, as stock does for SSB.
- Audio mode 0x47 = 0x6040 | mode << 8 (0 mute, 1 FM, 3 tone, 5 USB, 7 AM). Receive gain 0x48 = 0xB000 | volume << 4 | 0xF (FM) or 0xC (AM, USB), from settings 0xAE-0xB1 (this radio 0xB32F / 0xB32C); volume is bits 9-4.
- RSSI in dBm = (0x67 & 0x1FF) / 2 - 160. Noise = 0x65 & 0x7F. Glitch = 0x63.
- Transmit (stock 0x800B22C, then 0x80053F0 / 0x80052D4): deviation 0x40 from the transmit frequency's calibration block, offset 3 (wide) or 10 (narrow), 0x34E2 in every block on this radio; mic gain 0x7D = 0xE940 + the mic gain setting (default 25; stock uses settings 0xAD, 0x12 on this radio); 0x47 = 0x6140 (mic) or 0x6340 (tone); 0x37 = 0x1D0F, 10 ms, 0x52 = 0x028F, 0x30 = 0x0200, then 0x30 = 0xC1FE (mic) or 0xC3FA (tone). Tone 1: 0x71 = Hz / 10 * 0x67, 0x70 = 0xC000 on. Power 0x36 = level << 10 | 0xFF (0x007F = off). This radio's calibrated levels at 446 MHz: low 0x16, high 0x2C (used with the external amplifier on).
- Transmit off: PC15, PB11 low; 0x36 = 0x007F; 0x33 = 0; 0x30 = 0.

## Channel memory (stock layout, used by channel mode)
- 1024 channels of 0x30 bytes at SPI 0x4000 (programming region 0x91). The same layout as `struct memory` in CHIRP's `radtel_rt880g.py`, checked byte by byte.
- +0 mode (0 FM or NFM, 1 AM, 2 USB), +3 narrow, +4-5 RX tone, +6-9 RX frequency and +10-13 TX frequency (10 Hz units, little-endian; FF FF FF FF = empty channel), +14-15 TX tone, +16 power (0 Low, 1 Medium, 2 High), +17 busy lock, +18 bits 4-0 time-out, +19 bit 7 scan remove, +32-47 name.
- Tones (16 bits, little-endian): bits 13-12 type (01 CTCSS, 10 DCS, 11 DCS inverted, 00 off), bits 11-0 the value (CTCSS in 0.1 Hz; DCS the code read as an octal number, so D023 is 0x013). Also as CHIRP's `struct tone`.

## Power off and start-up in the stock firmware (V1.16, cross-checked with V1.03)
- No power-knob detection: nothing in the firmware reacts to the knob. The power voltage monitor interrupt (IRQ 1) goes to the default handler; the only PWC reference (0x40007000) is the clock setup writing the core voltage select; there is no EXINT use. The ADC voltage monitor on PA7 is switched on, but its interrupt only counts conversions. The knob probably cuts the supply directly.
- No power-hold pin: the start-up pin setup (0x8011AC8) sets PB7, the keypad columns and PB12 high, and the off routine clears none of them.
- Off routine 0x8020F84 (used for low battery 0x8021E58, auto power-off and the "Radio Sleep" key 0x80219B4, and before every software reboot 0x801E308): LEDs off; FM broadcast receiver off if on; beep; chip 1 0x33 &= 0xFFCC; PC15, PB11 low and chip 1 0x33 &= 0xFFF3 (0x8005318); PC5 low; modem off; PB9 low; PA0, PA1, PF1 low (0x8002F30); chip 1 sleep 0x80052B0 (0x30 = 0x0000, 0x37 = 0x1D00); chip 2 sleep 0x8003EC0 (the same two writes); 100 ms; backlight DAC 0; PA9 low; screen black. Auto power-off and "Radio Sleep" keep the processor running with both chips asleep; low battery stops in a loop after "Please Charge !". The low-battery check is skipped while transmitting.
- Start-up resets both chips: after the pin setup (PC15, PB11 and the six bus pins low), the display setup and a wait for battery level 60, 0x8004DC0 writes chip 1 0x00 = 0x8000, 10 ms, 0x00 = 0x0000; 0x8003D34 does the same for chip 2 and then puts it to sleep (0x30 = 0, 0x37 = 0x1D00). Nothing touches the chips earlier, so on a stock power-on a chip left in transmit stays so for at least about 0.3 s.
- So stock has no protection against the knob being turned off during transmit either. The bootloader is not in any image we have, so what it does with the chips is not known.
- Not known: how BK4819 #1 keeps its registers through a power-off. The programming cable was connected during the tests, so the cable's idle serial line may be feeding power into the radio.

## Controls (from the user)
- Top: volume knob, which also clicks the power on and off, a separate HF antenna socket, and the VHF/UHF antenna. No channel knob (the PC0/PC1 rotary-knob code in V1.03 has no knob on this radio).
- Side: PTT1, PTT2 (Side Key 1) and monitor (Side Key 2). Top: the alarm button. Side USB-C port: programming cable.

## Stock voice transmit sequence (V1.16, BK4819 #1)
- PTT handler 0x8022270 (PB6 low for more than 4 scans), then 0x801F828: PA13 low; 0x43 = 0x4048 (narrow) or 0x3028 by the channel, the only place 0x43 is written, so transmit uses the receive bandwidth; 0x8004518: 0x31 bit 3 cleared (compander off), 0x28 = 0x6B38, 0x29 = 0xAB40; a full return to receive on the channel; 0x47 = 0x6040.
- 0x800B22C: PA13 low, PA14 high; optional start beep (settings 0xA5, 0 on this radio); speaker users released (PF8 low).
- 0x80053F0: 0x33 &= 0xFFCC; 0x47 = 0x6140; 0x38/0x39 = TX frequency + calibration word - 0x8000; PF0 by band; 0x51 = 0 (no tone; 0x8004388); 0x71 = 0x68DC and 0x31 bit 1 cleared (scrambler off; 0x8005218); 0x74 = TX 3 kHz coefficient (calibration header byte 9); 0x7D = 0xE940 + settings 0xAD (0x8004E90; menu "MIC Gain", 0-31); 0x40 = calibration word +3 (wide) or +10 (narrow) (0x8004996); 0x80052D4 (0x37 = 0x1D0F, 10 ms, 0x52 = 0x028F, 0x30 = 0x0200, 0x30 = 0xC1FE); amplifier by band (0x8005354); 0x36 = power entry << 10 | 0xFF.
- Audio response coefficients (0x80038D0, the same tables as the RT-890 open firmware): calibration header bytes 8-11 are indexes for TX 300 Hz (0x44/0x45), TX 3 kHz (0x74), RX 300 Hz (0x54/0x55), RX 3 kHz (0x75). This radio: 06 00 06 04, so 0x8F88/0x3201, 0xE61C, 0x8F88/0x3201, 0xFA02.
- Stock never writes 0x19, 0x2B, 0x46 or 0x7E.

## Stock transmit power, limits, tones, SSB and time-out (V1.16)
- Power (0x8012A30, 0x80053F0, 0x8004F0C): the band's 0x70-byte calibration block (SPI 0x20 + block * 0x70) holds 16 high entries at +0x10 and 16 low entries at +0x20, one per 5 MHz from the block's base frequency (index capped at 15). Low = low entry, high = high entry, medium = (high + low) >> 1. 0x36 = (entry & 0x3F) << 10 | 0x7F | 0x80. Blocks: 0 = 136-174 MHz (base 135), 1 = 400-480 (base 400), 2 = 18-64 (base 15), 3 = 64-136 (base 60), 4 = 174-240 (base 170), 9 = 840-920, 10 = 920-1000, 13 = 1160-1300 (base 1240 from 1240 MHz). On this radio the deviation words at +3 and +10 are 0x34E2 in every block; blocks 2, 3, 9, 10 and 13 have calibration word 0x8000 and mostly flat tables (0x37 / 0x19), except block 2 entries 2-3 (25-35 MHz), which are 0x23 / 0x26.
- Amplifier by selector (0x8005354): 1 (18-32 MHz) PC15; 2 PC15 + PB11 (never assigned); 3 (32-400 MHz) BK4819 0x33 bit 3; 4 (400-480) and 5 (840-1300) 0x33 bit 2.
- Stock transmit limits (0x8007E88): always refused at 108-136 MHz; four ranges at settings 0x88 (type, start MHz, end MHz) can block more (all type 0 on this radio). Tuning (0x800788C) is limited to 18-620 and 840-1000 MHz, then by the Work Range menu (settings 0x74: 0 = 64-620, the setting on this radio; 1 = 18-64; 2 = 840-1000).
- CTCSS (0x8004388, the same on transmit and receive): 0x51 = 0x9041, 0x07 = tone in 0.1 Hz * 0x19D / 200. 50-tone table at 0x8026298 (67.0-254.1 Hz).
- DCS: 0x51 = 0x802C (normal) or 0xA02C (inverted), 0x07 = 0x0AD7, 0x08 = codeword & 0xFFF, then 0x08 = (codeword >> 12) + 0x8000. Codeword (0x8009920 / 0x800994C, the same as the RT-890 open firmware): code + 0x800, low 12 bits reversed, Golay parity with 0xAE3, 23 bits reversed. DCS 023 gives 0x763813. 105-code table at 0x80262FC.
- Receive tone match (0x8004444): 0x0C bit 10 = CTCSS, bit 14 = DCS normal, bit 15 = DCS inverted; bit 11 ends the receive (squelch tail).
- 1750 Hz: side-key function "Send Single Tone" (frequency settings 0xA2, 1750 on this radio): 0x47 = 0x6340, 0x30 = 0xC3FA, 0x70 = (read & 0xFF) | 0xC000, 0x71 = 1750 / 10 * 0x67. A toggle with no time limit.
- SSB: one SSB/CW mode, no USB/LSB choice and no frequency offset. Receive: 0x47 = 0x6540, 0x48 from settings 0xB0/0xB1 (as AM), 0x73 bit 4 set; software AGC (0x8024140) steps 0x13 through a 96-entry table from the averaged RSSI.
- Time-out timer: per channel (byte 0x12 & 0x1F): 0 off, 1-3 = 5/10/15 s, n >= 4 = (n - 2) * 15 s up to 600 s.

## Keys
- Keypad matrix 4 x 4: rows PC10, PC11, PC12, PD2 read together in 0x801FA9C; columns PB0, PB1, PB2, PB10 driven low in 0x801FBA0.
- Key map, from presses on this radio with test 3 (col = PB0/PB1/PB2/PB10 index 0-3, row = PC10/PC11/PC12/PD2 index 0-3). Top row is three buttons: green bar, up/down rocker, red bar.

  | | col 3 | col 2 | col 1 | col 0 |
  |---|---|---|---|---|
  | row 0 | 1 | 2 | 3 | green bar (menu) |
  | row 1 | 4 | 5 | 6 | up |
  | row 2 | 7 | 8 | 9 | down |
  | row 3 | * | 0 | # | red bar (exit) |

- Three side keys plus the alarm button on top. PTT1 = PB6 (low while pressed); in the stock code PB6 is read by 0x8022270, which calls 0x800B22C, a caller of the TX start routine 0x80053F0. Confirmed with PTT1, PTT2, monitor pressed in turn: one event, PB6 low then high, on the first press. Alarm (top), PTT2 and monitor change none of PA2, PA10, PB4, PB6.
- Alarm, PTT2 and monitor share one resistor ladder on PC2 (ADC1 channel 12). ADC setup 0x8002D64 samples channel 7 (PA7) and channel 12 (PC2) by DMA; 0x80121A8 stores PA7 as ADC*3/51+2 (probably battery voltage) and PC2 raw. Ladder decode 0x8013D68 (called from the side-key scanner 0x8007980, which also reads PB6):
  - PC2 < 1885: code 0x17 = Side Key 2 (settings 0xC8/0xC9)
  - 1885-2716: code 0x16 = Side Key 1 (settings 0xC6/0xC7)
  - 2717-3541: code 0x18 = Alarm key (settings 0xCA/0xCB) = the top button
  - >= 3542: none
  Code-to-setting pairing from the side-key handlers 0x8011D94, 0x801B374, 0x80207E0. Confirmed with test 5 on this radio: PTT2 = Side Key 1, monitor = Side Key 2. This radio's settings: Side Key 1 short = Spectrum, Side Key 2 none, Alarm short = Scanning, long = Frequency Detect.
- Single inputs (V1.16): PB6 = PTT1. PA2 = data-in of the APRS/data modem: read 8 bits at a time in 0x8005CD4 while PA1 pulses (PF1 select, PA0 data-out), sampled from the TMR3 interrupt 0x802152C; 0x8002F30 makes it an input. PA10: a status flag read every 0.5 s in 0x8022028. PB4: read once at start-up in 0x8003514, picks the modem type. PA2 is not a power-switch input.

## Bootloader (from `flash880.html` in https://github.com/nicsure/RT880-WebSerial-Firmware-Flasher, https://github.com/nicsure/RT880-FlasherX, and Radtel's upgrade tool in https://github.com/rampa069/RT880-misc `RT-880_V1.06_Upgrade_Tool_250417/update.py`)
- Entered by powering on with PTT held. 115200 baud.
- Packet `type hi lo data... sum`; checksum = byte sum + 0x52 (RT-880) or + 0x00 (UV-98 option).
- Radtel's tool: connect `39 33 05 10 D3`, update `39 33 05 55 18`, end `39 33 05 EE B1` (RT880-misc's Protocol.md says 0x24 for update; the code has decimal 24 = 0x18). The open flashers send the connect packet twice, then the update packet (this starts the erase), then type 0x57 blocks of 1 KB, each ACKed with 06.
- Radtel's tool subtracts 0x2800 from every hex address: the application starts at 0x08002800 and the bootloader is never written.
- None of the flashers can read back or verify. The connect packet alone erases nothing, so sending it with the 0x52 checksum is a way to tell the bootloader family.

## Official UV-98 Plus V1.16 (iRadio upgrade tool)
- `UV98plus V1_16 Upgrade Tool 250915__Slow.exe` (.NET, from iRadio's `UV98plus+V1_16+Upgrade+Tool+250915__Slow.zip`) embeds the firmware as UTF-16 Intel HEX at file offset 84,513: 11,390 records, 0 bad checksums, 182,160 bytes, 0x08002800-0x0802EF90, EOF record present. Version text "UV Radio V1.16 2025-09-15". `tools/extract_upgrade_tool.py` extracts it as `uv98plus_V1.16_250915.bin` (sha256 bf71002d58b5130269d2c0837c513645e8c4898e06a2fd89e91ee8ab187620ed).
- The tool's packets are stored as byte arrays at file offset 0x250-0x264: connect `39 33 05 10 81`, end `39 33 05 EE 5F`, update `39 33 05 55 C6`. All use checksum seed 0x00, so this radio's bootloader is the UV-98 variant (`flash.py --model uv98`).
- UV-98 Plus V1.16 and Radtel RT-880 V1.16 differ in 187 of 251,904 bytes, in two places: the model name ("UV Radio" / "RT-880 Radio") and the compressed initialised-data block at the end, where the shorter name shifts the following bytes. The code is identical.
- V1.16 250915 is the latest on Radtel's site (checked by the user, 2026-11-13). The bootloader checksum is the only difference between the brands: this radio answers FF to the RT-880 (0x52) connect.
- `UV98 Plus CPS_V1_07_250612.exe` (.NET, iRadio's programming software) contains no Intel HEX firmware.
- 2026-11-13: flashed `uv98plus_V1.16_250915.bin` onto this radio with `tools/flash.py --model uv98` (bootloader entered with PTT held at power-on; screen stays white in bootloader mode). Connect ACKed (the RT-880 connect gets FF), erase ACKed, 246 blocks ACKed in 32 s, final 06. The first attempt got no reply to its very first connect; the flasher now retries connect up to 5 times. Radio reports V1.16. A full clone read afterwards differs from the evening read in 2 bytes only (0x86000-0x86001, FF FF -> 00 00).

## This radio's firmware and the UV-98 Plus image
- The radio's Version menu shows "UV Radio V1.05 2025-02-14" (iRadio build, not Radtel's "RT-880 Radio").
- https://github.com/rampa069/RT880-misc `RT-880_V1.06_Upgrade_Tool_250417/firmware_data_uv.py` ("Firmware hex data for UV 98 Plus") holds a complete Intel HEX image: 121,596 bytes, 0x08002800-0x080202FC, all record checksums valid, version text "V1.03_241018". Converted to a 251,904-byte image (`uv98plus_V1.03_241018.bin`). `firmware_data.py` = RT-880 V1.06 250417.
- The repo's `firmware_UV98_1.05.bin` and `firmware_1.06/1.07/1.09/1.10.bin` are truncated: only the first 0xD800 bytes (the first 64 KB address segment) are present, the rest is zero. Not usable for flashing. The UV98 one equals the start of the V1.03 image.
- Hardware use compared between the RT-880 V1.16 and UV-98 Plus V1.03 decompilations:
  - Same: USART6 on PA11/PA12 (mux 8, vector 71); BK4819 #1 PA8/PF6/PC9 and #2 PC7/PC8/PC6; TX amplifier PC15/PB11 with the same on/off routines; SPI3 display PA15/PB3/PB5 with PC13 DC, PC14 reset and an identical ST7789 init; DAC backlight at 0x40007410 (UV levels 0, 60, 68, 76, 255); SPI2 flash PB13-15; keypad rows PC10-12, PD2; PB6 input.
  - Different in the UV build: no USART2 (GPS) interrupt; PA0/PA1 muxed (mux 8) instead of bit-banged; PC0/PC1 read as inputs instead of muxed; PA2, PA10, PB4 not read as inputs (V1.03's start-up pin setup 0x800C29C drives PA2 and PA9 low).
  - Every pin the test firmware drives as an output (PC15, PB11, PA13, PA14, PB0-2, PB10, PA8, PF6, PC9, PC7, PC8, PC6, PC13, PC14) is driven as an output by the UV build too. The test firmware leaves all other pins in their power-on state or as inputs.

## Stock firmware files available (all 251,904 bytes, application images)
- RT-880 V1.06, 1.07, 1.09, 1.10 and iRadio UV-98 1.05 (https://github.com/rampa069/RT880-misc), V1.14-1.16 (https://github.com/nicsure/RT880-WebSerial-Firmware-Flasher), V1.15 250612 and V1.16 250915 (https://github.com/nicsure/RT880-FlasherX).
- GPS/APRS text: V1.10 has 7 strings, V1.16 has 26, UV-98 1.05 has none (and no readable model or version text).
- The 4 MB SPI dump of this radio contains no model or version text, so this radio's firmware version is not known from the computer side.
- RT880-misc names the processor AT32F423RCT7 (LQFP64, 256 KB flash, 48 KB RAM), which agrees with the firmware analysis.

# Feature list

What the open firmware (test 19) does, and how to use each feature. Unless a feature says otherwise, it has been tested on the radio.

## Receiving

- **Receive 18-660 MHz and 840-1300 MHz**

  The radio listens in FM, NFM, AM or USB. To tune, type the frequency in kHz on the keypad and press `#` (for example `145500#`). The red bar clears a half-typed frequency. Up/down step by the menu step and repeat when held. Over the cable: `F 145.5`.

- **Modes: FM, NFM, AM, USB**

  Wide FM, narrow FM, AM for the air band, and USB for SSB and CW. Change it in menu RX/TX, Mode, or with the `M` command, for example `M USB`.

- **Band presets**

  10 m, 6 m, AIR, 2 m, MARINE, 220, 70 cm and 23 cm. Each one sets its usual mode, step and repeater offset, turns the shift off, and comes back to the last frequency you used in that band. Menu RX/TX, Band.

- **Noise squelch**

  Silences the speaker until a signal opens it. Levels run from 0 (always open) to 9, default 3. Menu RX/TX, Squelch, or `SQ 0-9`. Hold PTT2 or the monitor side key to open it by hand.

- **Receive tone squelch (CTCSS and DCS)**

  The speaker opens only for a signal carrying the tone you set: CTCSS, DCS normal or DCS inverted. Menu RX/TX, RX tone. In a tone item, `#` jumps to the next tone type. Over the cable: `RT 88.5`, `RT D023N`, `RT D023I` or `RT OFF`.

- **Signal meter**

  A CB-style S-meter at the bottom of the screen. Over the cable: `RSSI`. The readings are high: an empty channel shows about S9+10, so the meter is not yet calibrated.

- **LED colours**

  Green when receiving, yellow when a signal is held back by the tone squelch, red when transmitting.

- **Automatic gain control for USB and AM**

  Turns the receiver gain down on strong signals so they don't distort, and back up when they go. It is on by default. Menu Audio, AGC, or `AGC ON` / `AGC OFF`. Not yet tested on a real SSB signal.

- **Dual watch**

  The radio's second receiver chip listens on the other VFO, shown on the small line under the main frequency. When it hears a signal while the main receiver is quiet, the main receiver listens there too so you hear it, and the line turns green; the top of the screen and transmit stay on the main VFO or channel. It comes back 2 seconds after the signal ends, or at once on any key or PTT. On at start-up, also in channel mode. Menu Settings, Dual watch, or `DW ON` / `DW OFF`. Not yet tested with a signal on the other VFO.

- **Band scan**

  Steps through the current band preset by the menu step. It stops on a signal and carries on 3 seconds after the signal goes. Menu Scan, Scan, or `SCAN ON`. Any key stops it. Not yet tested on the radio.

- **Spectrum view**

  Shows the signal strength at 44 points around the current frequency, spaced by the menu step, as bars rising from the noise floor. Menu Scan, Spectrum. Any key stops it. Over the cable, `SPEC` prints one sweep as 44 dBm values. Fixed in test 18 (tests 16 and 17 read each point too soon); not yet seen on the radio.

- **Frequency detect**

  Finds the frequency of a nearby transmitter. Start it, then key the other radio close by. The radio tunes to what it finds, to the nearest 1 kHz, or shows NOTHING FOUND after 20 seconds. Menu Scan, Freq detect, or `FDET`. Any key stops it. Not yet tested on the radio.

## Transmitting

- **Amateur bands of all three ITU regions**

  PTT1 transmits voice on 17 m, 15 m, 12 m, 10 m, 8 m, 6 m, 4 m, 2 m, 1.25 m, 70 cm, 33 cm and 23 cm. Any other frequency is refused, including the air band. Only 2 m and 70 cm have been tested.

- **Marine VHF at up to MID power**

  156.000-162.025 MHz, capped at MID. Use it only where you are licensed to.

- **Power: LOW, MID, HIGH**

  Each level comes from the radio's own calibration, with the band's amplifier on, as stock does. The default is LOW. Menu RX/TX, Power, or `POW LOW`. In channel mode each channel uses its own power. The real output in watts has not been measured.

- **Transmit tone (CTCSS and DCS)**

  Sends a CTCSS tone or a DCS code with your voice, to open repeaters and tone-squelched radios. Menu RX/TX, TX tone, with `#` jumping to the next tone type. Over the cable: `T 88.5`, `T D023N`, `T D023I` or `T OFF`.

- **1750 Hz tone burst**

  Opens repeaters that need a 1750 Hz burst. Hold the alarm button on top of the radio to send it. If you press it while holding PTT1, it replaces your voice until you let go.

- **Repeater shift and offset**

  Transmits above or below the receive frequency. Menu RX/TX, Shift (off, + or -) and Offset (100 kHz to 12 MHz), or `SHIFT -` and `OFFSET 0.6`.

- **Reverse**

  With a shift set, `#` (with nothing typed) listens on the repeater input. Press it again to swap back.

- **Mic gain**

  Sets the microphone level, 0-31, default 25. Menu Audio, Mic gain, or `MIC 25`. Over the cable it also works while transmitting.

- **Time-out timer**

  Stops transmitting after a set time, 15 seconds to 10 minutes, default 60 seconds. After that, transmit stays locked out until you release the key. Menu RX/TX, Time-out.

- **Transmit cut-off at switch-off**

  If the supply drops while transmitting, for example when you switch off with PTT held, the firmware stops transmitting at once. Not yet tested on the radio. Until it is, release PTT before switching off.

## On the radio

- **VFO A and VFO B**

  Two independent VFOs, each with its own frequency, mode, step, shift, offset and tones. The main one is at the top; the other is on a small line under the main frequency, receive only. `*` swaps them.

- **Menu in groups**

  The green bar opens the menu at its five groups: 1 RX/TX, 2 Audio, 3 Memory, 4 Scan, 5 Settings. Up/down picks a row; the green bar or the row's number key opens a group, starts an action or edits a value. While editing, up/down change the value at once; the green bar keeps it and the red bar undoes it. The red bar goes back a level, and leaves the menu from the top. Not yet seen on the radio.

- **Tuning step**

  0.1, 1, 2.5, 5, 6.25, 10, 12.5, 25, 50 or 100 kHz. Menu RX/TX, Step.

- **Backlight level**

  Five brightness levels. Menu Settings, Backlight.

- **Backlight timeout**

  Turns the screen and keypad light off after 10, 20, 30 or 60 seconds without use, or never. The first key press only wakes the screen. Menu Settings, Light off.

- **Key lock**

  Hold the green bar for 1.5 seconds to lock or unlock the keys. PTT still works while locked.

- **Low battery warning**

  Shows LOW BATTERY when the battery drops to 6.8 V.

- **Boot splash and screen**

  M0FCC inverted in the middle, then "iRadio UV98+" and "Open Firmware v1.0". High-contrast screen with smooth fonts. The screen style can be changed over the cable with `FONT 1|2`, `EDGE 0-3` and `PAL 1|2`.

- **Saved settings**

  Both VFOs, power, mic gain, squelch, time-out, backlight, light timeout, band memories, channel mode and screen style are saved in the radio's flash 3 seconds after a change, and loaded at switch-on. `SAVE` saves at once; `SETTINGS` shows whether settings were loaded at start-up. Not yet tested on the radio.

- **Calibration from the radio itself**

  Frequency correction, deviation and power levels are read from the radio's own calibration in its flash, with built-in values as a fallback. `CAL` shows which were used. Tried on one radio so far.

## Channels

- **Channel mode**

  Uses the channels already in the radio, as programmed by the stock software or CHIRP: frequency, split, mode, tones, power and name. The red bar (with nothing typed), or menu Memory, VFO / CH, switches between VFO and channel mode; menu Memory, Channel steps through the channels. Up/down step through the used channels. Type a channel number and `#` to jump to it. On a split channel, `#` swaps receive and transmit. Over the cable: `CH 17`, `CHLIST` and `VFOMODE`. Not yet tested on the radio.

- **Programming with CHIRP or the stock CPS**

  The firmware answers the stock programming protocol, so CHIRP (model Radtel RT-880G) and iRadio's CPS can read and write the channels. After a write, the radio restarts. Not yet tested on the radio.

## From a computer

- **Serial control**

  Every setting above can be read and set over the programming cable at 115200 baud. `HELP` lists the commands; the README has the full table.

- **Transmit from the computer**

  `PTT ON` keys the radio and `PTT OFF` unkeys it. `PTT ON` must be repeated at least every 2 seconds, or transmit stops by itself. Band limits, power and the time-out timer still apply.

- **Hamlib rig control**

  `tools/rigctld.py` makes the radio look like a Hamlib network rig, so gpredict, WSJT-X, fldigi, CQRLOG and others can tune and key it. Run it, then choose Hamlib model 2 (NET rigctl) at localhost:4532. It unkeys if the program disconnects.

- **Satellite tracker**

  `tracker/server.py` serves a web page listing the satellites passing over you. One click tunes the radio to a downlink, corrected for Doppler, and Follow keeps it corrected for the whole pass. Auto steps through the satellites in view. Set your location in `tracker/config.json` and open http://localhost:8766.

- **Register access for experiments**

  `R1 xx` / `R2 xx` read a register on radio chip 1 or 2; `W1 reg value` writes one on chip 1 (transmit registers are refused). `SPIID` and `SPIRD` read the flash chip.

## Flashing

- **Flasher**

  `tools/flash.py` writes the firmware through the radio's bootloader. Power on with PTT1 held to enter it. It never touches the bootloader, so you can always flash again, including back to stock.

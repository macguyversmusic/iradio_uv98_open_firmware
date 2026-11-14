# Satellite tracker for the iRadio UV-98 Plus

A small web page that lists the amateur satellites passing over you and tunes the radio to their downlinks with one click, over the same cable used for flashing.

- **Which satellites:** those reported heard in the last 24 hours on AMSAT's live status page (status.amsat.org).
- **Passes:** worked out for your location, for the next hour or the next 24 hours, with a sky plot.
- **Downlinks:** from SatNOGS DB, with the uplinks and modes shown alongside.
- **Tuning:** one Tune button per downlink frequency and mode the radio can receive (transmitters sharing them are listed together). Clicking it sets the frequency and mode, corrected for Doppler at that moment. FM voice, AFSK packet and SSTV use NFM; FSK, GFSK and GMSK data (9600 bd) use wide FM, for a decoder on the radio's audio output; AM uses AM; SSB, CW and BPSK use USB.
- **Squelch:** with "Open the squelch during the pass" ticked, a Tune click opens the squelch when the pass starts (at once if it already has) and puts it back when the satellite sets or you stop following. A Tune click before the pass waits for it.
- **Signal:** a meter on the page shows the radio's signal strength, read over the cable twice a second.
- **Minimum elevation:** passes that never get higher than the chosen elevation (15° by default, `min_elevation` in `config.json`) are hidden.
- **Follow Doppler** (on by default): after a Tune click, the tracker keeps retuning as the shift changes. It retunes when the frequency has moved 50 Hz on USB or 500 Hz on FM, and stops when the satellite sets or you press Stop following.
- **Fine tuning:** buttons step the radio 100 Hz or 1 kHz. While following, a step is kept as a trim on top of the Doppler correction.
- **Auto:** steps through the downlinks of the satellites in view, highest first, staying on each for the time you choose (20 s to 2 min), with Doppler follow. A Tune click or Stop following ends it.

The radio must be running the open firmware (any test from 14 on); the tracker talks to it with the firmware's serial commands.

## Setup

From the repository folder, `./setup_ubuntu.sh` installs everything, including the Python packages in `.venv` (skyfield, numpy, pyserial). Then:

1. **Your location:** edit `tracker/config.json`. Set `name`, `lat`, `lon` (decimal degrees, south and west negative), `elev_m` and `timezone` (for example `Europe/London`). Leave `serial_port` as `null` to find the cable automatically, or set it, for example `"/dev/ttyUSB0"`.
2. **The radio:** plug in the programming cable (on WSL, attach it with usbipd as in the main README) and switch the radio on with the open firmware.
3. **Start:** `.venv/bin/python tracker/server.py`
4. **Open:** http://localhost:8766 in your browser. On Windows with WSL this works directly if WSL uses mirrored networking (Windows 11: add `networkingMode=mirrored` under `[wsl2]` in `%UserProfile%\.wslconfig`, then `wsl --shutdown`). Otherwise set `"host": "0.0.0.0"` in `config.json` and open the address WSL reports (`hostname -I`). That makes the page reachable from your network too.

## Keps (orbit data)

The tracker downloads keps itself and keeps them in `tracker/cache/`:
- SatNOGS, every 6 hours;
- CelesTrak (active satellites), every 2 hours;
- AMSAT's daily file, every 6 hours.

For each satellite it uses the newest set. To use your own keps as well, put them in `tracker/keps.txt` in the usual three-line format (name, line 1, line 2). They are used whenever they are newer.

## Files

| File | What it is |
|---|---|
| `server.py` | Web server: downloads, pass prediction, Doppler, radio control |
| `uv98.py` | Serial control of the radio (PING, F, M, RSSI, VOL, PTT, VFO), shared with `tools/rigctld.py` |
| `index.html` | The page |
| `config.json` | Your location, time zone, web port and serial port |
| `requirements.txt` | Python packages |

`cache/` (downloads) and `keps.txt` (your own keps) are created locally and not stored in the repository.

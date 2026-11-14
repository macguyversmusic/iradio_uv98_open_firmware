# Stock firmware analysis

The hardware details in [FINDINGS.md](../FINDINGS.md) come from Ghidra decompilations of the stock firmware. Those decompilations are Radtel/iRadio's code, so they are not published. This is how to regenerate them.

## Get the images

- UV-98 Plus V1.16: extract it from iRadio's upgrade tool with `tools/extract_upgrade_tool.py` (see the main README).
- Radtel RT-880 V1.16: https://github.com/nicsure/RT880-WebSerial-Firmware-Flasher (`RT-880-V1.16.bin`). It has the same code as the UV-98 Plus V1.16; only the model name differs.
- UV-98 Plus V1.03: the Intel HEX data in https://github.com/rampa069/RT880-misc (`RT-880_V1.06_Upgrade_Tool_250417/firmware_data_uv.py`).

All are 251,904-byte application images that load at 0x08002800.

## Decompile with Ghidra

Import settings, as recorded in our Ghidra projects: raw binary, language `ARM:LE:32:Cortex`, base address 0x08002800.

- `ghidra_scripts/SeedVectors.java` creates functions at every handler in the interrupt vector table and turns on the ARM aggressive instruction finder. Run it before auto-analysis.
- `ghidra_scripts/DecompileAll.java <output.c>` writes every function's decompiled C to one file, each headed `// FUN_xxxxxxxx @ xxxxxxxx`.

A headless command along these lines does both. We used Ghidra from the snap package, and this exact command has not been re-run:

```sh
analyzeHeadless ghidra_proj rt880_v116 -import RT-880-V1.16.bin \
  -processor ARM:LE:32:Cortex -loader BinaryLoader -loader-baseAddr 0x08002800 \
  -scriptPath analysis/ghidra_scripts -preScript SeedVectors.java \
  -postScript DecompileAll.java rt880_v116_decompiled.c
```

## Summarise hardware use

```sh
python3 analysis/hw_usage.py RT-880-V1.16.bin rt880_v116_decompiled.c
```

This lists the GPIO pins, pin mux settings, interrupt vectors and radio-chip register writes a firmware image uses. We used it to compare the RT-880 V1.16 with the UV-98 Plus V1.03.

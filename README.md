# gb-emu

A Game Boy / Game Boy Color emulator written in C, rendering through SDL2.
Built as a diploma project targeting Raspberry Pi Zero (works on desktop too).

## Build

Requires SDL2 (`brew install sdl2` / `apt install libsdl2-dev`).

```sh
make
./gb-emu path/to/game.gb
```

With no ROM it shows a demo pattern so you can verify the video pipeline.

## Controls

| Key | Button |
|---|---|
| W A S D | D-pad |
| Z | A |
| X | B |
| Enter | Start |
| Right Shift | Select |
| Tab (hold) | Turbo (fast-forward) |
| M | Menu (turbo speed setting) |
| Esc | Quit |

## Status

Done:
- SM83 CPU core (all unprefixed + CB-prefixed opcodes, flags, interrupts, EI delay, halt bug)
- Memory map with OAM DMA; IF/IE unused bits read as 1 (DMG behavior)
- PPU: mode timing, LY/STAT, background + window, sprites (8x8/8x16, flips,
  priority, 10/line limit, BG-over-OBJ priority), DMG palette
- STAT interrupts (mode 0/1/2 + LYC coincidence, rising-edge)
- Timer (DIV/TIMA/TMA/TAC) with overflow interrupt
- Joypad matrix with interrupt
- APU: 4 channels (2x pulse + sweep/envelope, wave, noise LFSR), frame sequencer,
  length/envelope/sweep, NR50/NR51 stereo mix, NR52 power, wave RAM,
  44.1 kHz high-pass output via SDL2 audio queue
- Cartridge loading + header parsing, MBC1 / MBC3 (+RTC) / MBC5 banking
  (RTC state persisted to a `.rtc` file next to the ROM)
- SDL2 frontend with frame pacing
- Headless runner (`build/rom_test`) with serial + screen-result detection
- Pixel-level PPU test (`make test`)

CGB (Game Boy Color):
- CGB mode auto-detected from header `0x143` (`0x80`/`0xC0`)
- Banked VRAM (2 banks) / WRAM (8 banks) with SVBK, VBK
- BG/window/sprite attributes: per-tile palette, VRAM bank, X/Y flip, priority
- CGB palette RAM (`FF68-FF6B`) with auto-increment
- HDMA/GDMA (`FF51-FF55`), KEY1 double-speed + STOP
- CGB boot register state applied on load

### Blargg results (15/16)

| Test | Result |
|---|---|
| cpu_instrs (01–11 + aggregate) | PASS |
| instr_timing | PASS |
| halt_bug | PASS |
| mem_timing | FAIL (needs M-cycle-accurate memory) |
| mem_timing2 | FAIL (same) |

mem_timing* require CPU memory accesses interleaved with T-cycles; the core
currently returns total cycles per instruction.

Acid tests:
- cgb-acid2: PASS (0 pixel diffs vs reference)
- dmg-acid2: PASS (expected "HELLO WORLD" screen)

TODO:
- Serial link
- M-cycle-accurate memory timing (mem_timing*)
- Blargg apu_* test ROMs not yet run (suite not in roms/tests)

Saves: battery carts write a `.sav` next to the ROM on quit and load it on boot.
MBC3+RTC carts also write a `.rtc` sidecar.

## Layout

```sh
include/   public headers
src/       cpu.c mmu.c ppu.c timer.c joypad.c apu.c cart.c gb.c main.c
tests/     ppu_test.c rom_test.c
roms/      put test/game ROMs here (not included)
```

## Testing

```sh
make test                              # PPU pixel tests
make build/rom_test
./build/rom_test roms/tests/cpu_instrs.gb 30000
```

CGB/DMG acid2 screenshots (not checked in): run `build/rom_test <acid2.gb> N out.bmp`.

## Resources

- Pan Docs (the GB hardware bible): https://gbdev.io/pandocs/
- SM83 CPU manual: https://rgbds.gbdev.io/docs/sm38
- Blargg test ROMs: https://github.com/retrio/gb-test-roms
- dmg-acid2 / cgb-acid2: https://github.com/mattcurrie/dmg-acid2

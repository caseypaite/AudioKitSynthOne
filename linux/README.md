# AudioKit Synth One — Linux x86_64 port

A native Linux build of the Synth One **synthesis engine**, plus a standalone
host with JACK/PortAudio output and ALSA sequencer MIDI input.

The DSP is the real thing: the original `S1DSPKernel`, `S1NoteState`,
`S1Sequencer`, `S1Arpeggiator`, `S1Rate` and `S1DSPCompressor` sources are
compiled in place from `../AudioKitSynthOne/DSP`, against a compatibility layer
that stands in for AudioKit and CoreAudio.

**The iOS UI is not ported.** UIKit, the storyboards, the PaintCode StyleKits,
and the Audiobus/OneSignal/AppCenter/Disk pods have no Linux equivalent — that
is a rewrite, not a port. What you get here is the engine and a headless host.

## Requirements

Arch Linux packages:

```sh
sudo pacman -S --needed clang cmake ninja pkgconf alsa-lib
sudo pacman -S --needed pipewire-jack   # or: jack2
sudo pacman -S --needed portaudio       # optional, second backend
```

Soundpipe is not packaged; it is fetched and built into the CMake binary
directory automatically. Nothing third-party is written into the source tree.

## Build

```sh
cd linux
cmake -S . -B build -G Ninja
cmake --build build
```

Offline (no network / air-gapped): build Soundpipe yourself and point CMake at
it with `-DSOUNDPIPE_ROOT=/path/to/soundpipe` (a tree containing
`libsoundpipe.a` and `h/soundpipe.h`, built with `make NO_LIBSNDFILE=1`).

Two binaries are produced:

| binary | purpose |
| --- | --- |
| `build/synthone` | standalone realtime host |
| `build/synthone-gui` | full graphical front end (Dear ImGui + GLFW) |
| `build/synthone-offline` | renders a preset to a WAV; no audio hardware needed |

## Run

```sh
# List what is available
./build/synthone --list-midi
./build/synthone --list
./build/synthone --list-params

# Play, driven by a MIDI keyboard
./build/synthone --backend jack --bank "Starter Bank" --preset 0

# No controller handy? Loop a test note to check the audio path
./build/synthone --backend portaudio --test-note 60
```

```
  [engine]  48000 Hz / 1024 frames / poly 6
  [audio]   jack  -> Ryzen HD Audio Controller Pro:playback_AUX0, ...
  [midi]    128:0 <- all sources
  [preset]  0: Init [Starter Bank]
  ...playing, Ctrl-C to quit
  voices  1   held   1   beat   0
```

`--backend` selects `jack` or `portaudio` at runtime; both are compiled in when
their libraries are present. JACK dictates its own sample rate and buffer size,
so `--rate`/`--buffer` apply to PortAudio only. MIDI arrives on an ALSA
sequencer port (`--midi CLIENT:PORT`, or `all`, the default).

Rendering without hardware:

```sh
./build/synthone-offline --bank BankA --preset 0 --note 60 --seconds 4 out.wav
./build/synthone-offline --note 69 --set arpIsOn=0 --set reverbMix=0 out.wav
```

## Install

```sh
./install.sh                            # ~/.local, adds a menu entry
sudo ./install.sh --prefix /usr/local    # system-wide
./uninstall.sh                           # removes it; your presets are kept
```

Binaries go to `<prefix>/lib/synthone`, runtime data to `<prefix>/share/synthone`,
and `<prefix>/bin` gets thin wrappers that point the binaries at that data --
necessary because the compiled-in resource path otherwise refers to this
checkout.

To hand it to someone else:

```sh
./package.sh          # -> dist/synthone-linux-<arch>.zip
```

The archive carries the binaries, their data and both scripts; `install.sh`
detects the packaged layout, so the recipient does not need the source. It is
not self-contained though -- alsa-lib, glfw, libGL, jack/pipewire-jack,
portaudio and libX11 must be present on the target.

### ARM64 (aarch64)

The sources carry no architecture-specific code -- no intrinsics, no inline
assembly, no `-march` flags -- so on ARM64 hardware the normal build works
unchanged:

```sh
cmake -S . -B build -G Ninja && cmake --build build
```

Soundpipe's only SSE is in kissfft, behind `USE_SIMD`, which this build does
not define (and Synth One uses none of the FFT modules).

From an x86_64 workstation you can build aarch64 binaries in an emulated
container:

```sh
docker run --privileged --rm tonistiigi/binfmt --install arm64   # once
./build-arm64.sh --package        # -> dist/synthone-linux-aarch64.zip
```

Emulated compilation is slow. `package.sh --build-dir DIR --arch NAME` packages
binaries built elsewhere, e.g. on real hardware.

### Raspberry Pi kiosk (boot straight into the synth)

`install.sh --kiosk` adds a systemd service that starts a bare X server on tty1
with `synthone-gui` as its only client. No display manager, no desktop, no
window manager -- the synth owns the panel, and the Pi's CPU goes to audio
rather than to a desktop session.

```sh
sudo ./install.sh --prefix /usr/local --kiosk --kiosk-user pi
sudo systemctl start synthone-kiosk        # or just reboot
journalctl -u synthone-kiosk -f            # watch it come up
```

It needs `xserver-xorg` and `xinit` (`apt install xserver-xorg xinit`) -- an X
server, but no desktop on top of it. Installing the kiosk also:

- takes tty1 from `getty` (`Conflicts=getty@tty1.service`), which `--uninstall`
  gives back;
- adds the kiosk user to `audio`, `video`, `input` and `tty`;
- writes `/etc/X11/Xwrapper.config` with `allowed_users=anybody` if that file
  does not already exist, since a service is not a console login. If it does
  exist, the script tells you what to add rather than editing it.

The service is enabled but **not started**, so you keep the console you ran it
from.

Settings live in `/etc/synthone/kiosk.conf`, never in the unit, and survive
reinstalls:

| Key | Default | Notes |
| --- | --- | --- |
| `BACKEND` | `portaudio` | ALSA directly, nothing to start first. `jack` if you run a server. |
| `GEOMETRY` | empty | Empty fills the panel; the official 7" display is `800x480`. |
| `TOP` / `BOTTOM` | empty | Which panel opens first: `MAIN ENV PAD FX SEQ TUNE`. |
| `MIDI` | `all` | ALSA source as `CLIENT:PORT`, or everything. |
| `HIDE_CURSOR` | `1` | Pointer off, for a touch panel. |
| `VT` / `DISPLAY_NUM` | `vt1` / `:0` | Which console and display to take. |
| `XSERVER_ARGS` | `-nolisten tcp -nocursor` | Passed to X. |
| `EXTRA_ARGS` | empty | Anything else for `synthone-gui`. |

The GUI flags this relies on -- `--fullscreen`, `--hide-cursor` and
`--compact` -- work on their own too, so you can try the kiosk configuration
inside a desktop session before committing to it:

```sh
synthone-gui --geometry 800x480      # exactly what the panel will show
```

To take the kiosk off without removing the app:

```sh
sudo systemctl disable --now synthone-kiosk
```

### Screenshots

`tools/screenshots.sh` captures one still per panel and assembles the slideshow
the top-level README shows. It runs the GUI on a throwaway X server sized to the
target panel, so it needs no display of its own and the frame comes out exactly
the panel's size:

```sh
./tools/screenshots.sh                      # -> ../screenshots/*.png + panels.gif
./tools/screenshots.sh --geometry 1024x600  # a different panel
```

The stills are gitignored; `screenshots/panels.gif` is the tracked artefact.

### Designing for 800x480

The official Raspberry Pi 7" display is **800x480**, under a third of the
pixels the roomy layout assumes. Below 1000x620 the GUI switches itself to a
compact mode; `--compact` and `--no-compact` force the decision either way.

What changes, and why:

- **The header wraps to two rows.** Laid out in one row it measures 1175px --
  387px wider than the whole panel -- and ImGui does not wrap a `SameLine`
  run, so KEYBOARD, MIDI LEARN and the voice count simply fell off the right
  edge, unreachable. Compact drops the wordmark, shortens the labels
  (`KEYBOARD` to `KEYS`, `MIDI LEARN` to `LEARN`) and gives the preset button
  whatever width is left over. Measured: 782px in an 800px viewport.
- **One panel at a time,** as the iOS app does it, switched by its own tab
  row. Two stacked panels leave about 116px each at this height, which is not
  enough for a single row of knobs; one panel with the keyboard hidden gets
  ~367px. On a desktop the arithmetic reverses and both panels are shown --
  see below.
- **The on-screen keyboard starts hidden,** and shrinks from 150px to 108px
  when you bring it back with KEYS. A box driven by a MIDI controller does not
  need it, and it costs a quarter of the height.
- **Chrome gives up pixels, touch targets do not.** Window padding goes
  12x10 to 6x5, item spacing 8x7 to 6x3 -- but frame padding *grows*, 8x5 to
  6x7, so buttons get taller rather than shorter, and scrollbars widen from
  12px to 16px. A control you cannot hit with a fingertip is worse than one you
  have to scroll to.

None of this is Pi-specific: it keys off the framebuffer size, so a resized
desktop window crosses the same threshold and a 1024x600 panel gets the same
treatment with more room to spend.

### One panel or two

How many panels are on screen is decided by the display, not by the user:

| Display | Layout |
| --- | --- |
| Below 1000x620 | One panel, its own tab row, keyboard hidden. |
| Desktop | Two panels stacked, UPPER and LOWER, each independently navigable. |

There is no layout button and no `--layout` flag. Offering the choice only
invited picking the arrangement that does not fit -- two panels on the 7" panel
leave 116px each, and one panel on a desktop wastes half the screen. Side by
side is gone entirely: panels are laid out wide, so splitting the width was the
one arrangement that made every panel scroll.

A stacked pane on a 900px screen is about 330px tall, so panels have to fit
that as well as the Pi's 367px. Anything sized in absolute pixels needs to come
from `GetContentRegionAvail()` rather than a constant -- the pitch wheel on TUNE
and the XY pads on PAD both do, and grow or shrink to whichever pane they land
in.

### Blocks, not rows

A full-width row per section wastes whatever the widest row does not use, and
that leftover repeats on every row. Instead each functional group is a
self-contained **block** sized to its own contents, and `BlockFlow`
(`gui/Panels.cpp`) packs blocks left to right, wrapping to a new shelf when the
next one will not fit.

Blocks fill both dimensions: a 2x2 stack of toggles sits beside a row of knobs
beside a stepper. MAIN is ten blocks on three shelves; SEQ fits all six
arpeggiator controls on one; FX holds nine blocks in four. Each block keeps its
own heading and border, so the grouping by function is more explicit than
headings stacked down the page.

Because the flow keys off available width, the same code packs FX into four
shelves at 800x480 and two at 1440x900.

Two rules worth knowing before adding a control:

- **Blocks never scroll.** A scrollbar inside one would steal width from the
  contents and truncate the captions, so they are created with
  `NoScrollbar`. A block whose declared height is too small silently clips
  instead -- if something disappears, the size passed to `flow.begin()` is
  wrong, not the layout.
- **Declare sizes with the helpers.** `kw(label)` and `kwRow({...})` mirror
  `Knob()`'s own width formula, which widens a cell when its caption is wider
  than the face. Measuring with a bare knob width under-counts a row and
  pushes it off the shelf.

### Knob sizes per panel

Compact scales knob faces to 70% and floors them at 32px, which is what lets
the busiest panel fit. Panels that finish above the fold have no reason to pay
that price, so each raises the floor for its own controls with a scoped
`s1gui::KnobFloor`:

| Panel | Face | Why |
| --- | --- | --- |
| MAIN | 72px | Ten blocks, three shelves, ~95% of the height. |
| ENV | 72px | Ten controls; the curve editors grow to 112px with them. |
| TUNE | 64px | Three knobs, finishes well clear of the fold. |
| SEQ | 52px | Cells are sized by their captions (`INTERVAL`, `TEMPO x`), so the face grows this far before the block widens and the arp row wraps. |
| FX | 32px | **At capacity.** Thirty-odd controls in four shelves, clearing the fold by ~10px. Measured: even a 36px face costs 12px across its three knob shelves and brings the scrollbar back. |
| PAD | -- | No knobs; the two XY pads take the full width and height instead. |

Raising one is a matter of trying it and looking: a bigger face widens the
block as well as heightening it, so it can reflow a shelf and cost more than it
gains. `tools/screenshots.sh` is the quickest way to check.

### Where presets live

Factory banks ship read-only in `AudioKitSynthOne/Presets/Data/` and are never
written to. Presets you save go to

```
$XDG_DATA_HOME/synthone/presets      (default: ~/.local/share/synthone/presets)
```

overridable with `--user-dir DIR` on any of the three binaries. A user bank
shadows a factory bank of the same name, and saving into a factory bank name
copies that whole bank into your directory first, leaving the shipped file
untouched -- so the source tree stays clean.

## How the port works

### Compatibility layer (`compat/`)

The upstream DSP sources are compiled **unmodified** except where noted below.
They resolve their Apple includes against `compat/`, which is placed ahead of
the DSP tree on the include path — while `DSP/Audio Unit` and `DSP/TAAE` are
deliberately left *off* it, so the replacements win.

| upstream dependency | replacement |
| --- | --- |
| `AudioKit/AKSoundpipeKernel.hpp` | `compat/AudioKit/AKSoundpipeKernel.hpp` — `AKDSPKernel`/`AKOutputBuffered`/`AKSoundpipeKernel` reduced to what the kernel derives from |
| `AudioKit/AKInterop.h` | `AK_ENUM` as a fixed-underlying-type enum |
| CoreAudio types | `compat/AppleTypes.h` — `AUValue`, `AUAudioFrameCount`, `AudioBufferList`, `AUMIDIEvent`, `AudioUnitParameterUnit`, `clamp`, `pow2`, `nil`, `__weak` |
| `S1AudioUnit.h` (Obj-C) | `compat/S1AudioUnit.h` — identical structs, `@protocol S1Protocol` becomes a C++ observer interface |
| `AEArray` (TAAE, Obj-C) | `compat/AEArray.h` — lock-free held-note snapshots over a fixed ring of preallocated generations; same macros and accessors |
| `AEMessageQueue` (TAAE, Obj-C) | `compat/AEMessageQueue.h` — bounded SPSC ring of tagged messages, drained by the host |
| `sp_port` (AudioKit's Soundpipe fork) | `compat/SoundpipeCompat.h` + `src/s1_port.c` — see below |
| Foundation/AVFoundation/AudioToolbox umbrellas | empty stubs |

`compat/S1Prefix.h` is force-included into every translation unit, standing in
for the standard headers Apple's libc++ supplies transitively (`<memory>`,
`<cfloat>`, …).

### Soundpipe

Stock Soundpipe (PaulBatchelor) provides every module Synth One uses. AudioKit
maintains a private fork that differs in one relevant place: its `sp_port`
names the smoothing half-time `htime` and takes it as an `sp_port_init()`
argument, where stock calls it `smooth` and defaults it.

Rather than patch third-party source, `compat/SoundpipeCompat.h` declares an
equivalent module (`s1_port`, in `src/s1_port.c` — the same one-pole filter,
transcribed from stock `modules/port.c`) and macro-redirects the `sp_port_*`
spellings onto it. Soundpipe itself is compiled separately and keeps its own
`sp_port` intact.

### Reimplemented, not ported

**`src/oscmorph2d.c`** is the one piece of DSP here that is not upstream code.
The repository's `DSP/Kernel/oscmorph2d.c` is an earlier one-dimensional
revision whose signature no longer matches its caller — `S1NoteState` passes a
band count, a band-frequency table, and sets `->enableBandlimit` /
`->bandlimitIndexOverride`, none of which that file has. The matching version
lives in AudioKit's Soundpipe fork, which is not public, so it was
reconstructed from the call sites and the shipped wavetable bank. The
interpolation math is carried over verbatim; only band selection is new.

Verified against the shipped tables: table index is `band * 4 + waveform`
(triangle, square, pwm, sawtooth), band 0 is the naive full-bandwidth waveform,
and bands 1–12 are bandlimited to fundamentals of 8.18 Hz … 22050 Hz such that
`f_band × harmonics = 22050`. Selection picks the lowest band whose limit is at
or above the oscillator frequency. Measured on a 2093 Hz sawtooth, enabling
bandlimiting drops inharmonic (aliased) energy from a 0.029 ratio to ~1e-7.

### Changes to the shared sources

Four upstream files carry `#ifdef __OBJC__` guards. Every guard keeps the
original code verbatim on the Apple path, so the iOS build is unaffected:

- `DSP/Kernel/S1DSPKernel.hpp` — `@class` forward declarations; `heldNoteNumbers`
  becomes `std::vector<NoteNumber>` and `heldNoteNumbersAE` is held by value
- `DSP/Kernel/S1DSPKernel.mm` — the `NSMutableArray`/`AEArray` construction
- `DSP/Sequencer/S1Sequencer.hpp` — `@class`, plus an `S1HeldNotesRef` typedef
  that is `AEArray *` on Apple and `AEArray &` on Linux
- `DSP/Sequencer/S1Sequencer.mm` — the matching `process()` signature

Three files could not be guarded sensibly because their bodies are Objective-C
throughout; Linux equivalents are compiled instead, transcribed line by line:

| upstream | Linux replacement |
| --- | --- |
| `S1DSPKernel+startStopNotes.mm` | `src/S1DSPKernel+startStopNotes.cpp` |
| `S1DSPKernel+reset.mm` | `src/S1DSPKernel+reset.cpp` |
| `S1DSPKernel+didChanges.mm` | `src/S1DSPKernel+didChanges.cpp` |

Everything else under `DSP/` — including the whole render loop in
`S1DSPKernel+process.mm`, all of `S1NoteState.mm`, and `S1Sequencer.mm` — is
compiled as-is.

### Host layer

`src/Engine.{h,cpp}` replaces `AKSynthOne.swift` + `Conductor.swift`: it owns
the kernel, uploads the 52-table wavetable bank, loads the factory preset banks
from `Presets/Data/*.json`, and exposes parameters by preset key. The
preset→parameter mapping is transcribed from `PresetDataManager.swift`,
including the legacy VCO-era key names (`vco1Volume` → `morph1Volume`, …);
parameters absent from older presets fall back to the DSP default.

MIDI is queued on the ALSA thread and applied at the top of the render
callback, so note handling and `process()` stay on one thread. Note on/off does
not allocate — the held-note vector is reserved to 128 entries up front.

Threading follows upstream: the render thread never blocks or allocates, and
DSP→UI notifications go through the message ring, drained by the host.

## Known gaps

- **The GUI is a rebuild, not a port.** `synthone-gui` covers all six panels
  and every parameter, but it is drawn with Dear ImGui rather than the original
  PaintCode vector assets, so it does not look like the iOS app.
- **Tunings travel with the app, not with presets.** The 117-scale library
  loads and applies correctly, but a scale stored *inside* a preset
  (`tuningName` / `tuningMasterSet`) is still ignored on load.
- **MIDI in only.** No MIDI out, no MPE, no Ableton Link, no Audiobus/IAA.
- **The ADSR editor is display-only** -- the curve tracks the parameters, but
  editing is via the knobs beneath it rather than by dragging handles.
- **Sample rate** is fixed at engine start; changing the JACK rate while
  running is not handled.
- Presets that differ from the DSP default in mono/poly clear all voices on the
  first render after loading — upstream behaviour, invisible on iOS because the
  engine runs continuously. Load presets before playing, not between notes.

## Layout

```
linux/
  CMakeLists.txt        build; fetches and builds Soundpipe
  compat/               Apple/AudioKit/TAAE stand-ins
  src/                  Soundpipe additions, Obj-C file replacements,
                        JSON reader, engine facade
  host/                 JACK + PortAudio backends, ALSA MIDI, CLI
  gui/                  Dear ImGui front end
  kiosk/                systemd unit, launcher and config for the Pi kiosk
  tools/                synthone-offline, tunings extractor, screenshot capture
```

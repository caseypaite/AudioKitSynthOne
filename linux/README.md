# AudioKit Synth One — Linux and Windows port

A native build of the Synth One **synthesis engine** for Linux (x86_64 and
aarch64) and Windows (x86_64), plus a standalone host and a graphical front end.

| | audio out | MIDI in |
| --- | --- | --- |
| Linux | JACK and/or PortAudio | ALSA sequencer |
| Windows | PortAudio (WASAPI/DirectSound/MME/WDMKS) | WinMM |

The DSP is the real thing: the original `S1DSPKernel`, `S1NoteState`,
`S1Sequencer`, `S1Arpeggiator`, `S1Rate` and `S1DSPCompressor` sources are
compiled in place from `../AudioKitSynthOne/DSP`, against a compatibility layer
that stands in for AudioKit and CoreAudio. The same sources build for both
platforms, and produce bit-identical audio on each — the Windows binaries are
verified against the Linux ones by rendering the same preset and comparing the
WAVs byte for byte.

> The directory is still called `linux/` for the Linux port it started as.
> Windows shares all of it but the two files named below.

**The iOS UI is not ported.** UIKit, the storyboards, the PaintCode StyleKits,
and the Audiobus/OneSignal/AppCenter/Disk pods have no equivalent on either
platform — that is a rewrite, not a port. `synthone-gui` covers all six panels
and every parameter, but it is drawn with Dear ImGui and does not look like the
iOS app.

## Requirements

For a Windows build, skip to [Windows](#windows-x86_64) — it needs only the
mingw-w64 cross toolchain, not the libraries below.

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

That is the native Linux build; for Windows binaries run
[`./build-windows.sh`](#windows-x86_64) instead, which needs no libraries
beyond the cross toolchain.

Offline (no network / air-gapped): build Soundpipe yourself and point CMake at
it with `-DSOUNDPIPE_ROOT=/path/to/soundpipe` (a tree containing
`libsoundpipe.a` and `h/soundpipe.h`, built with `make NO_LIBSNDFILE=1`).

Three binaries are produced:

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
so `--rate`/`--buffer`/`--latency` apply to PortAudio only.

`--list-devices` prints the outputs the chosen backend can open, and `--device`
picks one -- by index, or by any unambiguous part of its name:

```bash
./build/synthone --backend portaudio --list-devices
    0  HD-Audio Generic: HDMI 0 (hw:0,3)   ALSA   44100 Hz  8ch
    1  pipewire                            ALSA   44100 Hz  128ch
    2  default                             ALSA   44100 Hz  128ch  [default]

./build/synthone --backend portaudio --device 0        # by index
./build/synthone --backend portaudio --device HDMI     # by name
```

Without `--device` the backend chooses, which is the sound server's default
output. Naming a hardware device (`hw:1,0`) instead opens the card directly and
bypasses PipeWire/PulseAudio altogether -- useful when the server's routing is
the thing you are trying to rule out. Note the indices are not stable: they
shift as devices appear and disappear, so prefer a name in anything you script.
JACK has nothing to select, since the server owns the hardware and where the
ports go is a patchbay question; it reports a single entry.

On PortAudio the output latency follows `--buffer` -- one period, which is the
floor a callback API can offer:

| `--buffer` | output latency |
| --- | --- |
| 64 | 1.45 ms |
| 128 | 2.90 ms |
| 256 | 5.80 ms |
| 512 | 11.61 ms |

(measured at 44.1 kHz; the status line reports the figure the device actually
granted). `--latency MS` asks for more slack if a busy machine underruns. To
that, add up to one buffer of MIDI quantisation: events are applied at the top
of the render callback, so a note waits for the next period boundary. The
engine itself adds nothing -- a note-on is audible in the first sample of the
block it lands in. MIDI arrives on an ALSA
sequencer port (`--midi CLIENT:PORT`, or `all`, the default).

The same commands work on Windows, with two differences: there is no JACK, so
`--backend portaudio` is the only choice and `--host-api` picks the driver
family beneath it, and `--midi` takes a device index rather than `CLIENT:PORT`.
Both are covered under [Windows](#windows-x86_64). The latency table above was
measured on Linux and does not transfer -- MME, DirectSound and WASAPI each
report their own figure, and none of them was verified by round-trip
measurement.

Rendering without hardware:

```sh
./build/synthone-offline --bank BankA --preset 0 --note 60 --seconds 4 out.wav
./build/synthone-offline --note 69 --set arpIsOn=0 --set reverbMix=0 out.wav
```

## Install

Linux only -- a Windows build is a folder you unzip and run, with no install
step at all. See [Windows](#windows-x86_64).

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

The archive carries the binaries, their data, both scripts and the `kiosk/`
templates, so `sudo ./install.sh --kiosk` works straight out of it; `install.sh`
detects the packaged layout, so the recipient does not need the source. It is
not self-contained though -- alsa-lib, glfw, libGL, jack/pipewire-jack,
portaudio and libX11 must be present on the target.

`package.sh` fails rather than shipping an incomplete archive: it checks the
staged tree against everything `install.sh` reads out of it before zipping. Add
a file that `install.sh` needs and add it to that list too.

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

### Windows (x86_64)

Windows binaries are **cross-compiled from Linux** with MinGW-w64. There is no
MSVC project and nothing in the build has to run on Windows.

```sh
sudo apt install mingw-w64        # Debian/Ubuntu
sudo pacman -S mingw-w64-gcc      # Arch

./build-windows.sh                # -> build-windows/
./build-windows.sh --package      # -> dist/synthone-windows-x86_64.zip
```

If the toolchain is not on `PATH` — an unpacked one, or an install without root
— point at it with `--mingw DIR` (or `$MINGW_PREFIX`). Underneath, that is just:

```sh
cmake -S . -B build-windows -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-x86_64-w64-mingw32.cmake
```

GCC compiles these sources as readily as Clang; it only spells the warning
switches differently, which `CMakeLists.txt` handles. Soundpipe, PortAudio,
GLFW and Dear ImGui are all cross-built into the binary dir, and libstdc++ and
libwinpthread are linked statically, so the binaries **import nothing but
Windows' own DLLs** — there is no redistributable to ship alongside them.

The archive is a folder you unzip and run. Resources sit next to the
executables rather than behind wrapper scripts, because
`Engine::defaultResourceDir()` looks beside the `.exe` first:

```
synthone-windows-x86_64/
  synthone-gui.exe  synthone.exe  synthone-offline.exe
  resources/DSP/BandlimitedWavetables/   resources/Presets/Data/
  data/tunings.json
```

**Audio.** PortAudio wraps four Windows driver families and
`Pa_GetDefaultOutputDevice()` answers MME, the 1991 API: emulated on top of the
modern audio engine, the least dependable timing of the four, and the latency
it reports describes PortAudio's own buffer chain rather than what the driver
stack adds. The port prefers **WASAPI**, then DirectSound, then whatever the
default is. Override per machine with `--host-api wasapi|directsound|mme|wdmks`.

Treat that as a choice of API, not a measured latency win. On the machine this
was written on WASAPI *reports* 22 ms against MME's 5.8 ms and DirectSound's
6.8 ms, and no round-trip measurement was made to settle which is genuinely
lower — which is exactly why `--host-api` exists. WASAPI **exclusive** mode,
the thing that would actually cut latency, is not wired up. ASIO is absent
too: it needs Steinberg's licensed SDK, which cannot be redistributed.

**MIDI.** Input goes through WinMM, which has no notion of publishing a port
other applications can connect to — an application only opens devices that
already exist. So `--midi` takes a device *index* rather than ALSA's
`CLIENT:PORT` (`--list-midi` numbers them; `all`, the default, opens every one).
`N:0` is accepted too, so a command line copied from the Linux build still
works.

**What differs in the source.** Two files, plus one branch:

| | |
| --- | --- |
| `host/WinMidi.cpp` | WinMM MIDI input, replacing `host/AlsaMidi.cpp`. Both implement `MidiInput` from `host/MidiInput.h`; WinMM calls back on its own thread, so it has no reader thread of its own and pushes straight onto the queue the audio thread drains. |
| `src/PlatformPaths.cpp` | `executableDir()`, the one thing that needs the Win32 API. It is a separate translation unit **on purpose**: `<windows.h>` defines `typedef int BOOL` and the compat layer defines Apple's `typedef signed char BOOL`, which is a hard conflict in any file that includes both — and `<windows.h>` also defines `min`/`max` as macros, which would break `std::min` in the DSP sources. Keeping the Win32 surface in one file lets `Engine.cpp` stay plain C++. |

Everything else is shared. `Engine::defaultUserDataDir()` gains a Windows
branch (`%APPDATA%\SynthOne\presets`) and the CLI help changes wording for
`--midi`; the DSP is untouched.

**Not carried over:** `install.sh`, `uninstall.sh`, `package.sh` and the kiosk
are Linux-only — `build-windows.sh` does the Windows build and packaging
instead. JACK is not built for Windows; it exists there but is a niche install,
and PortAudio already reaches the same drivers.

`synthone-gui.exe` keeps a console attached, so backend and GLFW failures are
visible rather than turning into a window that silently does nothing. Configure
with `-DS1_WIN32_GUI_CONSOLE=OFF` for a console-free build.

### Raspberry Pi kiosk (boot straight into the synth)

`install.sh --kiosk` adds a systemd service that starts a bare X server on tty1
with `synthone-gui` as its only client. No display manager, no desktop, no
window manager -- the synth owns the panel, and the Pi's CPU goes to audio
rather than to a desktop session.

```sh
sudo ./install.sh --prefix /usr/local --kiosk --kiosk-user pi
journalctl -u synthone-kiosk -f            # watch it come up
```

It needs `xserver-xorg` and `xinit` (`apt install xserver-xorg xinit`) -- an X
server, but no desktop on top of it.

There is no login prompt and no autologin getty to configure: the unit runs as
the configured user under `PAMName=login`, so `logind` opens a real session for
them -- `XDG_RUNTIME_DIR`, user services and all -- with no password and no
shell in between. The service is enabled, so that happens on every boot.

**`--kiosk` takes the machine over.** As well as installing the unit, it:

- **starts the kiosk there and then**, which takes tty1 immediately. Run it
  over SSH: on the Pi's own console it replaces the shell you typed it into.
  If `xinit` is missing it is enabled but not started, and says so, rather than
  entering a two-second restart loop;
- **disables the display manager** (`lightdm` and friends) and **switches the
  default target to `multi-user.target`**, so nothing else claims the GPU or
  the seat at boot. The unit also carries `Conflicts=display-manager.service`,
  so starting the kiosk stops a display manager that gets re-enabled later;
- takes tty1 from `getty` (`Conflicts=getty@tty1.service`);
- adds the kiosk user to `audio`, `video`, `input` and `tty`;
- writes `/etc/X11/Xwrapper.config` with `allowed_users=anybody` if that file
  does not already exist, since a service is not a console login. If it does
  exist, the script tells you what to add rather than editing it.

The display manager and default target as they were before the first `--kiosk`
run are recorded in `/etc/synthone/kiosk.state`; `./uninstall.sh` reads it back,
re-enables the display manager, restores the target, returns tty1 to `getty`
and removes the file. Re-running `--kiosk` never rewrites that record, so the
original values survive a reinstall.

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
./tools/screenshots.sh --desktop            # -> desktop-*.png + desktop.gif
./tools/screenshots.sh --geometry 1024x600  # a different panel
```

The two modes match the two layouts: the default shoots one panel per frame at
800x480, `--desktop` shoots stacked pairs at 1440x900, so three frames cover all
six panels. The stills are gitignored; the two GIFs are the tracked artefacts.

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
beside a stepper. Each block keeps its own heading and border, so the grouping
by function is more explicit than headings stacked down the page.

Because the flow keys off available width, the same code lays a panel out at
either resolution -- FX is four shelves at 800x480 and two at 1440x900, MAIN
three and two. `BlockFlow` also takes an optional width, which turns it into a
column; SEQ uses that for its desktop arrangement.

Three rules worth knowing before adding a control:

- **Blocks never scroll.** A scrollbar inside one would steal width from the
  contents and truncate the captions, so they are created with
  `NoScrollbar`. A block whose declared size is too small silently clips
  instead -- if something disappears, the size passed to `flow.begin()` is
  wrong, not the layout.
- **Declare sizes with the helpers,** at the size you are drawing:
  `kw(label, diameter)` and `kwRow({...}, diameter)` mirror `Knob()`'s own
  width formula, which widens a cell when its caption is wider than the face.
- **A widget's own caption counts.** `Selector` and `Stepper` each emit a
  caption line above their row even when it is empty, so a block holding one
  needs `stepperBlockH()`, not `buttonBlockH(1)` -- half the height clips the
  buttons away entirely.

### Knob sizes per panel

Every panel names one face size and measures every width at it. Compact then
scales that to 70% and floors it, and each panel raises its own floor -- so the
number that applies depends on the display:

| Panel | Desktop | Compact | Notes |
| --- | --- | --- | --- |
| MAIN | 80px | 72px | Desktop ceiling: 80 keeps it at two shelves, more wraps a third. |
| FX | 64px | 32px | Both are ceilings. See below. |
| ENV | 60px | 72px | The curve editor takes the rest of the height. |
| SEQ | 60px | 52px | Cells are sized by their captions (`INTERVAL`, `TEMPO x`), so compact can grow to 52 before the block widens and the arp row wraps. |
| TUNE | 60px | 64px | The pitch wheel takes the rest of the height. |
| PAD | -- | -- | No knobs; the XY pads take the whole pane. |

Compact is the tighter of the two. FX is the panel the compact scaling exists
for: thirty-odd controls in four shelves, clearing the fold by ~10px, and
measured, even a 36px face costs 12px across its three knob shelves and brings
the scrollbar back.

**Measure at the size you draw.** `kw(label, diameter)`, `kwRow({...},
diameter)`, `knobBlockH(rows, diameter)` and `blockRowH(rows, diameter)` all
take it for this reason. Measuring a spread at the default 46px while drawing
at 60px overflows the block by ~50px, and blocks clip rather than scroll, so
the symptom is a control that has silently vanished.

Raising a size is a matter of trying it and looking: a bigger face widens the
block as well as heightening it, so it can reflow a shelf and cost more than it
gains. `tools/screenshots.sh --desktop` is the quickest way to check.

### What fills a pane, and what does not

ENV, SEQ and TUNE each have one elastic thing -- a curve editor, the step grid,
the pitch wheel -- sized from `GetContentRegionAvail()`, so they absorb whatever
the pane has spare and fill on their own at either resolution.

MAIN and FX are all fixed-content blocks, so they fill only as far as their
declared sizes reach. That is why their face sizes are tuned to the pane rather
than left at a default.

It also means a block's declared width matters: FX's routing block asked for
640px to hold content measuring ~604, and those 36 wasted pixels were the
difference between the LFO row fitting beside it and the matrix wrapping onto a
shelf of its own.

### SEQ

The sequencer is arranged differently on each display, because the step grid is
sixteen sliders and thirty-two buttons and the arp controls are six small
blocks:

- **Desktop** -- controls stacked in a column on the left, grid taking the rest
  of the pane on the right. `BlockFlow` takes an optional width so the column
  is built from the same primitive as everything else.
- **Compact** -- controls on one shelf, grid on the one below, since sixteen
  steps need most of an 800px panel on their own.

Each step's transpose is a **vertical slider**, which is what the iOS panel
uses: `SequencerPanelController` binds a `VerticalSlider` to each
`sequencerPatternNN` over the parameter's own -12..+12 range, with the
octave-boost and note-on buttons underneath. `SequencerGrid` takes the grid size
and lays its columns out to fit, so the same code serves both arrangements.

### Where presets live

Factory banks ship read-only in `AudioKitSynthOne/Presets/Data/` and are never
written to. Presets you save go to

```
Linux    $XDG_DATA_HOME/synthone/presets   (default: ~/.local/share/synthone/presets)
Windows  %APPDATA%\SynthOne\presets
```

overridable with `--user-dir DIR` on any of the three binaries. (Roaming
`%APPDATA%`, not `%LOCALAPPDATA%`: a preset bank is a small user-authored
document, the kind of thing that should follow the user between machines.) A user bank
shadows a factory bank of the same name, and saving into a factory bank name
copies that whole bank into your directory first, leaving the shipped file
untouched -- so the source tree stays clean.

### Choosing an output device

The **AUDIO** button in the header opens a dialog listing every output the
current backend can open, grouped by driver family, alongside sample rate and
buffer size. It shows what the running stream actually settled on -- device,
rate, buffer and the latency the driver granted -- which is usually the fastest
way to find out that audio is going somewhere you are not listening to.

Applying reopens the stream between frames, never inside one, since the render
callback lives on the thread being torn down. Held notes are cut. Changing the
sample rate additionally rebuilds the DSP kernel, because the wavetable
increments, envelope rates and LFO phases are all derived from it; the preset
and tuning are put back afterwards. Every failure path ends with the synth
audible on the device it had before -- a mistyped device is a smaller problem
than a synth that has gone quiet.

The choice is remembered in the data directory that *holds* the preset
directory, never inside it:

```
Linux    $XDG_DATA_HOME/synthone/audio.json   (default: ~/.local/share/synthone/audio.json)
Windows  %APPDATA%\SynthOne\audio.json
```

Two reasons it sits outside. Banks are saved as `<bank>.json` in the preset
directory, so a bank innocently named "audio" would collide with this file and
one would silently overwrite the other. And `--user-dir` moves where *presets*
live, which is a different question from which speaker this machine uses -- so
this path deliberately ignores that flag. Which speaker you use belongs to the
machine and must not travel with a preset collection onto another box.

Both the index and the device name are stored, and the name has to still match
at that index or the setting falls back to automatic -- device indices shift as
hardware and sound servers come and go, and opening whatever now sits at index 3
is worse than not trying.

The same selection is available headlessly through `--list-devices` and
`--device` on both hosts (`synthone-offline` writes a file and opens no device
at all); the GUI additionally accepts `--rate` and `--buffer`. Flags override
the saved file for that run without overwriting it, so a one-off `--device`
does not become permanent.

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

MIDI is queued on the ALSA thread — or, on Windows, on the thread WinMM calls
back from — and applied at the top of the render callback, so note handling and
`process()` stay on one thread. Note on/off does not allocate — the held-note
vector is reserved to 128 entries up front.

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
- **On Windows, WASAPI exclusive mode and ASIO are not available** — the two
  routes to genuinely low latency. Shared-mode WASAPI is what you get; ASIO
  needs a licensed SDK that cannot be shipped. Actual round-trip latency has
  not been measured on any Windows machine.
- Presets that differ from the DSP default in mono/poly clear all voices on the
  first render after loading — upstream behaviour, invisible on iOS because the
  engine runs continuously. Load presets before playing, not between notes.

## Layout

```
linux/
  CMakeLists.txt        build; fetches and builds Soundpipe (+ PortAudio and
                        GLFW when cross-compiling for Windows)
  cmake/                the MinGW-w64 cross toolchain file
  build-windows.sh      Windows cross build and packaging
  compat/               Apple/AudioKit/TAAE stand-ins
  src/                  Soundpipe additions, Obj-C file replacements,
                        JSON reader, engine facade, Win32 path lookup
  host/                 JACK + PortAudio backends, ALSA and WinMM MIDI, CLI
  gui/                  Dear ImGui front end
  kiosk/                systemd unit, launcher and config for the Pi kiosk
  tools/                synthone-offline, tunings extractor, screenshot capture
```

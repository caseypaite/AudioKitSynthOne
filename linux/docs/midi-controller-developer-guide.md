# MIDI controller drivers -- developer guide

This covers the architecture behind `--controller-driver` and MIDI SysEx
support: how bytes get from the wire to a driver, how a driver is matched
and loaded, how it can seed default parameter mappings without stepping on
the user's own MIDI Learn, and what it takes to add a driver for a new
controller. If you just want to *use* a supported controller, see the
[user guide](midi-controller-user-guide.md) instead.

Read this alongside [linux/README.md](../README.md) -- in particular the
render-thread discipline described there ("no allocation, no blocking, no
locks" on the audio render thread) applies here too, and is called out
again below at the one place it actually matters for this subsystem.

## Why this exists, and what it deliberately doesn't do

Zynthian's [`zyngine/ctrldev`](https://github.com/zynthian/zynthian-ui/tree/vangelis/zyngine/ctrldev)
drivers are the model, but Zynthian is a multi-chain rack with a mixer,
a pad-grid launcher, and a pattern sequencer -- most of what a Zynthian
driver's knobs and pads *do* has no equivalent in Synth One, which is a
single preset-based synth. So this port's driver framework is narrower on
purpose:

- A driver identifies a device by name and, optionally, talks SysEx to it.
- A driver can seed default CC-to-parameter mappings for its knobs.
- **Pads and keys need no driver code at all.** They already flow through
  as ordinary MIDI notes; a driver never intercepts them.
- There is no mixer/chain/launcher concept to control, so there's nothing
  resembling Zynthian's `zynthian_ctrldev_zynpad`/`zynmixer` base classes
  here -- one flat `ControllerDriver` interface covers everything this port
  needs.

## Data flow, end to end

```
ALSA seq / WinMM  --(raw bytes)-->  MidiInput reader thread
                                         |
                    +--------------------+--------------------+
                    |                    |                    |
             3-byte channel       SysEx (0xF0..0xF7)    Program Change
             messages ->      -> SysExAssembler      -> ProgramChangeQueue
             MidiQueue            -> SysExQueue                |
                    |                    |                    |
             audio render         control-thread poll loop (both queues)
             callback             (`while (gRunning)` /
             (host/main.cpp        `while (!glfwWindowShouldClose)`)
             `render` lambda;                |
             JACK/PortAudio    ControllerDriverManager::dispatchSysEx
             callback thread)  / dispatchProgramChange
                    |                    |
             Engine::handleMidi    driver->onSysEx(...) /
             (mCcToParameter,      driver->onProgramChange(...)
              then                          |
              mDeviceDefaultCc     Engine::setDeviceDefaultCc(...) /
              fallback)            clearDeviceDefaults()
```

Two threads matter here, and they're isolated from each other on purpose:

- **The MIDI reader thread** (ALSA: its own `std::thread`; WinMM: whatever
  thread the OS invokes the callback on) does the byte-level parsing and
  reassembly. It's *not* the audio render thread, so unlike
  `S1DSPKernel+process.mm`, it's fine for this code to allocate
  (`std::vector`, `new`) -- and `SysExAssembler`/the WinMM buffer pool do.
- **The audio render callback** only ever touches the small, fixed-size,
  lock-free `MidiQueue`/`mCcToParameter`/`mDeviceDefaultCc` structures via
  `Engine::handleMidi()`. SysEx/Program Change dispatch, driver matching, and
  everything a driver itself does (including calling
  `Engine::setDeviceDefaultCc`) all happen on the *control* thread instead --
  the same ~50 ms poll loop that already calls `engine.drainNotifications()`.
  **A driver must never be called from the render thread**, and nothing in
  this framework does that.

## SysEx transport (`host/MidiSysEx.h`)

Three pieces, all header-only and platform-independent:

- **`SysExMessage`** -- a reassembled message, `0xF0`..`0xF7` inclusive, in
  a fixed 512-byte buffer (`SysExMessage::kMaxLength`). Comfortably covers
  the MPK Mini mk3's 254-byte program dump; a longer message is dropped,
  not truncated-and-kept, since this path exists for startup handshakes,
  not general SysEx passthrough. Also carries a `sourceId` -- the ALSA
  client id or WinMM device index the message arrived on -- so
  `ControllerDriverManager` can route a reply to the driver that's waiting
  on it.
- **`SysExQueue`** -- a lock-free SPSC ring of `SysExMessage`, capacity 16.
  Deliberately a *separate* ring from `MidiQueue` (the note/CC path):
  reusing that 1024-slot ring would mean either bloating every slot to fit
  a rare, variable-length payload, or storing heap pointers in a lock-free
  structure with the lifetime hazards that brings. Two small, simple rings
  beat one complicated one.
- **`SysExAssembler`** -- accumulates raw bytes into a complete message.
  Terminates on `0xF7`; a `0xF0` always restarts accumulation (defends
  against a dropped terminator leaving stale bytes around); MIDI System
  Realtime bytes (`0xF8`-`0xFF`, except `0xF7` itself) that can legally
  appear interleaved inside a live SysEx stream are skipped rather than
  treated as payload. One instance per reader thread -- not thread-safe by
  itself, matching how `MidiInput` already owns one queue per platform.
  Exercised directly (no hardware needed) by
  `tools/sysex_assembler_test.cpp` -- run it after touching this file.

### ALSA (`host/AlsaMidi.cpp`, `AlsaMidiOut.cpp`)

- RX: `MidiInput::run()`'s event switch has a `case SND_SEQ_EVENT_SYSEX:`
  that feeds `event->data.ext.ptr`/`.len` through `mSysExAssembler.append()`
  *before* `snd_seq_free_event(event)` runs a few lines later -- `append()`
  copies synchronously, so the pointer's validity window is respected.
  ALSA sequencer events can in principle split one logical SysEx message
  across several `SND_SEQ_EVENT_SYSEX` events; the assembler handles that
  the same way it handles any other fragmentation.
- TX: `MidiOutput::sendSysEx()` builds an `snd_seq_event_t`, calls
  `snd_seq_ev_set_sysex(&ev, length, data)`, and sends it with
  `snd_seq_event_output_direct()` -- the same `_direct`, unscheduled send
  path `noteOn`/`noteOff` already use.
- `MidiInput::connectedSources()` is new: it returns whatever
  `connect()` actually subscribed to (the `all` branch could subscribe to
  several sources), which is what `ControllerDriverManager::load()` matches
  driver name-hints against.

### WinMM (`host/WinMidi.cpp`, `WinMidiOut.cpp`)

This side is **implemented from documented WinMM behaviour and has not been
run against a real Windows install** -- this port cross-compiles from Linux
and this codebase's dev environment has no Windows runtime. Treat it as
higher-risk than the ALSA side until someone validates it on real hardware.

- RX: a small pool of `MIDIHDR` buffers (`kSysExBufferCount = 4`,
  `kSysExBufferSize = 1024`) is prepared and posted per opened `HMIDIIN` via
  `midiInPrepareHeader`/`midiInAddBuffer`, before `midiInStart`. The
  callback (`midiCallback`) now handles `MIM_LONGDATA` in addition to the
  pre-existing `MIM_DATA` -- previously SysEx was structurally impossible
  to receive at all (no buffers were ever posted for it, and the callback
  explicitly rejected anything but `MIM_DATA`). `MidiInput::enqueueSysExFragment()`
  feeds the buffer through the same `SysExAssembler` and re-posts it via
  `midiInAddBuffer` for the next chunk -- documented as the one call that's
  safe to make from inside the `MIM_LONGDATA` callback itself. Teardown
  (`~MidiInput()`) calls `midiInReset` (which returns every outstanding
  buffer through the callback, seen as `mRunning == false` there so nothing
  gets re-posted) then `midiInUnprepareHeader` on each one before freeing
  it -- skipping the unprepare step would be undefined behaviour.
- TX: `MidiOutput::sendSysEx()` uses `midiOutPrepareHeader`/`midiOutLongMsg`,
  then polls `midiOutUnprepareHeader` until it stops returning
  `MIDIERR_STILLPLAYING` (bounded to 2 seconds). This output is opened with
  `CALLBACK_NULL`, so there's no `MOM_DONE` notification to wait on instead
  -- the poll is the standard callback-free idiom for a synchronous SysEx
  send on WinMM.

## Program Change transport (`host/MidiSysEx.h`)

A driver's other input: `ProgramChangeMessage`/`ProgramChangeQueue`, added for
mode switching (see below) and structurally identical to
`SysExMessage`/`SysExQueue` -- a small SPSC ring (capacity 8) carrying
`{channel, program, sourceId}` from the reader thread to the control thread.
Kept separate from `MidiQueue` for the same reason SysEx is: driver dispatch
happens on the control thread, not the render thread, so this message kind
needs its own path off the hot note/CC ring regardless of how simple (2
bytes) it is.

Program Change was already reaching the existing `MidiQueue` on Windows (it's
just another packed short message `enqueuePacked()` forwards, with
`length == 2`) but was silently dropped on Linux (no `SND_SEQ_EVENT_PGMCHANGE`
case existed in `AlsaMidi.cpp`'s `run()`). Both platforms now *additionally*
push a copy onto `ProgramChangeQueue` when a controller driver wants it --
the existing `MidiQueue` path (Windows-only, and `Engine::handleMidi()`
falling through to `mKernel->handleMIDIEvent()` for it) is unchanged.

- ALSA: `run()`'s switch gained `case SND_SEQ_EVENT_PGMCHANGE:`, reading
  `event->data.control.channel`/`.value`.
- WinMM: `enqueuePacked()` additionally checks `kind == 0xC0` and, if so,
  resolves `sourceId` by scanning `mHandles` for the `HMIDIIN` the callback
  fired on (same lookup pattern `enqueueSysExFragment()` already uses for the
  SysEx path) before pushing.

## The driver interface (`host/ctrldev/ControllerDriver.h`)

```cpp
class ControllerDriver {
public:
    virtual std::vector<std::string> deviceNameHints() const = 0;
    virtual const char *driverName() const = 0;
    virtual void init(Engine &engine, MidiOutput *midiOut, bool allowConfigure) = 0;
    virtual void onSysEx(const uint8_t *data, size_t length) {}
    virtual void onProgramChange(int program) {}
};
```

- **`deviceNameHints()`** -- case-insensitive substrings matched against
  `MidiSource::name`. A device is claimed if its name contains *any* one of
  them. Keep this list generous: the same physical device can report subtly
  different names across ALSA/WinMM/firmware revisions.
- **`init(engine, midiOut, allowConfigure)`** -- called once, after the
  manager has matched a device and tried to pair a same-device output.
  `midiOut` is `nullptr` when no output was found; `init()` must still work
  as a plain input in that case (skip anything needing SysEx TX). `midiOut`
  is a private `MidiOutput` the manager owns and connected itself --
  independent of any `--midi-out` the user also configured, so the two
  never fight over one `MidiOutput`'s connection state. `allowConfigure`
  reflects `--controller-driver-configure` (default off): permission to
  *write* to the device, not just read from it -- gate any such write
  behind this rather than firing it unconditionally.
- **`onSysEx(data, length)`** -- a complete, reassembled message addressed
  to this driver's device. Default implementation ignores it.
- **`onProgramChange(program)`** -- a Program Change addressed to this
  driver's device arrived, `program` in `[0,127]`. Default implementation
  ignores it. The hook for mode/bank switching -- see below.

## The manager (`host/ctrldev/ControllerDriverManager`)

A small static registration table, deliberately not a plugin system:

```cpp
// ControllerDriverManager.cpp
const Entry kDrivers[] = {
    {"akai-mpk-mini-mk3", makeAkaiMpkMiniMk3},
};
```

`load(wantedName, engine, connectedInputs, allowConfigure, status)`:

1. `wantedName == "off"` -> does nothing, returns `false`.
2. For each registered driver where `wantedName == "auto"` or matches its
   name exactly: instantiate it, lower-case its `deviceNameHints()`, and
   check them (substring match) against every connected input's
   lower-cased name.
3. On a match, find a same-device *output* port:
   - Prefer the same ALSA client id as the matched input (a USB MIDI
     interface's in/out ports typically share one client).
   - Fall back to an identical port *name* (the only signal on WinMM, where
     input and output devices are independently numbered).
4. Open a private `MidiOutput` for TX if an output was found; call
   `driver->init(engine, outPtrOrNull, allowConfigure)`.
5. Fill `status` with something printable either way (what loaded and from
   where, or why nothing did) -- `host/main.cpp` prints this on a `[ctrl]`
   status line; `gui/main.cpp` prints it once at startup if something
   loaded.

`dispatchSysEx(msg)`/`dispatchProgramChange(msg)` route a message to the
loaded driver whose `inputSourceId` matches `msg.sourceId`; if nothing
matches (unset source id, or a startup race), it's offered to every loaded
driver rather than dropped.

`Loaded` (the manager's internal per-driver record) stores its `MidiOutput`
by value inside a `std::vector<std::unique_ptr<Loaded>>`, not
`std::vector<Loaded>` -- `MidiOutput` owns a raw ALSA seq handle or WinMM
handles with no defined copy/move semantics, so a plain vector reallocating
as more drivers load would silently double-close a handle. The indirection
sidesteps that regardless of how many drivers end up registered.

**No hotplug.** Matching happens once, against whatever `MidiInput` already
connected at startup. This mirrors an existing limitation of MIDI port
handling generally in this port (see `linux/README.md`) -- not something
new introduced here, and not addressed by this framework either.

## `Engine`'s device-scoped CC fallback

A driver's knob defaults live in a table that's deliberately *separate*
from the existing MIDI-Learn table (`Engine::mCcToParameter`), so a
driver's default is never indistinguishable from something the user
actually bound, and never gets wiped by "Clear All MIDI Learn":

```cpp
// Engine.h
S1Parameter deviceDefaultForCc(int cc) const;
void setDeviceDefaultCc(int cc, S1Parameter parameter);
void clearDeviceDefaults();
```

Backed by `std::atomic<int> mDeviceDefaultCc[128]` (same shape as
`mCcToParameter`, initialised to `-1`). `Engine::handleMidi()`
(`Engine.cpp`, the CC branch under `status == 0xB0`) checks this table
*after* the user's own `mCcToParameter`, and only if that CC has nothing
bound:

```cpp
const int mapped = mCcToParameter[cc].load(std::memory_order_acquire);
if (mapped >= 0 && mapped < S1Parameter::S1ParameterCount) {
    /* ... existing user-learned-CC handling, unchanged ... */
    return;
}
const int deviceDefault = mDeviceDefaultCc[cc].load(std::memory_order_acquire);
if (deviceDefault >= 0 && deviceDefault < S1Parameter::S1ParameterCount) {
    /* ... same shape as above, using mDeviceDefaultCc instead ... */
    return;
}
```

This is the one place in the whole feature that runs on the **audio render
thread** (`Engine::handleMidi()` is called from the `render` lambda in
`host/main.cpp`/`gui/main.cpp`, which runs on the JACK/PortAudio callback
thread). That's why it's one more atomic load and branch, not a
`std::function` hook or anything that could allocate or block --
`setDeviceDefaultCc()`/`clearDeviceDefaults()` themselves are only ever
called from driver code on the control thread, but the *read* side has to
satisfy the same "no allocation, no blocking, no locks" discipline as
`S1DSPKernel+process.mm` does, because it's reached from the same callback.

## Modes: reaching more than 8 knobs' worth of parameters

A physical controller has a fixed number of knobs; Synth One has far more
parameters than that. `ControllerDriver::onProgramChange()` is the general,
framework-level answer: a driver that wants "modes" or "banks" reacts to an
incoming Program Change by clearing and re-seeding
`Engine`'s existing device-default CC table for a *different* set of target
parameters. No new Engine state is needed for this -- `mDeviceDefaultCc` is
already just a flat 128-entry table with no concept of "mode" baked in; a
driver own the concept entirely by re-populating that same table each time
the mode changes:

```cpp
void YourDriver::onProgramChange(int program) {
    engine.clearDeviceDefaults();      // old mode's mapping no longer applies
    if (program not in a mode you've designed) return;  // leave unbound, don't guess
    // ... determine the new mode's 8 target parameters and seed them,
    // synchronously if you already know the CCs, or after a fresh SysEx
    // query if (like the MPK driver) you need to discover them per-mode.
}
```

This works for *any* trigger a driver can turn into a Program Change --
what actually produces the PC message is entirely device-specific and none
of the framework's concern. The Akai MPK Mini mk3 happens to have a
hardware-native one: its PAD CONTROLS row includes a **PROG SELECT** button
that switches between the device's onboard program slots (0 = RAM/current,
1-8 = stored) with no reprogramming required, and Zynthian's own driver
already relies on this producing a Program Change -- see
`AkaiMpkMiniMk3.cpp`'s file-header comment for the caveat that this specific
assumption (PROG SELECT reliably sends PC) is unverified against real
hardware here, same status as the rest of the protocol detail below.

The MPK driver's mode table (`kModeTargets[kModeCount][8]`) treats the
program number directly as the mode index, with 4 designed modes:

| Mode (program) | Focus | Targets |
| --- | --- | --- |
| 0 | Sound (default) | cutoff, resonance, attackDuration, releaseDuration, lfo1Rate, reverbMix, delayMix, masterVolume |
| 1 | Oscillators/voice | morph1Volume, morph2Volume, morph2Detuning, subVolume, fmAmount, noiseVolume, glide, morphBalance |
| 2 | Filter envelope depth | filterAttackDuration, filterDecayDuration, filterSustainLevel, filterReleaseDuration, filterADSRMix, cutoffLFO, resonanceLFO, adsrPitchTracking |
| 3 | Modulation/FX depth | lfo1Amplitude, lfo2Rate, lfo2Amplitude, phaserMix, phaserRate, autoPanAmount, autoPanFrequency, bitCrushSampleRate |

`onProgramChange(program)`:
1. Always `engine.clearDeviceDefaults()` first -- the previous mode's
   mapping must not silently persist into the new one if the following
   query fails or the slot is undesigned.
2. If `program` is outside `[0, kModeCount)`, stop there -- no target set
   exists for it, so the knobs stay unbound rather than reusing an
   unrelated mode's mapping or guessing at one.
3. Otherwise record `mCurrentMode = program` and re-run the same
   `sendQuery(program)` used at startup, for that specific slot -- the
   query command already takes an arbitrary program number (0-8), so this
   is a direct reuse of the already-confirmed protocol, not new surface
   area. The reply arrives later via `onSysEx()`/`parseKnobs()`, which reads
   `kModeTargets[mCurrentMode]` to know what to seed.

Only 4 of the device's 9 program slots have a designed mode; switching to
slot 4-8 clears the knob defaults and leaves them there, deliberately -- add
a row to `kModeTargets` (and bump `kModeCount`) to give a slot a purpose
rather than inventing a mapping speculatively.

## Adding a driver for a new controller

1. Create `host/ctrldev/YourDevice.h`/`.cpp`, modelled on
   `AkaiMpkMiniMk3.{h,cpp}`. Implement `deviceNameHints()`, `driverName()`,
   `init()`, and `onSysEx()` if you need it.
2. Register it in `ControllerDriverManager.cpp`'s `kDrivers` table.
3. Add the new `.cpp` to `synthone_host`'s sources in `CMakeLists.txt`
   (next to the existing `host/ctrldev/*.cpp` entries).
4. If your device has no SysEx worth speaking (many controllers don't),
   `init()` can be as simple as calling `engine.setDeviceDefaultCc()` with
   fixed CCs the device is documented to send -- no need to touch
   `onSysEx()` at all in that case.
5. Pads/keys need *no* driver code, ever -- they already work via the
   ordinary `MidiQueue -> Engine::handleMidi()` path regardless of which
   channel they're sent on. Only write code for what genuinely needs
   device-specific handling (SysEx identification, knob CC discovery).
6. **Don't guess at CC numbers you haven't confirmed.** CC 1 (mod wheel)
   and CC 64 (sustain) are universal MIDI conventions; seeding a device
   default onto one of those hijacks it for anyone who also uses that
   control physically. Prefer discovering real CCs via SysEx query where
   possible (see the MPK driver below), and when you can't, say so in the
   user guide rather than shipping a plausible-looking guess.
7. If the device reaches more parameters than its knobs can cover in one
   pass, implement `onProgramChange()` for mode/bank switching -- see
   "Modes" above. Optional; most controllers won't need it.

## Testing without hardware

- **`SysExAssembler`/`SysExQueue`/`ProgramChangeQueue`** are pure,
  allocation-free, and have no platform dependency -- exercised by
  `tools/sysex_assembler_test.cpp`
  (`cmake --build build --target sysex_assembler_test && ./build/sysex_assembler_test`).
  Covers whole-message delivery, byte-at-a-time fragmentation, multi-chunk
  fragmentation, a dropped terminator followed by a fresh message (resync
  on `0xF0`), interleaved Realtime bytes, overflow handling, and both
  queues' push/pop/capacity behaviour. Run this after touching
  `MidiSysEx.h` -- it's fast and needs nothing else built.
- **A new driver's parsing logic** (anything in `onSysEx()`) is testable
  the same way: hand-build a synthetic byte sequence matching your device's
  documented envelope and feed it through directly, no hardware needed for
  the *logic*, only for confirming the envelope itself is right.
- **ALSA SysEx RX/TX plumbing** is, in principle, testable end-to-end with
  `amidi`/`aconnect`/`aseqdump` against a virtual ALSA client, injecting raw
  bytes with `amidi -p <port> -S "F0 ..."` and confirming they reach
  `ControllerDriverManager::dispatchSysEx()`. This needs a working ALSA
  sequencer (`/dev/snd/seq`) -- not guaranteed to exist in every dev/CI
  environment (it doesn't in the container this feature was originally
  built in), so treat this as "do it if you have it," not an assumption.
- **Real hardware** is the only way to confirm a specific device's SysEx
  byte layout and factory defaults. Nothing above substitutes for it.

## Akai MPK Mini mk3 protocol reference

Implemented in `host/ctrldev/AkaiMpkMiniMk3.cpp`. Transcribed from
Zynthian's shipped driver
(`zyngine/ctrldev/zynthian_ctrldev_akai_mpk_mini_mk3.py` on the `vangelis`
branch of `zynthian/zynthian-ui`), itself sourced from
[tsmetana/mpk3-settings](https://github.com/tsmetana/mpk3-settings/blob/master/src/message.h)
-- a working reference implementation, not a guess, but **not independently
validated against physical hardware** in this project.

All offsets are relative to the byte *after* the leading `0xF0` (offset 0 =
manufacturer byte); the wire message is `F0 <these bytes> F7`.

Envelope (first 7 bytes, every command):

| offset | field | values |
| --- | --- | --- |
| 0 | manufacturer | `0x47` (Akai) |
| 1 | direction | `0x7F` host->device, `0x00` device->host |
| 2 | product | `0x49` (MPK Mini mk3) |
| 3 | command | `0x64` write, `0x66` query-request, `0x67` query-reply |
| 4, 5 | payload length | `(246>>7)&127, 246&127` for write/reply; unused for query-request |
| 6 | program slot | 0 (RAM/current) - 8 |

Query request is just the envelope, wrapped: `F0 47 7F 49 66 00 01 <program>
F7` (9 bytes). `AkaiMpkMiniMk3::sendQuery()` asks for slot 0 (RAM/current) at
`init()`, and whatever slot `onProgramChange()` was just told about after
that -- see "Modes" above.

Write/reply body continues for 246 more bytes (252 total; the full wire
message including `F0`/`F7` is 254 bytes -- this exact length, plus the
command byte `0x67`, is what `onSysEx()` checks before trusting a reply):

| offset | field |
| --- | --- |
| 7-22 | program name (16 chars) |
| 23-42 | pads channel, aftertouch, keybed channel, keybed octave, arp settings, tempo, joystick config |
| **43-90** | **16 pads x 3 bytes each: note, program-change, CC** (not parsed -- pads pass through as plain notes, see above) |
| **91-250** | **8 knobs x 20 bytes each: mode (0=absolute/1=relative), CC, min, max, name[16]** |
| 251 | transpose |

`parseKnobs()` reads the CC byte (offset `+1`) from each of the 8 knob
blocks at `91 + k*20` and calls `engine.setDeviceDefaultCc(cc, target)` for
the active mode's target row (`kModeTargets[mCurrentMode]` -- see "Modes"
above for the full table and how `mCurrentMode` gets set).

Before trusting any of it, `parseKnobs()` sanity-checks each block (`mode
<= 1`, `cc <= 127`); a single implausible value aborts parsing for *all*
eight knobs rather than seeding a partial or garbage mapping -- if the
offset assumption is wrong for a given firmware/unit, the result is "the
knobs stay unbound," never "the knobs are bound to something wrong."

`writeProgram()` (the `CMD_WRITE_DATA` path) is implemented but not called
anywhere automatically, even when `--controller-driver-configure` is
passed -- it's reserved for a future pass once the read path above has seen
real hardware. If you pick this up: the pad-table offset (43) is documented
above but currently unused by any code; validate it the same way before
writing to it, since a wrong write could alter what's stored on the device
itself, not just what this driver reads.

## Known gaps / where to look before trusting this in production

- WinMM SysEx RX/TX: implemented, not run on real Windows.
- MPK Mini mk3 protocol: transcribed from a working reference, not
  independently confirmed against a physical unit.
- **Mode switching depends on PROG SELECT sending Program Change**, which is
  the documented, Zynthian-precedented mechanism but is not independently
  confirmed against real MPK Mini mk3 hardware here. If it turns out the
  device doesn't send PC on its own (or sends something else), modes 1-3
  simply never activate -- mode 0's behavior is unaffected either way, so
  this degrades safely, but it's untested.
- Only 4 of the MPK's 9 program slots (0-3) have a designed mode; 4-8
  intentionally clear the knob defaults and stop there.
- No hotplug detection (matches the rest of this port's MIDI handling).
- `writeProgram()` exists but is never invoked automatically.
- The Windows device-name hint for the MPK Mini mk3
  (`deviceNameHints()`) is unconfirmed -- check with `--list-midi` on real
  Windows hardware and extend the hint list if the reported name differs
  from the ALSA one.

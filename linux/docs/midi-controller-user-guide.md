# MIDI controller drivers -- user guide

Synth One's Linux/Windows port can recognise specific MIDI controllers by
name and give them useful defaults automatically: knobs get mapped to synth
parameters, and the connection is verified over MIDI SysEx where the device
supports it. This is a smaller, synth-scoped take on the idea behind
[Zynthian's `ctrldev` drivers](https://wiki.zynthian.org/index.php/Akai_MPK_Series)
-- Synth One has no chains, mixer strips, or pad-launcher grid, so a driver
here only ever does two things: recognise your controller, and give its
knobs sensible starting points.

If you just plug in a MIDI keyboard or controller and play it, none of this
is required reading -- notes work exactly as before, with or without a
driver loaded. This document is for getting more out of a *specific*,
supported controller, and for understanding what to do if it isn't behaving
as expected.

## Supported controllers

| Controller | Driver name | What it does |
| --- | --- | --- |
| Akai MPK Mini mk3 | `akai-mpk-mini-mk3` | Maps the 8 knobs to synth parameters across 4 switchable modes (32 parameters reachable in total -- see [Switching modes](#switching-modes)). The keybed and 8 of the 16 pads play notes normally; the other 8 pads are dedicated transport/panel buttons -- see [Function pads](#function-pads). |

Don't see your controller? It still works as a plain MIDI keyboard/controller
-- see [Using it with an unsupported controller](#using-it-with-an-unsupported-controller)
below, and consider asking for a driver (see the developer guide for what
that takes to add).

## Quick start

```sh
# See what's compiled into this build
./build/synthone --list-controller-drivers

# Just run normally -- a supported controller is picked up automatically
./build/synthone --midi all

# Same on the GUI
./build/synthone-gui
```

That's it for the common case: with the default `--controller-driver auto`,
if a supported controller is connected, its driver loads by itself and you
should see a status line like:

```
  [ctrl]    akai-mpk-mini-mk3 <- MPK mini 3 / MIDI 1
```

If you don't want this at all, turn it off:

```sh
./build/synthone --controller-driver off
```

Or force a specific driver to load regardless of what name-matching would
have picked (mostly useful for testing):

```sh
./build/synthone --controller-driver akai-mpk-mini-mk3
```

## What the Akai MPK Mini mk3 driver actually does

![Akai MPK Mini mk3 with knobs, keys, and the top-row transport / bottom-row panel-switch pads annotated](images/mpk-mini-mk3-annotated.png)

1. **On startup**, if the device is connected on both MIDI in and out, the
   driver asks it (over SysEx) what its 8 knobs are currently set to send.
2. **Once it gets an answer**, each knob is bound to a fixed synth parameter
   -- whatever CC the knob happens to be sending, mapped to cutoff,
   resonance, attack, release, LFO 1 rate, reverb mix, delay mix, or master
   volume respectively. You don't need to touch anything on the device
   itself; whatever program/CC assignment it's already using is read and
   used as-is.
3. **The keybed and 8 of the 16 pads play notes**, exactly like any other
   MIDI keyboard. The other 8 pads are claimed as dedicated buttons instead
   -- see [Function pads](#function-pads) below.
4. **If the query never completes** -- no output port could be paired with
   the input, or the device doesn't reply -- the knobs simply do nothing
   until you bind them yourself with MIDI Learn (see below). Nothing guesses
   at a mapping; a wrong guess risks reassigning a knob to something that
   collides with a universal MIDI convention (e.g. the mod wheel is always
   CC 1), so the driver would rather do nothing than do something wrong.
   (Function pads are unaffected by this -- pad discovery is a separate part
   of the same query and doesn't depend on the knobs resolving.)
5. **Switching the device's onboard program re-runs the same discovery** for
   a different set of 8 knob targets -- see [Switching modes](#switching-modes)
   -- and re-confirms the function pads too, on every program slot, not just
   the ones with a designed knob mode.

## Function pads

![Akai MPK Mini mk3 with knobs, keys, and the top-row transport / bottom-row panel-switch pads annotated](images/mpk-mini-mk3-annotated.png)

8 of the 16 pads stop playing notes and become dedicated buttons instead:
pressing one of these never sounds a note, on any mode or program slot.

**Top row (PAD5-8): transport.** The closest things Synth One has to
transport controls, since it has no play/stop/record:

| Pad | Function |
| --- | --- |
| PAD5 | Panic -- immediately silences every voice |
| PAD6 | All notes off |
| PAD7 | Arp/Seq on/off |
| PAD8 | Switch between Arp mode and Sequencer mode |

**Bottom row (PAD1-4): panel switching.** GUI only (`synthone-gui`) --
jumps straight to a panel instead of swiping through them:

| Pad | Panel |
| --- | --- |
| PAD1 | MAIN (Generators) |
| PAD2 | ENV (Envelopes) |
| PAD3 | FX (Effects) |
| PAD4 | SEQ (Sequencer) |

On the headless `synthone` host (no GUI, no panels), the bottom row still
stops those 4 pads from playing notes, it just has nothing to switch --
there's no panel to jump to.

These 8 pads are rediscovered every time you switch the device's onboard
program with PROG SELECT (see [Switching modes](#switching-modes)), so they
keep working the same way across every mode, including the 5 undesigned
slots (4-8) where the knobs go unbound.

The remaining 8 pads (PAD9-16, or however your unit numbers them) are
unclaimed and continue to play notes normally.

**Which physical pad is which is an assumption, not a confirmed fact** --
this project doesn't have a physical MPK Mini mk3 to test the pad table's
byte order against the silkscreen. If the two rows turn out to be swapped
on your unit, the functions above still work, just on the opposite row --
nothing will misbehave or leak a note through. If you notice this, it's
useful to report.

## Switching modes

The 8 knobs can reach more than 8 parameters: the MPK Mini mk3's **PROG
SELECT** button (in the PAD CONTROLS row, alongside BANK A/B, CC and PROG
CHANGE) switches between the device's onboard program slots without any
reprogramming, and the driver treats each slot as a different knob mapping,
re-running the same SysEx discovery for that slot's actual CC assignments.

![Akai MPK Mini mk3 with the PROG SELECT button and PAD CONTROLS row highlighted](images/mpk-progselect-annotated.png)

| Program slot | Mode | Knobs 1-8 |
| --- | --- | --- |
| 0 (RAM/default) | Sound | Cutoff, Resonance, Attack, Release, LFO 1 rate, Reverb mix, Delay mix, Master volume |
| 1 | Oscillators/voice | OSC1 volume, OSC2 volume, OSC2 detune, Sub volume, FM amount, Noise volume, Glide, OSC1/2 balance |
| 2 | Envelope depth | Filter attack, Filter decay, Filter sustain, Filter release, Filter/amp env mix, Envelope pitch tracking, Amp decay, Amp sustain |
| 3 | Modulation/FX | LFO 1 amount, LFO 2 rate, LFO 2 amount, Phaser mix, Phaser rate, Autopan amount, Autopan rate, Bitcrush rate |

Consult your MPK Mini mk3's manual for exactly how to reach PROG SELECT +
a program number on your unit (this varies slightly by firmware). Slots 4-8
have no assigned mode -- switching to one just leaves the knobs unbound
until you switch back or MIDI-Learn them yourself.

Mode switching depends on the device actually sending a MIDI Program Change
when you use PROG SELECT -- this is the standard, documented mechanism (the
same one Zynthian's own MPK driver relies on), but hasn't been confirmed
against physical hardware in this project's own testing. If switching
modes doesn't do anything, see [Troubleshooting](#troubleshooting).

### Where each knob shows up in the app (Mode 0)

K1/K2/K8 land on the **MAIN** panel (cutoff, resonance, master volume), K3/K4
on the **ENV** panel (amplitude attack/release):

![MAIN and ENV panels with K1, K2, K3, K4 and K8 marked next to the knobs they control](images/app-main-env-annotated.png)

K5/K6/K7 land on the **FX** panel (LFO 1 rate, reverb mix, delay mix):

![FX panel with K5, K6 and K7 marked next to the knobs they control](images/app-fx-annotated.png)

If you ever forget which physical knob drives which on-screen control in
Mode 0, this is the reference to come back to.

### Where each knob shows up in the app (Mode 1: oscillators/voice)

All 8 land on the **MAIN** panel -- OSC1/OSC2 volume and detune, sub/FM/noise
levels, glide, and the OSC1/OSC2 balance:

![MAIN panel with K1-K8 marked next to the Mode 1 knobs they control](images/app-mode1-oscillators.png)

### Where each knob shows up in the app (Mode 2: envelope depth)

All 8 land on the **ENV** panel -- the filter envelope's full ADSR, how much
it affects cutoff, how much the envelope tracks pitch, and the amplitude
envelope's decay/sustain (the two stages Mode 0 doesn't reach):

![ENV panel with K1-K8 marked next to the Mode 2 knobs they control](images/app-mode2-envelope.png)

### Where each knob shows up in the app (Mode 3: modulation/FX)

All 8 land on the **FX** panel -- LFO 1/LFO 2 depth and LFO 2 rate, phaser
mix/rate, autopan amount/rate, and bitcrush rate:

![FX panel with K1-K8 marked next to the Mode 3 knobs they control](images/app-mode3-modulation.png)

## How this interacts with MIDI Learn

A driver's knob mapping is a *default*, not a lock:

- **Your own MIDI Learn always wins.** If you MIDI-Learn a knob to a
  different parameter, that binding is used from then on, overriding
  whatever the driver had set.
- **Clearing MIDI Learn doesn't remove a driver's defaults.** "Clear All"
  only clears what *you've* explicitly learned; the driver's own mapping
  for the knobs you never touched keeps working.
- **The two never fight.** A CC checked against your explicit learn table
  first; only if that CC has nothing bound to it does the driver's default
  apply.

In short: use the controller as-is if the defaults suit you, or MIDI-Learn
over the top of any knob you want to reassign -- either way there's nothing
to reset or reconfigure first.

## Using it with an unsupported controller

Any MIDI controller works as a plain input with no driver loaded -- notes,
CC, pitch bend, and MIDI Learn all work exactly as they always have. A
driver is purely additive convenience for the specific devices listed above.
If your knobs don't do anything by default, that's expected for any
controller without a driver: bind them yourself via MIDI Learn (in the GUI,
or via the DSP parameter list -- see the main [README](../README.md) for
how MIDI Learn works).

## Troubleshooting

**`--list-controller-drivers` doesn't show anything, or my driver won't
load.**
Check `--controller-driver` isn't set to `off`. Check the device actually
shows up in `--list-midi` -- a driver can only match a device that's
connected and named the way the driver expects. If it's connected but not
matching, the device's reported name may differ from what the driver looks
for (this varies by OS and driver version) -- see the developer guide for
how to extend the name list.

**The status line shows `(SysEx TX unavailable)`.**
The driver found your controller's input port but couldn't find a matching
output port to talk back to it -- so it can identify the device by name but
can't run the SysEx query that discovers the knob CCs. The knobs will need
manual MIDI Learn in this case. This can happen if only the input side of
the device is connected (e.g. via `--midi` naming just one port) or if the
in/out pairing heuristic doesn't recognise your specific device -- see the
developer guide.

**Knobs don't respond even though the status line looks fine.**
The query may not have gotten a reply in time, or the device's SysEx
program dump didn't parse as expected (this is a defensive check, not a
bug report waiting to happen -- see the developer guide for why). Either
way, MIDI-Learn the knob yourself as a reliable fallback.

**Switching modes with PROG SELECT doesn't change what the knobs control.**
The knobs should go briefly unresponsive and then pick up the new mode's
mapping; if nothing changes at all, either the device isn't sending a
Program Change on PROG SELECT the way this driver expects (unverified
against real hardware -- see the developer guide), or you're on a slot
without a designed mode (only 0-3 have one). Try slot 0 first to confirm
the driver itself is working, then try 1-3. As always, MIDI Learn works
regardless of mode switching.

**A pad plays a note instead of doing its function, or the wrong pad does
the wrong thing.** Function pads are only claimed once the driver's startup
SysEx query completes -- if that never happens (see the "SysEx TX
unavailable" and "Knobs don't respond" entries above; pad discovery shares
the same query as the knobs), all 16 pads fall back to playing notes
normally, since nothing claims them. If pads *are* claimed but the two rows
seem swapped, that's the unverified table-order assumption described in
[Function pads](#function-pads) above, not a bug to work around -- MIDI
Learn doesn't apply here since these pads never reach the note path at all.

**No hotplug.** Controllers are matched once at startup against whatever's
already connected when `synthone`/`synthone-gui` starts. Plugging a
controller in after that won't be picked up without restarting -- this is a
limitation of MIDI port handling generally in this port, not specific to
controller drivers.

**Windows.** The controller-driver feature is compiled and shipped in the
Windows build, but has not been validated against real Windows hardware in
this project's own testing (the port is cross-compiled from Linux). If
something doesn't work as described here on Windows specifically, that's
useful to know -- see the developer guide's testing section.

## See also

- [linux/README.md](../README.md) -- the main port documentation, including
  general MIDI setup (`--midi`, `--midi-out`) and MIDI Learn.
- [Developer guide](midi-controller-developer-guide.md) -- architecture, how
  to add a driver for a new controller, and the MPK Mini mk3 protocol
  reference.

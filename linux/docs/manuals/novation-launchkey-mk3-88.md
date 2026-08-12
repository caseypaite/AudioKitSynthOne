# Novation Launchkey MK3 88

![Novation Launchkey MK3 88](images/novation-launchkey-mk3-88.png)

*Photo: Novation. Used here to help you find your way around the control
surface; not affiliated with or endorsed by Novation.*

88 semi-weighted keys, 16 RGB pads, 8 knobs, 8 faders plus a master fader.
The largest of the four Launchkey drivers in this port -- the only one with
physical faders. See
[the Launchkey family overview](#the-launchkey-family) below for how the
other three (Launchkey Mini mk3, Launchkey MK4 37, Launchkey Mini MK4 37)
compare.

## Quick start

```sh
./build/synthone-gui               # or: ./build/synthone --midi all
```

Plug it in **before** launching (no hotplug). On startup, the driver
automatically puts the device into "session mode" (an ordinary MIDI Note)
so the knobs/faders send the CCs this driver expects. Look for:

```
  [ctrl]    novation-launchkey-mk3-88 <- Launchkey 88 MK3 / MIDI 1
```

## What's mapped

| Control | Maps to |
| --- | --- |
| The 8 knobs | OSC1/OSC2 volume, OSC2 detune, sub volume, FM amount, noise volume, glide, OSC1/2 balance |
| The 8 faders | Cutoff, resonance, attack, release, LFO 1 rate, reverb mix, delay mix, arp rate |
| Master fader | Master volume |
| PLAY | Arp/Seq on-off |
| RECORD | Switch between Arp mode and Sequencer mode |

Not mapped: navigation (track left/right, up/down) and per-channel chain
buttons (select/mute/solo) -- no Synth One equivalent action for either. The
keybed and 16 pads play as plain notes.

## Where this shows up in the app

The 8 knobs (oscillators/voice/glide/balance) land entirely on **MAIN**.
From the faders: cutoff/resonance/master volume land on **MAIN**;
attack/release on **ENV**; LFO 1 rate/reverb mix/delay mix on **FX**; arp
rate on **SEQ**:

![MAIN panel with oscillator/voice controls, cutoff, resonance and master volume](images/app/main.png)

![ENV panel with the amplitude ADSR](images/app/env.png)

![SEQ panel with the arpeggiator rate control and 16-step sequencer](images/app/seq.png)

PLAY/RECORD are global transport actions -- the Arp/Seq toggle they drive
also lives on **SEQ**, shown above.

## Troubleshooting

**Knobs/faders don't respond.** Check the status line doesn't show `(SysEx
TX unavailable)` -- the session-mode handshake needs an output port the same
way SysEx does. If port pairing looks fine and controls still don't respond,
your unit may be sending different CCs than the confirmed reference this
driver was built from; MIDI-Learn them yourself as a fallback.

**PLAY/RECORD don't do anything.** These report as MIDI CC presses rather
than Notes, and are gated to one confirmed MIDI channel -- a unit reporting
on a different channel would show this symptom. See the developer guide's
"CC transport" section for the confirmed values.

## The Launchkey family

| Controller | The 8 knobs | Sliders/master |
| --- | --- | --- |
| [Launchkey Mini mk3](novation-launchkey-mini-mk3.md) | Cutoff, resonance, attack, release, LFO 1 rate, reverb mix, delay mix, master volume | -- (no physical sliders) |
| **Launchkey MK3 88** (this device) | OSC1/OSC2 volume, OSC2 detune, sub volume, FM amount, noise volume, glide, OSC1/2 balance | 8 faders: cutoff, resonance, attack, release, LFO 1 rate, reverb mix, delay mix, arp rate. Master fader: master volume. |
| [Launchkey MK4 37](novation-launchkey-mk4-37.md) | Filter attack/decay/sustain/release, filter/amp env mix, envelope pitch tracking, amp decay, amp sustain | -- (no physical sliders) |
| [Launchkey Mini MK4 37](novation-launchkey-mini-mk4-37.md) | **Not mapped** -- relative-encoder output | -- |

All four also map PLAY/RECORD identically to the table above.

## See also

- [MIDI controller drivers -- user guide](../midi-controller-user-guide.md)
- [Developer guide](../midi-controller-developer-guide.md)

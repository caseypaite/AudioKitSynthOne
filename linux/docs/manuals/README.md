# Controller manuals

One illustrated, per-device manual for each of the 20 MIDI controllers this
port recognises automatically. Each covers quick-start, the full parameter
mapping, where the mapped controls show up in `synthone-gui`'s panels, and
device-specific troubleshooting.

For the full protocol reference, architecture, and how to add a new driver,
see the [developer guide](../midi-controller-developer-guide.md). For a
single table covering all 20 devices at once, see the
[user guide](../midi-controller-user-guide.md) -- these per-device manuals
are a more detailed, illustrated companion to that table, not a replacement
for it.

## Akai

- [MPK Mini mk3](akai-mpk-mini-mk3.md)
- [MIDI Mix](akai-midimix.md)
- [APC Key 25 (original)](akai-apc-key25.md)
- [APC Key 25 mk2](akai-apc-key25-mk2.md)
- [APC40 mk2](akai-apc40-mk2.md)
- [MPK249](akai-mpk249.md)

## Arturia

- [KeyLab mkII 61](arturia-keylab-61-mk2.md)

## Novation

- [Launchkey Mini mk3](novation-launchkey-mini-mk3.md)
- [Launchkey MK3 88](novation-launchkey-mk3-88.md)
- [Launchkey MK4 37](novation-launchkey-mk4-37.md)
- [Launchkey Mini MK4 37](novation-launchkey-mini-mk4-37.md)
- [Launchpad Mini (original/mk1)](novation-launchpad-mini.md)
- [Launchpad Mini mk3](novation-launchpad-mini-mk3.md)
- [Launchpad X](novation-launchpad-x.md)
- [Launchpad Pro mk2](novation-launchpad-pro-mk2.md)
- [Launchpad Pro mk3](novation-launchpad-pro-mk3.md)

## Korg

- [nanoKONTROL2](korg-nanokontrol2.md)

## Behringer

- [MOTÖR61 / MOTÖR49](behringer-motor.md)

## WORLDE

- [MINI](worlde-mini.md)

## Teenage Engineering

- [OP-1 (original)](teenage-engineering-op1.md)

## A note on the photos

Product photos in these manuals come from manufacturer product pages where
one exists, and from retailer/marketplace listings otherwise (noted in each
manual's photo caption when that's the case). They're included to help you
find your way around a physical control surface, not as marketing material
-- none of these manufacturers endorse or are affiliated with this project.
The [WORLDE MINI manual](worlde-mini.md) in particular uses an unconfirmed
visual stand-in; see its caption for why.

## A note on the annotated diagrams

11 of the 20 manuals additionally include a hand-annotated diagram (colored
boxes + a legend, in the same style as the Akai MPK Mini mk3 reference
diagrams in the [developer guide](../midi-controller-developer-guide.md))
marking exactly which physical knob/fader/button maps to what. The other 9
don't, for one of three reasons, each stated explicitly in that manual: the
device maps nothing at all (4 of the 5 Launchpads), the only available
photo is too low-resolution or too ambiguous a match to annotate accurately
and honestly (Behringer MOTÖR, WORLDE MINI), or this project can't
confidently identify which physical button corresponds to a given mapped
function from the photo alone (Launchpad Pro mk2, Teenage Engineering OP-1).
A wrong box would be worse than no box; the mapping table in each manual is
always the authoritative reference regardless of whether a diagram exists.

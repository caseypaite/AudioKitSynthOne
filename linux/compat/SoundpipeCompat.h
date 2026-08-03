//
//  SoundpipeCompat.h
//  AudioKitSynthOne - Linux port
//
//  Bridges stock Soundpipe (github.com/PaulBatchelor/Soundpipe) to the AudioKit
//  fork that Synth One was written against. Two gaps are closed here, both
//  without modifying any third-party source:
//
//  1. sp_port. AudioKit's fork spells the one-pole smoothing half-time `htime`
//     and takes it as an argument to sp_port_init(). Stock calls the same field
//     `smooth` and defaults it in init(). Rather than patch Soundpipe, this
//     header declares an equivalent module (s1_port) with the AudioKit shape
//     and macro-redirects the sp_port_* spellings onto it. The DSP is identical
//     -- see linux/src/s1_port.c, a transcription of stock modules/port.c.
//
//  2. sp_oscmorph2d. This is a Synth One-local Soundpipe module living in
//     DSP/Kernel/oscmorph2d.c, which declares its own prototypes but ships no
//     header. The C++ sources need the type, so it is declared here.
//
//  Include this instead of <soundpipe.h>.
//

#pragma once

extern "C" {
#include <soundpipe.h>
}

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// 1. Portamento with an AudioKit-shaped interface
// ---------------------------------------------------------------------------

typedef struct {
    SPFLOAT htime;
    SPFLOAT a1, b0, y0, phtime;
    SPFLOAT onedsr;
} s1_port;

int s1_port_create(s1_port **p);
int s1_port_destroy(s1_port **p);
int s1_port_init(sp_data *sp, s1_port *p, SPFLOAT htime);
int s1_port_compute(sp_data *sp, s1_port *p, SPFLOAT *in, SPFLOAT *out);
int s1_port_reset(sp_data *sp, s1_port *p, SPFLOAT *in);

// ---------------------------------------------------------------------------
// 2. Synth One's 2-D wavetable morphing oscillator (DSP/Kernel/oscmorph2d.c)
// ---------------------------------------------------------------------------

typedef struct {
    SPFLOAT   freq, amp, iphs;
    int32_t   lphs;
    sp_ftbl **tbl;
    int       inc;
    SPFLOAT   wtpos;
    int       nft;              /* waveforms per band */
    int       nbl;              /* number of bandlimited copies */
    SPFLOAT  *bandlimitFreqs;   /* nbl entries */
    SPFLOAT   enableBandlimit;
    int       bandlimitIndexOverride;
} sp_oscmorph2d;

int sp_oscmorph2d_create(sp_oscmorph2d **p);
int sp_oscmorph2d_destroy(sp_oscmorph2d **p);
int sp_oscmorph2d_init(sp_data *sp, sp_oscmorph2d *osc, sp_ftbl **ft, int nft, int nbl,
                       SPFLOAT *bandlimitFreqs, SPFLOAT iphs);
int sp_oscmorph2d_compute(sp_data *sp, sp_oscmorph2d *p, SPFLOAT *in, SPFLOAT *out);

#ifdef __cplusplus
} // extern "C"
#endif

// Redirect the synth's sp_port_* usage onto s1_port. Declared above first, so
// only the Synth One sources that include this header are affected -- stock
// Soundpipe is compiled separately and keeps its own sp_port intact.
#define sp_port         s1_port
#define sp_port_create  s1_port_create
#define sp_port_destroy s1_port_destroy
#define sp_port_init    s1_port_init
#define sp_port_compute s1_port_compute
#define sp_port_reset   s1_port_reset

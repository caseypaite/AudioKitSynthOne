/*
 * oscmorph2d.c
 * AudioKitSynthOne - Linux port
 *
 * Synth One's two-dimensional wavetable oscillator.
 *
 * NOTE: this file is a reimplementation, not upstream code. The repository's
 * DSP/Kernel/oscmorph2d.c is an earlier one-dimensional revision whose
 * signature no longer matches its caller (S1NoteState passes a band count, a
 * band-frequency table, and sets ->enableBandlimit / ->bandlimitIndexOverride).
 * The matching version ships inside AudioKit's Soundpipe fork, which is not
 * public, so it is reconstructed here from the call sites and the shipped
 * wavetable bank. The interpolation math is carried over verbatim from the
 * repository's 1-D version; only band selection is added.
 *
 * The two dimensions:
 *
 *   dimension 1 -- morph position `wtpos` in [0,1] across `nft` waveforms
 *                  (triangle, square, pwm, sawtooth)
 *   dimension 2 -- band index chosen from the oscillator frequency, across
 *                  `nbl` bandlimited copies of that set
 *
 * Table layout, matching AKSynthOne.swift's upload loop:
 *
 *     tbl[bandIndex * nft + waveformIndex]
 *
 * Band i holds waveforms bandlimited so that a fundamental of
 * bandlimitFrequencies[i] has no partials above Nyquist -- e.g. band 7 is
 * 525 Hz with 42 harmonics (525 * 42 = 22050). Band 0 is the naive,
 * full-bandwidth waveform and is what gets used when bandlimiting is off.
 */

#include <math.h>
#include <stdlib.h>

#include <soundpipe.h>

typedef struct {
    SPFLOAT   freq, amp, iphs;
    int32_t   lphs;
    sp_ftbl **tbl;
    int       inc;
    SPFLOAT   wtpos;
    int       nft;              /* waveforms per band */
    int       nbl;              /* number of bands */
    SPFLOAT  *bandlimitFreqs;   /* nbl entries */
    SPFLOAT   enableBandlimit;
    int       bandlimitIndexOverride;
} sp_oscmorph2d;

int sp_oscmorph2d_create(sp_oscmorph2d **p) {
    *p = malloc(sizeof(sp_oscmorph2d));
    if (*p == NULL) return SP_NOT_OK;
    return SP_OK;
}

int sp_oscmorph2d_destroy(sp_oscmorph2d **p) {
    free(*p);
    *p = NULL;
    return SP_OK;
}

int sp_oscmorph2d_init(sp_data *sp, sp_oscmorph2d *osc, sp_ftbl **ft, int nft, int nbl,
                       SPFLOAT *bandlimitFreqs, SPFLOAT iphs) {
    int i;
    const int total = nft * nbl;

    osc->freq = 0;
    osc->amp = 0;
    osc->iphs = iphs;
    osc->tbl = ft;
    osc->nft = nft;
    osc->nbl = nbl;
    osc->bandlimitFreqs = bandlimitFreqs;
    osc->enableBandlimit = 0;
    osc->bandlimitIndexOverride = -1;
    osc->wtpos = 0;
    osc->inc = 0;

    /* every table in the bank must share a size, as in sp_oscmorph_init */
    for (i = 0; i < total; i++) {
        if (osc->tbl[i]->size != osc->tbl[0]->size) {
            fprintf(stderr, "sp_oscmorph2d: size mismatch\n");
            return SP_NOT_OK;
        }
    }

    osc->lphs = ((int32_t)(iphs * SP_FT_MAXLEN)) & SP_FT_PHMASK;
    (void)sp;
    return SP_OK;
}

/* Lowest band whose fundamental limit is at or above `freq`; band 0 is the
   non-bandlimited set and is skipped when bandlimiting is enabled. */
static int sp_oscmorph2d_band(sp_oscmorph2d *osc, SPFLOAT freq) {
    int i;

    if (osc->bandlimitIndexOverride >= 0 && osc->bandlimitIndexOverride < osc->nbl) {
        return osc->bandlimitIndexOverride;
    }
    if (osc->enableBandlimit == 0 || osc->bandlimitFreqs == NULL) {
        return 0;
    }
    for (i = 1; i < osc->nbl; i++) {
        if (freq <= osc->bandlimitFreqs[i]) {
            return i;
        }
    }
    return osc->nbl - 1;
}

int sp_oscmorph2d_compute(sp_data *sp, sp_oscmorph2d *osc, SPFLOAT *in, SPFLOAT *out) {
    sp_ftbl *ftp1;
    SPFLOAT  amp, cps, fract, v1, v2;
    SPFLOAT *ft1, *ft2;
    int32_t  phs, lobits, pos;
    SPFLOAT  sicvt = osc->tbl[0]->sicvt;
    int      band, base;

    (void)sp;
    (void)in;

    /* Use only the fractional part of the position or 1 */
    if (osc->wtpos > 1.0) {
        osc->wtpos -= (int)osc->wtpos;
    }
    SPFLOAT findex = osc->wtpos * (osc->nft - 1);
    int index = floor(findex);
    SPFLOAT wtfrac = findex - index;

    band = sp_oscmorph2d_band(osc, osc->freq);
    base = band * osc->nft;

    lobits = osc->tbl[0]->lobits;
    amp = osc->amp;
    cps = osc->freq;
    phs = osc->lphs;
    ftp1 = osc->tbl[base + index];
    ft1 = osc->tbl[base + index]->tbl;

    if (index >= osc->nft - 1) {
        ft2 = ft1;
    } else {
        ft2 = osc->tbl[base + index + 1]->tbl;
    }

    osc->inc = (int32_t)lrintf(cps * sicvt);

    fract = ((phs) & ftp1->lomask) * ftp1->lodiv;

    pos = phs >> lobits;

    v1 = (1 - wtfrac) *
        *(ft1 + pos) +
        wtfrac *
        *(ft2 + pos);
    v2 = (1 - wtfrac) *
        *(ft1 + ((pos + 1) % ftp1->size)) +
        wtfrac *
        *(ft2 + ((pos + 1) % ftp1->size));

    *out = (v1 + (v2 - v1) * fract) * amp;

    phs += osc->inc;
    phs &= SP_FT_PHMASK;

    osc->lphs = phs;
    return SP_OK;
}

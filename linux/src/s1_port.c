/*
 * s1_port.c
 * AudioKitSynthOne - Linux port
 *
 * Portamento / one-pole control smoother with the interface AudioKit's
 * Soundpipe fork exposes: the half-time is named `htime` and is supplied to
 * init() rather than defaulted.
 *
 * The filter itself is a transcription of stock Soundpipe's modules/port.c:
 *
 *   y(n) = b0*x(n) + a1*y(n-1),  a1 = 0.5^(1/(t*sr)),  b0 = 1 - a1
 *
 * See linux/compat/SoundpipeCompat.h for why this exists.
 */

#include <math.h>
#include <stdlib.h>

#include <soundpipe.h>

typedef struct {
    SPFLOAT htime;
    SPFLOAT a1, b0, y0, phtime;
    SPFLOAT onedsr;
} s1_port;

int s1_port_create(s1_port **p) {
    *p = malloc(sizeof(s1_port));
    if (*p == NULL) return SP_NOT_OK;
    return SP_OK;
}

int s1_port_destroy(s1_port **p) {
    free(*p);
    *p = NULL;
    return SP_OK;
}

int s1_port_init(sp_data *sp, s1_port *p, SPFLOAT htime) {
    p->y0 = 0;
    p->b0 = 0;
    p->a1 = 0;
    p->phtime = -100.0;
    p->htime = htime;

    /* using this constant shaves off a multiply operation */
    p->onedsr = 1.0 / sp->sr;
    return SP_OK;
}

int s1_port_compute(sp_data *sp, s1_port *p, SPFLOAT *in, SPFLOAT *out) {
    (void)sp;
    if (p->phtime != p->htime) {
        p->a1 = pow(0.5, p->onedsr / p->htime);
        p->b0 = 1.0 - p->a1;
        p->phtime = p->htime;
    }

    p->y0 = p->b0 * (*in) + p->a1 * p->y0;
    *out = p->y0;
    return SP_OK;
}

int s1_port_reset(sp_data *sp, s1_port *p, SPFLOAT *in) {
    (void)sp;
    p->y0 = *in;
    return SP_OK;
}

/* Copyright (c) 2026 HAUTLLE. SPDX-License-Identifier: BSD-3-Clause */
#include "kiss_fftr.h"
#include "tesselator.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef char require_arm_tess_index_16[(sizeof(TESSindex) == 2) ? 1 : -1];
typedef char require_float32[(sizeof(kiss_fft_scalar) == 4) ? 1 : -1];

int main(void) {
    enum { N = 16 };
    kiss_fft_scalar input[N], inverse[N];
    kiss_fft_cpx spectrum[N / 2 + 1];
    kiss_fftr_cfg forward = kiss_fftr_alloc(N, 0, NULL, NULL);
    kiss_fftr_cfg backward = kiss_fftr_alloc(N, 1, NULL, NULL);
    if (!forward || !backward) return 1;
    for (int pattern = 0; pattern < 2; ++pattern) {
        for (int i = 0; i < N; ++i)
            input[i] = pattern ? (float)sin(2.0 * 3.141592653589793 * 3 * i / N) : (i == 0);
        kiss_fftr(forward, input, spectrum);
        for (int i = 0; i <= N / 2; ++i) {
            if (!isfinite(spectrum[i].r) || !isfinite(spectrum[i].i)) return 2;
            if (!pattern && (fabs(spectrum[i].r - 1.0) > 1e-5 || fabs(spectrum[i].i) > 1e-5)) return 3;
        }
        if (pattern && fabs(hypot(spectrum[3].r, spectrum[3].i) - N / 2.0) > 1e-4) return 4;
        kiss_fftri(backward, spectrum, inverse);
        for (int i = 0; i < N; ++i)
            if (!isfinite(inverse[i]) || fabs(inverse[i] / N - input[i]) > 1e-5) return 5;
    }
    kiss_fftr_free(forward);
    kiss_fftr_free(backward);
    const TESSreal square[] = {0, 0, 2, 0, 2, 2, 0, 2};
    TESStesselator *tess = tessNewTess(NULL);
    if (!tess) return 6;
    tessAddContour(tess, 2, square, 2 * sizeof(TESSreal), 4);
    if (!tessTesselate(tess, TESS_WINDING_ODD, TESS_POLYGONS, 3, 2, NULL)) return 7;
    int count = tessGetElementCount(tess), vertices = tessGetVertexCount(tess);
    const TESSindex *elements = tessGetElements(tess);
    const TESSreal *points = tessGetVertices(tess);
    if (count != 2 || vertices != 4) return 8;
    double area = 0;
    for (int i = 0; i < count; ++i) {
        TESSindex a = elements[3*i], b = elements[3*i+1], c = elements[3*i+2];
        if (a >= vertices || b >= vertices || c >= vertices) return 9;
        area += fabs((points[2*b]-points[2*a])*(points[2*c+1]-points[2*a+1]) -
                     (points[2*c]-points[2*a])*(points[2*b+1]-points[2*a+1])) * 0.5;
    }
    tessDeleteTess(tess);
    if (fabs(area - 4.0) > 1e-5) return 10;
    puts("PASS: float32 real FFT impulse/sine/round-trip and ARM uint16 tessellation area/indices");
    return 0;
}

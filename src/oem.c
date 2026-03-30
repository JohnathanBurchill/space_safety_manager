/*
    Space Safety Manager  oem.c

    Copyright (C) 2026  Johnathan K Burchill

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "oem.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int buf_append(char *buf, int buflen, int *offset, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *offset, buflen - *offset, fmt, ap);
    va_end(ap);
    if (n < 0 || *offset + n >= buflen) {
        return -1;
    }
    *offset += n;
    return 0;
}

int oem_write(const opm_state_t *meta, const ephem_point_t *pts, int n_pts,
              char *buf, int buflen)
{
    int off = 0;
    char ts[64];
    utc_time_t now;
    utc_now(&now);
    utc_format(&now, ts, sizeof ts);

    /* Header */
    if (buf_append(buf, buflen, &off, "CCSDS_OEM_VERS = 3.0\n") != 0) return -1;
    if (buf_append(buf, buflen, &off, "CREATION_DATE = %s\n", ts) != 0) return -1;
    if (buf_append(buf, buflen, &off, "ORIGINATOR = University of Calgary\n") != 0) return -1;
    if (buf_append(buf, buflen, &off, " \n") != 0) return -1;

    /* Meta block */
    utc_time_t start_utc, stop_utc;
    jd_to_utc(pts[0].jd, &start_utc);
    jd_to_utc(pts[n_pts - 1].jd, &stop_utc);
    char start_ts[64], stop_ts[64];
    utc_format(&start_utc, start_ts, sizeof start_ts);
    utc_format(&stop_utc, stop_ts, sizeof stop_ts);

    if (buf_append(buf, buflen, &off, "META_START\n") != 0) return -1;
    if (buf_append(buf, buflen, &off, "OBJECT_NAME = %s\n", meta->object_name) != 0) return -1;
    if (buf_append(buf, buflen, &off, "OBJECT_ID = %s\n", meta->object_id) != 0) return -1;
    if (buf_append(buf, buflen, &off, "CENTER_NAME = %s\n", meta->center_name) != 0) return -1;
    if (buf_append(buf, buflen, &off, "REF_FRAME = %s\n", meta->ref_frame) != 0) return -1;
    if (buf_append(buf, buflen, &off, "TIME_SYSTEM = %s\n", meta->time_system) != 0) return -1;
    if (buf_append(buf, buflen, &off, "START_TIME = %s\n", start_ts) != 0) return -1;
    if (buf_append(buf, buflen, &off, "STOP_TIME = %s\n", stop_ts) != 0) return -1;
    if (buf_append(buf, buflen, &off, "META_STOP\n") != 0) return -1;
    if (buf_append(buf, buflen, &off, " \n") != 0) return -1;

    /* Ephemeris data */
    for (int i = 0; i < n_pts; i++) {
        utc_time_t t;
        char ets[64];
        jd_to_utc(pts[i].jd, &t);
        utc_format(&t, ets, sizeof ets);
        if (buf_append(buf, buflen, &off,
                       "%s %e %e %e %e %e %e\n",
                       ets,
                       pts[i].pos[0], pts[i].pos[1], pts[i].pos[2],
                       pts[i].vel[0], pts[i].vel[1], pts[i].vel[2]) != 0)
            return -1;
    }

    if (buf_append(buf, buflen, &off, " \n") != 0) return -1;
    if (buf_append(buf, buflen, &off, " \n") != 0) return -1;

    /* Covariance block */
    if (buf_append(buf, buflen, &off, "COVARIANCE_START\n") != 0) return -1;

    double cr2 = OEM_SIGMA_R * OEM_SIGMA_R;
    double ct2 = OEM_SIGMA_T * OEM_SIGMA_T;
    double cn2 = OEM_SIGMA_N * OEM_SIGMA_N;
    double crd2 = OEM_SIGMA_RDOT * OEM_SIGMA_RDOT;
    double ctd2 = OEM_SIGMA_TDOT * OEM_SIGMA_TDOT;
    double cnd2 = OEM_SIGMA_NDOT * OEM_SIGMA_NDOT;

    for (int i = 0; i < n_pts; i++) {
        utc_time_t t;
        char ets[64];
        jd_to_utc(pts[i].jd, &t);
        utc_format(&t, ets, sizeof ets);

        if (buf_append(buf, buflen, &off, "EPOCH = %s\n", ets) != 0) return -1;
        if (buf_append(buf, buflen, &off, "COV_REF_FRAME = RTN\n") != 0) return -1;
        /* Lower-triangle 6x6 symmetric covariance (diagonal only) */
        if (buf_append(buf, buflen, &off, "%e\n", cr2) != 0) return -1;
        if (buf_append(buf, buflen, &off, "%e %e\n", 0.0, ct2) != 0) return -1;
        if (buf_append(buf, buflen, &off, "%e %e %e\n", 0.0, 0.0, cn2) != 0) return -1;
        if (buf_append(buf, buflen, &off, "%e %e %e %e\n", 0.0, 0.0, 0.0, crd2) != 0) return -1;
        if (buf_append(buf, buflen, &off, "%e %e %e %e %e\n", 0.0, 0.0, 0.0, 0.0, ctd2) != 0)
            return -1;
        if (buf_append(buf, buflen, &off, "%e %e %e %e %e %e\n", 0.0, 0.0, 0.0, 0.0, 0.0, cnd2) !=
            0)
            return -1;
    }

    if (buf_append(buf, buflen, &off, "COVARIANCE_STOP\n") != 0) return -1;

    return off;
}

int oem_write_file(const char *filename, const opm_state_t *meta,
                   const ephem_point_t *pts, int n_pts)
{
    char *buf = malloc(OEM_BUF_SIZE);
    if (buf == NULL) {
        fprintf(stderr, "Failed to allocate OEM buffer\n");
        return -1;
    }

    int len = oem_write(meta, pts, n_pts, buf, OEM_BUF_SIZE);
    if (len < 0) {
        fprintf(stderr, "Failed to format OEM data\n");
        free(buf);
        return -1;
    }

    FILE *f = fopen(filename, "w");
    if (f == NULL) {
        fprintf(stderr, "Error opening output file: %s\n", filename);
        free(buf);
        return -1;
    }

    fwrite(buf, 1, len, f);
    fclose(f);
    free(buf);
    return 0;
}

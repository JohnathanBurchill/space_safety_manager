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

#include <math.h>
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

/* 3x3 ECEF->RTN rotation from a state: rows are R (radial), T (transverse,
   completes the right-handed set), N (cross-track), each in ECEF. */
static void rtn_basis(const double r[3], const double v[3], double Q[3][3])
{
    double rn = sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    double R[3] = { r[0] / rn, r[1] / rn, r[2] / rn };
    double h[3] = { r[1] * v[2] - r[2] * v[1],
                    r[2] * v[0] - r[0] * v[2],
                    r[0] * v[1] - r[1] * v[0] };
    double hn = sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
    double N[3] = { h[0] / hn, h[1] / hn, h[2] / hn };
    double T[3] = { N[1] * R[2] - N[2] * R[1],
                    N[2] * R[0] - N[0] * R[2],
                    N[0] * R[1] - N[1] * R[0] };
    for (int j = 0; j < 3; j++) { Q[0][j] = R[j]; Q[1][j] = T[j]; Q[2][j] = N[j]; }
}

/* RTN 6x6 covariance at one ephemeris point, grown from the GNSS fix. The
   receiver gives a diagonal ECEF 1-sigma; grow it over the time dt since the
   epoch with the free-drift state transition (Phi = [[I, dt*I],[0, I]]), which
   adds dt^2*sigma_vel^2 to the position variance and a dt*sigma_vel^2 position-
   velocity correlation, then rotate ECEF->RTN with blkdiag(Q,Q). The rotation
   turns the per-axis ECEF sigmas into the correlated R/T/N matrix the API
   expects. The free-drift growth ignores gravity-gradient coupling and Earth
   rotation -- a first-order, conservative approximation for screening. */
static void rtn_cov(const opm_state_t *m, const ephem_point_t *p, double dt,
                    double P[6][6])
{
    double Pe[6][6] = { { 0 } };
    for (int k = 0; k < 3; k++) {
        double sp2 = m->pos_sigma[k] * m->pos_sigma[k];
        double sv2 = m->vel_sigma[k] * m->vel_sigma[k];
        Pe[k][k]         = sp2 + dt * dt * sv2;
        Pe[k + 3][k + 3] = sv2;
        Pe[k][k + 3]     = dt * sv2;
        Pe[k + 3][k]     = dt * sv2;
    }

    double Q[3][3];
    rtn_basis(p->pos, p->vel, Q);
    double T6[6][6] = { { 0 } };
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) { T6[a][b] = Q[a][b]; T6[a + 3][b + 3] = Q[a][b]; }

    /* P = T6 * Pe * T6^T */
    double M[6][6];
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 6; j++) {
            double s = 0.0;
            for (int k = 0; k < 6; k++) s += T6[i][k] * Pe[k][j];
            M[i][j] = s;
        }
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 6; j++) {
            double s = 0.0;
            for (int k = 0; k < 6; k++) s += M[i][k] * T6[j][k];
            P[i][j] = s;
        }
}

/* Fixed conservative diagonal RTN covariance (the legacy fallback used when
   the OPM carried no uncertainty). */
static void fixed_cov(double P[6][6])
{
    double d[6] = { OEM_SIGMA_R * OEM_SIGMA_R, OEM_SIGMA_T * OEM_SIGMA_T,
                    OEM_SIGMA_N * OEM_SIGMA_N, OEM_SIGMA_RDOT * OEM_SIGMA_RDOT,
                    OEM_SIGMA_TDOT * OEM_SIGMA_TDOT, OEM_SIGMA_NDOT * OEM_SIGMA_NDOT };
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 6; j++) P[i][j] = (i == j) ? d[i] : 0.0;
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

    /* Covariance block. With GNSS uncertainty in the OPM, each epoch gets the
       fix's covariance grown over the elapsed time and rotated into that
       point's RTN frame; otherwise the fixed conservative diagonal is used. */
    if (buf_append(buf, buflen, &off, "COVARIANCE_START\n") != 0) return -1;

    for (int i = 0; i < n_pts; i++) {
        utc_time_t t;
        char ets[64];
        jd_to_utc(pts[i].jd, &t);
        utc_format(&t, ets, sizeof ets);

        double P[6][6];
        if (meta->has_covariance) {
            double dt = (pts[i].jd - pts[0].jd) * 86400.0;   /* s since epoch */
            rtn_cov(meta, &pts[i], dt, P);
        } else {
            fixed_cov(P);
        }

        if (buf_append(buf, buflen, &off, "EPOCH = %s\n", ets) != 0) return -1;
        if (buf_append(buf, buflen, &off, "COV_REF_FRAME = RTN\n") != 0) return -1;
        /* Lower-triangle of the 6x6 symmetric covariance, one row per line. */
        for (int r = 0; r < 6; r++) {
            char ln[256];
            int p = 0;
            for (int c = 0; c <= r; c++)
                p += snprintf(ln + p, sizeof ln - p, "%s%e", c ? " " : "", P[r][c]);
            if (buf_append(buf, buflen, &off, "%s\n", ln) != 0) return -1;
        }
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

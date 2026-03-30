/*
    Space Safety Manager  propagate.c

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

#include "propagate.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Compute acceleration from two-body + J2 perturbation.
 * pos: position in km (ITRF)
 * acc: output acceleration in km/s^2
 */
static void j2_acceleration(const double pos[3], double acc[3])
{
    double r2 = pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2];
    double r = sqrt(r2);
    double r3 = r2 * r;
    double r5 = r3 * r2;

    /* Two-body */
    acc[0] = -PROP_MU * pos[0] / r3;
    acc[1] = -PROP_MU * pos[1] / r3;
    acc[2] = -PROP_MU * pos[2] / r3;

    /* J2 perturbation */
    double z2 = pos[2] * pos[2];
    double fac = 1.5 * PROP_J2 * PROP_MU * PROP_RE * PROP_RE / r5;
    double fxy = fac * (5.0 * z2 / r2 - 1.0);
    double fz = fac * (5.0 * z2 / r2 - 3.0);

    acc[0] -= fxy * pos[0];
    acc[1] -= fxy * pos[1];
    acc[2] -= fz * pos[2];
}

/*
 * RK4 step for the 6-element state [x, y, z, vx, vy, vz].
 */
static void rk4_step(double state[6], double dt)
{
    double k1[6], k2[6], k3[6], k4[6];
    double tmp[6];
    double acc[3];

    /* k1 */
    j2_acceleration(state, acc);
    k1[0] = state[3];
    k1[1] = state[4];
    k1[2] = state[5];
    k1[3] = acc[0];
    k1[4] = acc[1];
    k1[5] = acc[2];

    /* k2 */
    for (int i = 0; i < 6; i++) {
        tmp[i] = state[i] + 0.5 * dt * k1[i];
    }
    j2_acceleration(tmp, acc);
    k2[0] = tmp[3];
    k2[1] = tmp[4];
    k2[2] = tmp[5];
    k2[3] = acc[0];
    k2[4] = acc[1];
    k2[5] = acc[2];

    /* k3 */
    for (int i = 0; i < 6; i++) {
        tmp[i] = state[i] + 0.5 * dt * k2[i];
    }
    j2_acceleration(tmp, acc);
    k3[0] = tmp[3];
    k3[1] = tmp[4];
    k3[2] = tmp[5];
    k3[3] = acc[0];
    k3[4] = acc[1];
    k3[5] = acc[2];

    /* k4 */
    for (int i = 0; i < 6; i++) {
        tmp[i] = state[i] + dt * k3[i];
    }
    j2_acceleration(tmp, acc);
    k4[0] = tmp[3];
    k4[1] = tmp[4];
    k4[2] = tmp[5];
    k4[3] = acc[0];
    k4[4] = acc[1];
    k4[5] = acc[2];

    /* Update state */
    for (int i = 0; i < 6; i++) {
        state[i] += dt / 6.0 * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
    }
}

int prop_propagate(const opm_state_t *initial, double duration_sec, double step_sec,
                   ephem_point_t *out, int *n_points)
{
    int max_pts = (int)(duration_sec / step_sec) + 2;
    if (max_pts > PROP_MAX_POINTS) {
        fprintf(stderr, "Too many propagation points (%d > %d)\n", max_pts, PROP_MAX_POINTS);
        return -1;
    }

    double state[6];
    state[0] = initial->pos[0];
    state[1] = initial->pos[1];
    state[2] = initial->pos[2];
    state[3] = initial->vel[0];
    state[4] = initial->vel[1];
    state[5] = initial->vel[2];

    double jd0 = utc_to_jd(&initial->epoch);
    double t = 0.0;
    int idx = 0;

    /* Use a smaller internal step for accuracy, output at step_sec intervals */
    double internal_dt = 10.0; /* 10-second internal steps */
    if (internal_dt > step_sec) {
        internal_dt = step_sec;
    }

    /* Store initial point */
    out[idx].jd = jd0;
    memcpy(out[idx].pos, state, 3 * sizeof(double));
    memcpy(out[idx].vel, state + 3, 3 * sizeof(double));
    idx++;

    double next_output = step_sec;

    while (t < duration_sec && idx < max_pts) {
        double dt = internal_dt;
        if (t + dt > next_output) {
            dt = next_output - t;
        }

        rk4_step(state, dt);
        t += dt;

        /* Output point at each step_sec interval */
        if (fabs(t - next_output) < 1e-6) {
            out[idx].jd = jd0 + t / 86400.0;
            memcpy(out[idx].pos, state, 3 * sizeof(double));
            memcpy(out[idx].vel, state + 3, 3 * sizeof(double));
            idx++;
            next_output += step_sec;
        }
    }

    *n_points = idx;
    return 0;
}

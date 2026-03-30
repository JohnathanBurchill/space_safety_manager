/*
    Space Safety Manager  propagate.h

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

#ifndef PROPAGATE_H
#define PROPAGATE_H

#include "opm.h"

#define PROP_MAX_POINTS 8192

/* Earth constants */
#define PROP_MU     398600.4418     /* km^3/s^2 */
#define PROP_RE     6378.137        /* km */
#define PROP_J2     1.08263e-3

typedef struct ephem_point {
    double jd;      /* Julian date (UTC) */
    double pos[3];  /* km, ITRF */
    double vel[3];  /* km/s, ITRF */
} ephem_point_t;

/*
 * Propagate from OPM initial state using RK4 with J2 perturbation.
 * duration_sec: propagation time in seconds
 * step_sec: output step size in seconds
 * out: array of at least (duration_sec / step_sec + 2) elements
 * n_points: number of points written
 * Returns 0 on success, -1 on error.
 */
int prop_propagate(const opm_state_t *initial, double duration_sec, double step_sec,
                   ephem_point_t *out, int *n_points);

#endif /* PROPAGATE_H */

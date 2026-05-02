/*
    Space Safety Manager  opm.h

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

#ifndef OPM_H
#define OPM_H

#include "util.h"

typedef struct opm_state {
    char object_name[128];
    char object_id[64];
    char center_name[32];
    char ref_frame[16];
    char time_system[8];
    utc_time_t epoch;
    double pos[3];                  /* km, ECEF/ITRF */
    double vel[3];                  /* km/s, ECEF/ITRF */
    double hard_body_radius;        /* m */
    double ballistic_coef;          /* kg/m^2 */
    int sequence_number;

    /* Mean orbital elements (if present in OPM). has_mean_elements = 1 when populated. */
    int has_mean_elements;
    double mean_perigee_alt_km;
    double mean_apogee_alt_km;
    double mean_inclination_deg;
    double mean_arg_perigee_deg;
    double mean_raan_deg;           /* longitude of ascending node */
    double mean_mean_anomaly_deg;
} opm_state_t;

/*
 * Parse an ExoLaunch deployment file (.opm).
 * If sat_name is non-NULL, find that satellite's entry by name.
 * If sat_name is NULL, use the default SSM_OBJECT_NAME from config.h.
 * Returns 0 on success, -1 on error.
 */
int opm_parse(const char *filename, const char *sat_name, opm_state_t *state);

#endif /* OPM_H */

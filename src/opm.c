/*
    Space Safety Manager  opm.c

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

#include "opm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim(char *s)
{
    int n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        n--;
    }
    s[n] = '\0';
}

/* Extract value after "key:" from a line. Returns pointer into line, or NULL. */
static const char *get_value(const char *line, const char *key)
{
    /* Skip leading whitespace and "- " list prefix */
    const char *p = line;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (p[0] == '-' && p[1] == ' ') {
        p += 2;
    }

    size_t klen = strlen(key);
    if (strncmp(p, key, klen) != 0) {
        return NULL;
    }
    p += klen;
    if (*p != ':') {
        return NULL;
    }
    p++;
    while (*p == ' ') {
        p++;
    }
    return p;
}

/* Parse "[x, y, z]" into 3 doubles */
static int parse_vec3(const char *s, double out[3])
{
    return sscanf(s, "[%lf, %lf, %lf]", &out[0], &out[1], &out[2]) == 3 ? 0 : -1;
}

int opm_parse(const char *filename, const char *sat_name, opm_state_t *state)
{
    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        fprintf(stderr, "Error opening OPM file: %s\n", filename);
        return -1;
    }

    if (sat_name == NULL) {
        sat_name = "FrontierSat";
    }

    memset(state, 0, sizeof *state);
    snprintf(state->center_name, sizeof state->center_name, "EARTH");
    snprintf(state->ref_frame, sizeof state->ref_frame, "ITRF");
    snprintf(state->time_system, sizeof state->time_system, "UTC");

    char line[512];
    const char *val;
    int in_target = 0;
    int found = 0;
    int got_pos = 0, got_vel = 0, got_epoch = 0;

    while (fgets(line, sizeof line, f)) {
        trim(line);

        /* Skip comments and blank lines */
        const char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\0') {
            continue;
        }

        /* Detect start of a new deployment entry */
        val = get_value(line, "name");
        if (val != NULL) {
            if (in_target) {
                /* We hit the next satellite entry; stop */
                break;
            }
            /* Match if either name is a prefix of the other */
            size_t vlen = strlen(val);
            size_t slen = strlen(sat_name);
            size_t cmplen = vlen < slen ? vlen : slen;
            if (strncasecmp(val, sat_name, cmplen) == 0) {
                in_target = 1;
                found = 1;
                snprintf(state->object_name, sizeof state->object_name, "%s", val);
            }
            continue;
        }

        if (!in_target) {
            continue;
        }

        if ((val = get_value(line, "date")) != NULL) {
            char ts[64];
            snprintf(ts, sizeof ts, "%s", val);
            if (utc_parse(ts, &state->epoch) == 0) {
                got_epoch = 1;
            }
        } else if ((val = get_value(line, "r_ecef_m")) != NULL) {
            double r[3];
            if (parse_vec3(val, r) == 0) {
                state->pos[0] = r[0] / 1000.0;
                state->pos[1] = r[1] / 1000.0;
                state->pos[2] = r[2] / 1000.0;
                got_pos = 1;
            }
        } else if ((val = get_value(line, "v_ecef_m_per_s")) != NULL) {
            double v[3];
            if (parse_vec3(val, v) == 0) {
                state->vel[0] = v[0] / 1000.0;
                state->vel[1] = v[1] / 1000.0;
                state->vel[2] = v[2] / 1000.0;
                got_vel = 1;
            }
        } else if ((val = get_value(line, "sequence_number")) != NULL) {
            state->sequence_number = atoi(val);
        } else if ((val = get_value(line, "hard_body_radius_m")) != NULL) {
            state->hard_body_radius = atof(val);
        } else if ((val = get_value(line, "ballistic_coef_kg_per_m2")) != NULL) {
            state->ballistic_coef = atof(val);
        } else if ((val = get_value(line, "mean_perigee_altitude_km")) != NULL) {
            state->mean_perigee_alt_km = atof(val);
            state->has_mean_elements = 1;
        } else if ((val = get_value(line, "mean_apogee_altitude_km")) != NULL) {
            state->mean_apogee_alt_km = atof(val);
        } else if ((val = get_value(line, "mean_inclination_deg")) != NULL) {
            state->mean_inclination_deg = atof(val);
        } else if ((val = get_value(line, "mean_argument_of_perigee_deg")) != NULL) {
            state->mean_arg_perigee_deg = atof(val);
        } else if ((val = get_value(line, "mean_longitude_ascending_node_deg")) != NULL) {
            state->mean_raan_deg = atof(val);
        } else if ((val = get_value(line, "mean_mean_anomaly_deg")) != NULL) {
            state->mean_mean_anomaly_deg = atof(val);
        }
    }

    fclose(f);

    if (!found) {
        fprintf(stderr, "Satellite '%s' not found in %s\n", sat_name, filename);
        return -1;
    }

    if (!got_epoch || !got_pos || !got_vel) {
        fprintf(stderr, "OPM entry for '%s' missing required fields (epoch=%d pos=%d vel=%d)\n",
                sat_name, got_epoch, got_pos, got_vel);
        return -1;
    }

    return 0;
}

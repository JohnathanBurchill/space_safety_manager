/*
    Space Safety Manager  tle.c

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

#include "tle.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EARTH_MU    398600.4418         /* km^3/s^2 */
#define EARTH_OMEGA 7.2921150e-5        /* rad/s */
#define TWO_PI      (2.0 * M_PI)

/* --- OEM parsing --- */

static const char *trim_leading(const char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

/* Extract value for "KEY =" from line. Returns pointer in line or NULL. */
static int oem_get_kv(const char *line, const char *key, char *out, int outlen)
{
    const char *p = trim_leading(line);
    size_t klen = strlen(key);
    if (strncmp(p, key, klen) != 0) {
        return 0;
    }
    p += klen;
    while (*p == ' ' || *p == '=') {
        p++;
    }
    snprintf(out, outlen, "%s", p);
    int n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) {
        n--;
    }
    out[n] = '\0';
    return 1;
}

int tle_parse_oem_first(const char *oem, char *name_out, int name_len,
                        char *id_out, int id_len,
                        utc_time_t *epoch_out,
                        double r_ecef[3], double v_ecef[3])
{
    const char *p = oem;
    int in_data = 0;
    int got_point = 0;

    name_out[0] = '\0';
    id_out[0] = '\0';

    while (*p != '\0') {
        /* Extract one line */
        const char *lend = p;
        while (*lend != '\0' && *lend != '\n') {
            lend++;
        }
        int linelen = (int)(lend - p);
        char line[512];
        if (linelen >= (int)sizeof line) {
            linelen = sizeof line - 1;
        }
        memcpy(line, p, linelen);
        line[linelen] = '\0';

        if (strstr(line, "META_STOP") != NULL) {
            in_data = 1;
        } else if (strstr(line, "COVARIANCE_START") != NULL) {
            /* end of ephemeris data */
            break;
        } else if (!in_data) {
            char buf[256];
            if (oem_get_kv(line, "OBJECT_NAME", buf, sizeof buf)) {
                snprintf(name_out, name_len, "%s", buf);
            } else if (oem_get_kv(line, "OBJECT_ID", buf, sizeof buf)) {
                snprintf(id_out, id_len, "%s", buf);
            }
        } else {
            /* Try to parse an ephemeris line: TIMESTAMP x y z vx vy vz */
            const char *s = trim_leading(line);
            if (*s != '\0' && *s != '#') {
                char ts[64];
                double r[3], v[3];
                int n = sscanf(s, "%63s %lf %lf %lf %lf %lf %lf",
                               ts, &r[0], &r[1], &r[2], &v[0], &v[1], &v[2]);
                if (n == 7) {
                    if (utc_parse(ts, epoch_out) == 0) {
                        memcpy(r_ecef, r, sizeof r);
                        memcpy(v_ecef, v, sizeof v);
                        got_point = 1;
                        break;
                    }
                }
            }
        }

        p = lend;
        if (*p == '\n') {
            p++;
        }
    }

    if (!got_point) {
        fprintf(stderr, "Failed to find first ephemeris point in OEM\n");
        return -1;
    }
    return 0;
}

/* --- Frame conversion --- */

/* Greenwich Mean Sidereal Time (rad) at UTC Julian date (IAU 1982, simplified) */
static double gmst_rad(double jd_ut)
{
    double T = (jd_ut - 2451545.0) / 36525.0;
    double gmst_sec = 67310.54841
                      + (876600.0 * 3600.0 + 8640184.812866) * T
                      + 0.093104 * T * T
                      - 6.2e-6 * T * T * T;
    double gmst = fmod(gmst_sec, 86400.0) * (TWO_PI / 86400.0);
    while (gmst < 0) gmst += TWO_PI;
    while (gmst >= TWO_PI) gmst -= TWO_PI;
    return gmst;
}

/*
 * Convert ECEF (ITRF) state to TEME (approximate, no polar motion, no nutation).
 * Good enough for quick-look TLEs; RAAN may be off by a few arcminutes.
 */
static void ecef_to_teme(const double r_ecef[3], const double v_ecef[3],
                         double gmst,
                         double r_teme[3], double v_teme[3])
{
    double c = cos(gmst);
    double s = sin(gmst);

    r_teme[0] = c * r_ecef[0] - s * r_ecef[1];
    r_teme[1] = s * r_ecef[0] + c * r_ecef[1];
    r_teme[2] = r_ecef[2];

    /* v_teme = R * v_ecef + omega x r_teme */
    double vr_x = c * v_ecef[0] - s * v_ecef[1];
    double vr_y = s * v_ecef[0] + c * v_ecef[1];
    double vr_z = v_ecef[2];

    v_teme[0] = vr_x - EARTH_OMEGA * r_teme[1];
    v_teme[1] = vr_y + EARTH_OMEGA * r_teme[0];
    v_teme[2] = vr_z;
}

/* --- Keplerian elements --- */

static double vec3_dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static double vec3_mag(const double a[3])
{
    return sqrt(vec3_dot(a, a));
}

static void vec3_cross(const double a[3], const double b[3], double out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* Compute classical orbital elements from state vector in an inertial frame */
static int state_to_elements(const double r[3], const double v[3],
                             double *a, double *e, double *inc,
                             double *raan, double *argp, double *ma)
{
    double r_mag = vec3_mag(r);
    double v_mag = vec3_mag(v);

    double h[3];
    vec3_cross(r, v, h);
    double h_mag = vec3_mag(h);

    /* Node vector: n = k_hat x h */
    double n[3] = {-h[1], h[0], 0.0};
    double n_mag = sqrt(n[0] * n[0] + n[1] * n[1]);

    /* Eccentricity vector */
    double v2 = v_mag * v_mag;
    double rv = vec3_dot(r, v);
    double coeff = (v2 - EARTH_MU / r_mag);
    double e_vec[3];
    e_vec[0] = (coeff * r[0] - rv * v[0]) / EARTH_MU;
    e_vec[1] = (coeff * r[1] - rv * v[1]) / EARTH_MU;
    e_vec[2] = (coeff * r[2] - rv * v[2]) / EARTH_MU;
    *e = vec3_mag(e_vec);

    /* Semi-major axis */
    double energy = 0.5 * v2 - EARTH_MU / r_mag;
    if (energy >= 0.0) {
        fprintf(stderr, "Non-elliptical orbit (energy=%g)\n", energy);
        return -1;
    }
    *a = -EARTH_MU / (2.0 * energy);

    /* Inclination */
    *inc = acos(h[2] / h_mag);

    /* RAAN */
    if (n_mag < 1e-10) {
        *raan = 0.0;
    } else {
        *raan = acos(n[0] / n_mag);
        if (n[1] < 0) {
            *raan = TWO_PI - *raan;
        }
    }

    /* Argument of perigee */
    if (*e < 1e-10 || n_mag < 1e-10) {
        *argp = 0.0;
    } else {
        double cos_argp = vec3_dot(n, e_vec) / (n_mag * (*e));
        if (cos_argp > 1.0) cos_argp = 1.0;
        if (cos_argp < -1.0) cos_argp = -1.0;
        *argp = acos(cos_argp);
        if (e_vec[2] < 0) {
            *argp = TWO_PI - *argp;
        }
    }

    /* True anomaly */
    double true_anom;
    if (*e < 1e-10) {
        true_anom = 0.0;
    } else {
        double cos_nu = vec3_dot(e_vec, r) / ((*e) * r_mag);
        if (cos_nu > 1.0) cos_nu = 1.0;
        if (cos_nu < -1.0) cos_nu = -1.0;
        true_anom = acos(cos_nu);
        if (rv < 0) {
            true_anom = TWO_PI - true_anom;
        }
    }

    /* Convert true anomaly -> eccentric anomaly -> mean anomaly */
    double E = 2.0 * atan2(sqrt(1.0 - *e) * sin(true_anom * 0.5),
                           sqrt(1.0 + *e) * cos(true_anom * 0.5));
    *ma = E - (*e) * sin(E);
    while (*ma < 0) *ma += TWO_PI;
    while (*ma >= TWO_PI) *ma -= TWO_PI;

    return 0;
}

/* --- TLE formatting --- */

static double day_of_year(const utc_time_t *t)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int is_leap = (t->year % 4 == 0 && (t->year % 100 != 0 || t->year % 400 == 0));
    int doy = t->day;
    for (int m = 1; m < t->month; m++) {
        doy += days[m - 1];
        if (m == 2 && is_leap) doy += 1;
    }
    double frac = (t->hour + (t->min + t->sec / 60.0) / 60.0) / 24.0;
    return doy + frac;
}

static double utc_to_jd_local(const utc_time_t *t)
{
    int y = t->year, m = t->month;
    double d = t->day + t->hour / 24.0 + t->min / 1440.0 + t->sec / 86400.0;
    if (m <= 2) { y -= 1; m += 12; }
    int A = y / 100;
    int B = 2 - A + A / 4;
    return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + d + B - 1524.5;
}

static int tle_checksum(const char *line)
{
    int sum = 0;
    for (int i = 0; line[i] != '\0' && i < 68; i++) {
        if (line[i] >= '0' && line[i] <= '9') {
            sum += line[i] - '0';
        } else if (line[i] == '-') {
            sum += 1;
        }
    }
    return sum % 10;
}

int tle_write_from_state(const char *object_name, const char *object_id,
                         const utc_time_t *epoch,
                         const double r_ecef[3], const double v_ecef[3],
                         const char *filename)
{
    double jd = utc_to_jd_local(epoch);
    double gmst = gmst_rad(jd);

    double r_teme[3], v_teme[3];
    ecef_to_teme(r_ecef, v_ecef, gmst, r_teme, v_teme);

    double a, e, inc, raan, argp, ma;
    if (state_to_elements(r_teme, v_teme, &a, &e, &inc, &raan, &argp, &ma) != 0) {
        return -1;
    }

    double n_rad_s = sqrt(EARTH_MU / (a * a * a));
    double n_rev_day = n_rad_s * 86400.0 / TWO_PI;

    double inc_deg = inc * 180.0 / M_PI;
    double raan_deg = raan * 180.0 / M_PI;
    double argp_deg = argp * 180.0 / M_PI;
    double ma_deg = ma * 180.0 / M_PI;

    int epoch_year = epoch->year % 100;
    double epoch_doy = day_of_year(epoch);

    int catalog_num = 99999;
    const char *intl_designator = "26999A  ";
    int element_set = 1;
    int rev_num = 0;

    char line1[80];
    snprintf(line1, sizeof line1,
             "1 %05dU %-8s %02d%012.8f  .00000000  00000-0  00000-0 0 %4d",
             catalog_num, intl_designator, epoch_year, epoch_doy, element_set);
    int chk1 = tle_checksum(line1);

    int ecc_int = (int)(e * 10000000.0 + 0.5);
    char line2[80];
    snprintf(line2, sizeof line2,
             "2 %05d %8.4f %8.4f %07d %8.4f %8.4f %11.8f%5d",
             catalog_num, inc_deg, raan_deg, ecc_int, argp_deg, ma_deg, n_rev_day,
             rev_num);
    int chk2 = tle_checksum(line2);

    FILE *f = fopen(filename, "w");
    if (f == NULL) {
        fprintf(stderr, "Cannot write TLE to %s\n", filename);
        return -1;
    }
    fprintf(f, "%s\n", object_name[0] != '\0' ? object_name : "UNKNOWN");
    fprintf(f, "%s%d\n", line1, chk1);
    fprintf(f, "%s%d\n", line2, chk2);
    fclose(f);

    return 0;
}

void tle_default_filename(const char *object_id, const utc_time_t *epoch,
                          char *buf, int len)
{
    const char *id = (object_id != NULL && object_id[0] != '\0') ? object_id : "unknown";
    snprintf(buf, len, "%s-%04d%02d%02dT%02d%02d%02d.tle",
             id, epoch->year, epoch->month, epoch->day,
             epoch->hour, epoch->min, (int)epoch->sec);
}

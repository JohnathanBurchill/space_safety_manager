/*
    Space Safety Manager  util.c

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

#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

double utc_to_jd(const utc_time_t *t)
{
    int y = t->year;
    int m = t->month;
    double d = t->day + t->hour / 24.0 + t->min / 1440.0 + t->sec / 86400.0;

    if (m <= 2) {
        y -= 1;
        m += 12;
    }

    int A = y / 100;
    int B = 2 - A + A / 4;

    return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + d + B - 1524.5;
}

void jd_to_utc(double jd, utc_time_t *t)
{
    double jd_plus = jd + 0.5;
    int Z = (int)jd_plus;
    double F = jd_plus - Z;

    int A;
    if (Z < 2299161) {
        A = Z;
    } else {
        int alpha = (int)((Z - 1867216.25) / 36524.25);
        A = Z + 1 + alpha - alpha / 4;
    }

    int B = A + 1524;
    int C = (int)((B - 122.1) / 365.25);
    int D = (int)(365.25 * C);
    int E = (int)((B - D) / 30.6001);

    double day_frac = B - D - (int)(30.6001 * E) + F;
    t->day = (int)day_frac;
    double time_frac = (day_frac - t->day) * 24.0;
    t->hour = (int)time_frac;
    double min_frac = (time_frac - t->hour) * 60.0;
    t->min = (int)min_frac;
    t->sec = (min_frac - t->min) * 60.0;

    if (E < 14) {
        t->month = E - 1;
    } else {
        t->month = E - 13;
    }

    if (t->month > 2) {
        t->year = C - 4716;
    } else {
        t->year = C - 4715;
    }
}

void utc_format(const utc_time_t *t, char *buf, int len)
{
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%09.6fZ",
             t->year, t->month, t->day, t->hour, t->min, t->sec);
}

int utc_parse(const char *str, utc_time_t *t)
{
    memset(t, 0, sizeof *t);
    int n = sscanf(str, "%d-%d-%dT%d:%d:%lfZ",
                   &t->year, &t->month, &t->day, &t->hour, &t->min, &t->sec);
    if (n < 5) {
        return -1;
    }
    return 0;
}

void utc_now(utc_time_t *t)
{
    time_t now = time(NULL);
    struct tm *gm = gmtime(&now);
    t->year = gm->tm_year + 1900;
    t->month = gm->tm_mon + 1;
    t->day = gm->tm_mday;
    t->hour = gm->tm_hour;
    t->min = gm->tm_min;
    t->sec = (double)gm->tm_sec;
}

int utc_parse_epoch(const char *str, utc_time_t *t)
{
    memset(t, 0, sizeof *t);

    /* Relative: now, now+3h, today+1d-5h+30m-10s, etc. */
    const char *p = NULL;
    if (strncmp(str, "now", 3) == 0 && (str[3] == '\0' || str[3] == '+' || str[3] == '-')) {
        p = str + 3;
    } else if (strncmp(str, "today", 5) == 0) {
        p = str + 5;
    }
    if (p != NULL) {
        utc_now(t);
        double jd = utc_to_jd(t);
        while (*p != '\0') {
            int sign = 1;
            if (*p == '+') {
                sign = 1;
                p++;
            } else if (*p == '-') {
                sign = -1;
                p++;
            } else {
                fprintf(stderr, "epoch: unexpected '%c' in '%s'\n", *p, str);
                return -1;
            }
            char *end;
            double val = strtod(p, &end);
            if (end == p) {
                fprintf(stderr, "epoch: expected number after +/- in '%s'\n", str);
                return -1;
            }
            p = end;
            switch (*p) {
            case 'd': jd += sign * val; p++; break;
            case 'h': jd += sign * val / 24.0; p++; break;
            case 'm': jd += sign * val / 1440.0; p++; break;
            case 's': jd += sign * val / 86400.0; p++; break;
            default:
                fprintf(stderr, "epoch: unknown unit '%c' in '%s' (use d/h/m/s)\n", *p, str);
                return -1;
            }
        }
        jd_to_utc(jd, t);
        return 0;
    }

    /* Absolute: try with colons first (2026-03-30T14:00:00.000Z) */
    int n = sscanf(str, "%d-%d-%dT%d:%d:%lf",
                   &t->year, &t->month, &t->day, &t->hour, &t->min, &t->sec);
    if (n >= 5) {
        return 0;
    }

    /* Try without colons (2026-03-30T140000.000) */
    int hhmm = 0;
    n = sscanf(str, "%d-%d-%dT%d%lf", &t->year, &t->month, &t->day, &hhmm, &t->sec);
    if (n >= 4) {
        t->hour = hhmm / 10000;
        t->min = (hhmm / 100) % 100;
        t->sec += (hhmm % 100);
        return 0;
    }

    fprintf(stderr, "epoch: cannot parse '%s'\n", str);
    return -1;
}

char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(size + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }

    size_t nread = fread(buf, 1, size, f);
    buf[nread] = '\0';
    fclose(f);

    return buf;
}

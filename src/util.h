/*
    Space Safety Manager  util.h

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

#ifndef UTIL_H
#define UTIL_H

typedef struct utc_time {
    int year;
    int month;
    int day;
    int hour;
    int min;
    double sec;
} utc_time_t;

/* Convert UTC calendar to Julian date (days) */
double utc_to_jd(const utc_time_t *t);

/* Convert Julian date to UTC calendar */
void jd_to_utc(double jd, utc_time_t *t);

/* Format as CCSDS timestamp: 2026-03-05T02:09:42.000000Z */
void utc_format(const utc_time_t *t, char *buf, int len);

/* Parse CCSDS timestamp string */
int utc_parse(const char *str, utc_time_t *t);

/* Get current UTC time */
void utc_now(utc_time_t *t);

/*
 * Parse an epoch string: either absolute (2026-03-30T140000.000 or with colons)
 * or relative (today, today+1d-5h+30m, etc.). Units: d h m s.
 * Returns 0 on success, -1 on error.
 */
int utc_parse_epoch(const char *str, utc_time_t *t);

/* Read entire file into malloc'd buffer. Caller frees. Returns NULL on error. */
char *read_file(const char *path);

#endif /* UTIL_H */

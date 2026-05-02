/*
    Space Safety Manager  tle.h

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

#ifndef TLE_H
#define TLE_H

#include "util.h"

/*
 * Parse an OEM text buffer, extracting metadata and the first ephemeris point.
 * Returns 0 on success, -1 on error.
 */
int tle_parse_oem_first(const char *oem, char *name_out, int name_len,
                        char *id_out, int id_len,
                        utc_time_t *epoch_out,
                        double r_ecef[3], double v_ecef[3]);

/*
 * Write a NORAD two-line element set generated from a single ECEF state vector.
 * Performs a GMST-based ECEF -> TEME rotation then computes Keplerian elements.
 * The resulting TLE is approximate (osculating elements used as mean) and
 * suitable for qualitative tracking, not precision orbit determination.
 *
 * Returns 0 on success, -1 on error.
 */
int tle_write_from_state(const char *object_name, const char *object_id,
                         const utc_time_t *epoch,
                         const double r_ecef[3], const double v_ecef[3],
                         const char *filename);

/*
 * Generate the default filename "<id>-<YYYYMMDDTHHMMSS>.tle".
 */
void tle_default_filename(const char *object_id, const utc_time_t *epoch,
                          char *buf, int len);

#endif /* TLE_H */

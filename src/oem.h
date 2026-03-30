/*
    Space Safety Manager  oem.h

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

#ifndef OEM_H
#define OEM_H

#include "opm.h"
#include "propagate.h"

/* Conservative RTN covariance sigmas (matching accepted test upload) */
#define OEM_SIGMA_R     1.0         /* km */
#define OEM_SIGMA_T     10.0        /* km */
#define OEM_SIGMA_N     0.8         /* km */
#define OEM_SIGMA_RDOT  1.0e-4      /* km/s */
#define OEM_SIGMA_TDOT  1.0e-4      /* km/s */
#define OEM_SIGMA_NDOT  1.0e-4      /* km/s */

#define OEM_BUF_SIZE    (512 * 1024)

/*
 * Write CCSDS OEM v3.0 with covariance into buf.
 * meta: OPM metadata (object_name, object_id, ref_frame, etc.)
 * pts: propagated ephemeris points
 * n_pts: number of points
 * Returns number of bytes written, or -1 on error.
 */
int oem_write(const opm_state_t *meta, const ephem_point_t *pts, int n_pts,
              char *buf, int buflen);

/* Convenience: write OEM to file. Returns 0 on success. */
int oem_write_file(const char *filename, const opm_state_t *meta,
                   const ephem_point_t *pts, int n_pts);

#endif /* OEM_H */

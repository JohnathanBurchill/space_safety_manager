/*
    Space Safety Manager  api.h

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

#ifndef API_H
#define API_H

#include <stddef.h>

#define API_MAX_PATH 512
#define API_MAX_URL  1024

typedef struct api_config {
    char cert_path[API_MAX_PATH];
    char key_path[API_MAX_PATH];
    char base_url[API_MAX_URL];
} api_config_t;

/* Response buffer from API calls. Caller frees data. */
typedef struct api_response {
    char *data;
    size_t size;
    long http_code;
} api_response_t;

void api_response_free(api_response_t *resp);

/*
 * All API functions return 0 on success, -1 on error.
 * On success, resp contains the response data. Caller must call api_response_free().
 */
int api_get_operator(const api_config_t *cfg, api_response_t *resp);
int api_list_objects(const api_config_t *cfg, api_response_t *resp);
int api_show_object(const api_config_t *cfg, const char *object_id, api_response_t *resp);
int api_create_object(const api_config_t *cfg, const char *operator_id,
                      const char *object_name, double hard_body_radius,
                      api_response_t *resp);
int api_update_object(const api_config_t *cfg, const char *object_id, int norad_id,
                      const char *operator_id, api_response_t *resp);
int api_upload_trajectory(const api_config_t *cfg, const char *oem_str,
                          const char *object_id, const char *upload_type,
                          api_response_t *resp);
int api_list_trajectories(const api_config_t *cfg, const char *object_id,
                          const char *upload_type, api_response_t *resp);
int api_get_trajectory(const api_config_t *cfg, const char *trajectory_id,
                       api_response_t *resp);
int api_get_trajectory_meta(const api_config_t *cfg, const char *trajectory_id,
                            api_response_t *resp);

#endif /* API_H */

/*
    Space Safety Manager  api.c

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

#include "api.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    api_response_t *resp = (api_response_t *)userp;
    char *ptr = realloc(resp->data, resp->size + total + 1);
    if (ptr == NULL) {
        fprintf(stderr, "Out of memory in API response\n");
        return 0;
    }
    resp->data = ptr;
    memcpy(resp->data + resp->size, contents, total);
    resp->size += total;
    resp->data[resp->size] = '\0';
    return total;
}

void api_response_free(api_response_t *resp)
{
    free(resp->data);
    resp->data = NULL;
    resp->size = 0;
}

static int api_request(const api_config_t *cfg, const char *method, const char *endpoint,
                       const char *body, api_response_t *resp)
{
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    char url[API_MAX_URL];
    int result = -1;

    memset(resp, 0, sizeof *resp);

    snprintf(url, sizeof url, "%s%s", cfg->base_url, endpoint);

    curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "Failed to initialize curl\n");
        goto fail;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_SSLCERT, cfg->cert_path);
    curl_easy_setopt(curl, CURLOPT_SSLKEY, cfg->key_path);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);

    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (strcmp(method, "PATCH") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    } else if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    headers = curl_slist_append(headers, "accept: application/json");
    if (body != NULL) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl error: %s\n", curl_easy_strerror(res));
        goto fail;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->http_code);

    if (resp->http_code >= 400) {
        fprintf(stderr, "HTTP %ld: %s\n", resp->http_code,
                resp->data ? resp->data : "(no body)");
        goto fail;
    }

    result = 0;

fail:
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

int api_get_operator(const api_config_t *cfg, api_response_t *resp)
{
    return api_request(cfg, "GET", "/operator", NULL, resp);
}

int api_list_objects(const api_config_t *cfg, api_response_t *resp)
{
    return api_request(cfg, "GET", "/object", NULL, resp);
}

int api_show_object(const api_config_t *cfg, const char *object_id, api_response_t *resp)
{
    char endpoint[256];
    snprintf(endpoint, sizeof endpoint, "/object/%s", object_id);
    return api_request(cfg, "GET", endpoint, NULL, resp);
}

int api_create_object(const api_config_t *cfg, const char *operator_id,
                      const char *object_name, double hard_body_radius,
                      api_response_t *resp)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "object_name", object_name);
    cJSON_AddStringToObject(json, "operator_id", operator_id);
    cJSON_AddNumberToObject(json, "hard_body_radius", hard_body_radius);
    cJSON_AddBoolToObject(json, "maneuverability", 0);
    cJSON_AddNumberToObject(json, "norad_id", 0);

    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    int result = api_request(cfg, "POST", "/object", body, resp);
    free(body);
    return result;
}

int api_update_object(const api_config_t *cfg, const char *object_id, int norad_id,
                      const char *operator_id, api_response_t *resp)
{
    char endpoint[256];
    snprintf(endpoint, sizeof endpoint, "/object/%s", object_id);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "operator_id", operator_id);
    cJSON_AddNumberToObject(json, "norad_id", norad_id);

    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    int result = api_request(cfg, "PATCH", endpoint, body, resp);
    free(body);
    return result;
}

int api_upload_trajectory(const api_config_t *cfg, const char *oem_str,
                          const char *object_id, const char *upload_type,
                          api_response_t *resp)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "file", oem_str);
    cJSON_AddStringToObject(json, "object_id", object_id);
    cJSON_AddStringToObject(json, "upload_type", upload_type);

    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    int result = api_request(cfg, "POST", "/trajectory", body, resp);
    free(body);
    return result;
}

int api_list_trajectories(const api_config_t *cfg, const char *object_id,
                          const char *upload_type, api_response_t *resp)
{
    char endpoint[512];
    int off = snprintf(endpoint, sizeof endpoint, "/trajectory?object_id=%s", object_id);
    if (upload_type != NULL) {
        snprintf(endpoint + off, sizeof endpoint - off, "&upload_type=%s", upload_type);
    }
    return api_request(cfg, "GET", endpoint, NULL, resp);
}

int api_get_trajectory(const api_config_t *cfg, const char *trajectory_id,
                       api_response_t *resp)
{
    char endpoint[256];
    snprintf(endpoint, sizeof endpoint, "/trajectory/%s", trajectory_id);
    return api_request(cfg, "GET", endpoint, NULL, resp);
}

int api_get_trajectory_meta(const api_config_t *cfg, const char *trajectory_id,
                            api_response_t *resp)
{
    char endpoint[256];
    snprintf(endpoint, sizeof endpoint, "/trajectory/%s/metadata", trajectory_id);
    return api_request(cfg, "GET", endpoint, NULL, resp);
}

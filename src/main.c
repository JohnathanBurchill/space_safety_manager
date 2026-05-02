/*
    Space Safety Manager  main.c

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
#include "config.h"
#include "oem.h"
#include "opm.h"
#include "propagate.h"
#include "state.h"
#include "tle.h"
#include "util.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <dirent.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *progname)
{
    fprintf(stderr,
            "usage: %s [options] <command> [args...]\n"
            "\n"
            "options:\n"
            "  --cert <path>                     client certificate\n"
            "  --key <path>                      client key\n"
            "  --pretty                          human-readable output\n"
            "  -o N                              select object by number\n"
            "  --environment <production|staging> override environment\n"
            "\n"
            "commands:\n"
            "  operator                          list operators\n"
            "  object-show                       list your objects (updates local cache)\n"
            "  object-show all                   list all objects on server\n"
            "  object-create <name> <radius>     create object (name, hard body radius in m)\n"
            "  object-update [--norad <id>]      update active object\n"
            "  propagate <opm-file> [options]    propagate OPM to OEM\n"
            "      --name <satellite>            (default: FrontierSat)\n"
            "      --epoch <time>                override epoch (see below)\n"
            "      --duration <hours>            (default: %.1f)\n"
            "      --step <seconds>              (default: %.0f)\n"
            "      --output <oem-file>           (default: stdout)\n"
            "  upload <oem-file> [--type <t>]    upload OEM trajectory\n"
            "  upload-opm <opm-file> [options]   propagate + upload\n"
            "      --name <satellite>\n"
            "      --epoch <time>\n"
            "      --duration <hours>\n"
            "      --step <seconds>\n"
            "      --type <hypothetical|definitive>\n"
            "  trajectories [--type <t>]         list trajectories for active object\n"
            "  trajectory <id> [--export-tle]    get trajectory OEM; optionally write TLE\n"
            "  trajectory-meta <id>              get trajectory metadata\n"
            "\n"
            "-o N selects object by number (from object-show list).\n"
            "Last used object is remembered across invocations.\n"
            "\n"
            "epoch formats:\n"
            "  now, now+3h, now-30m              relative to current UTC\n"
            "  today+1d-5h+30m                   relative (units: d h m s)\n"
            "  2026-03-30T14:00:00.000           absolute UTC\n",
            progname, SSM_PROP_DURATION_HOURS, SSM_PROP_STEP_SEC);
}

static const char *find_arg(int argc, char **argv, const char *flag, int start)
{
    for (int i = start; i < argc - 1; i++) {
        if (strcmp(argv[i], flag) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static int has_flag(int argc, char **argv, const char *flag, int start)
{
    for (int i = start; i < argc; i++) {
        if (strcmp(argv[i], flag) == 0) {
            return 1;
        }
    }
    return 0;
}

static void trim_input(char *buf)
{
    int n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
        n--;
    }
    buf[n] = '\0';
}

/* Find first file matching *.ext in current directory */
static int find_cred_in_cwd(const char *ext, char *buf, int len)
{
    DIR *d = opendir(".");
    if (d == NULL) {
        return -1;
    }

    struct dirent *ent;
    int found = 0;
    while ((ent = readdir(d)) != NULL) {
        const char *dot = strrchr(ent->d_name, '.');
        if (dot != NULL && strcmp(dot + 1, ext) == 0) {
            char cwd[512];
            if (getcwd(cwd, sizeof cwd) != NULL) {
                snprintf(buf, len, "%s/%s", cwd, ent->d_name);
            } else {
                snprintf(buf, len, "%s", ent->d_name);
            }
            found = 1;
            break;
        }
    }
    closedir(d);
    return found ? 0 : -1;
}

/*
 * Ensure cert and key paths are available. Priority:
 * 1. --cert/--key args (already in cfg)
 * 2. Persisted state
 * 3. Auto-detect in cwd, prompt to confirm
 */
static int ensure_credentials(api_config_t *cfg, int store_keys)
{
    int have_cert = (cfg->cert_path[0] != '\0');
    int have_key = (cfg->key_path[0] != '\0');

    /* If args provided and --store-keys, persist them */
    if (have_cert && have_key && store_keys) {
        state_save_cert(cfg->cert_path);
        state_save_key(cfg->key_path);
        fprintf(stderr, "Credential paths saved for %s environment\n", state_get_env());
        return 0;
    }

    /* If args provided, use them directly */
    if (have_cert && have_key) {
        return 0;
    }

    /* Try loading from state */
    if (!have_cert) {
        have_cert = (state_load_cert(cfg->cert_path, sizeof cfg->cert_path) == 0);
    }
    if (!have_key) {
        have_key = (state_load_key(cfg->key_path, sizeof cfg->key_path) == 0);
    }
    if (have_cert && have_key) {
        return 0;
    }

    /* Auto-detect in cwd */
    char found_cert[512] = {0};
    char found_key[512] = {0};
    int got_cert = (find_cred_in_cwd("crt", found_cert, sizeof found_cert) == 0);
    int got_key = (find_cred_in_cwd("key", found_key, sizeof found_key) == 0);

    if (got_cert && got_key) {
        fprintf(stderr, "Found credentials in current directory:\n");
        fprintf(stderr, "  cert: %s\n", found_cert);
        fprintf(stderr, "  key:  %s\n", found_key);
        fprintf(stderr, "Use these and save for %s? (y/n) [y]: ", state_get_env());
        fflush(stderr);

        char buf[16];
        if (fgets(buf, sizeof buf, stdin) != NULL) {
            trim_input(buf);
            if (buf[0] != '\0' && buf[0] != 'y' && buf[0] != 'Y') {
                fprintf(stderr, "Provide paths with --cert and --key\n");
                return -1;
            }
        }

        snprintf(cfg->cert_path, sizeof cfg->cert_path, "%s", found_cert);
        snprintf(cfg->key_path, sizeof cfg->key_path, "%s", found_key);
        state_save_cert(cfg->cert_path);
        state_save_key(cfg->key_path);
        fprintf(stderr, "Credential paths saved for %s environment\n", state_get_env());
        return 0;
    }

    fprintf(stderr, "No credentials found. Provide --cert and --key paths.\n");
    fprintf(stderr, "Use --store-keys to persist them for this environment.\n");
    return -1;
}

/* Ensure environment is set, prompting on first run */
static int ensure_environment(void)
{
    if (state_get_env() != NULL) {
        return 0;
    }

    fprintf(stderr, "No environment configured.\n");
    fprintf(stderr, "Environment (production/staging) [production]: ");
    fflush(stderr);

    char buf[64];
    if (fgets(buf, sizeof buf, stdin) == NULL) {
        return -1;
    }
    trim_input(buf);

    if (buf[0] == '\0' || buf[0] == 'p' || buf[0] == 'P') {
        state_save_default_env(STATE_ENV_PRODUCTION);
    } else if (buf[0] == 's' || buf[0] == 'S') {
        state_save_default_env(STATE_ENV_STAGING);
    } else {
        fprintf(stderr, "Invalid environment: %s\n", buf);
        return -1;
    }

    fprintf(stderr, "Environment set to %s\n", state_get_env());
    return 0;
}

/* Ensure operator ID is set for current environment, prompting if needed */
static int ensure_operator(char *operator_id, int len)
{
    if (state_load_operator(operator_id, len) == 0) {
        return 0;
    }

    fprintf(stderr, "No operator ID configured for %s.\n", state_get_env());
    fprintf(stderr, "Enter your operator ID (UUID from SpaceX): ");
    fflush(stderr);
    if (fgets(operator_id, len, stdin) == NULL) {
        return -1;
    }
    trim_input(operator_id);

    if (strlen(operator_id) < 10) {
        fprintf(stderr, "Invalid operator ID\n");
        return -1;
    }

    state_save_operator(operator_id);
    fprintf(stderr, "Operator ID saved for %s environment\n", state_get_env());
    return 0;
}

/* Resolve active object ID. Returns 0 on success. */
static int get_active_object_id(char *id_buf, int len)
{
    int active = state_get_active();
    if (active == 0) {
        fprintf(stderr, "No active object. Run 'ssm object-show' then use '-o N' to select.\n");
        return -1;
    }
    return state_resolve_object(active, id_buf, len);
}

/* --- Pretty printing --- */

static void format_cell(const cJSON *item, char *buf, int buflen)
{
    if (cJSON_IsString(item)) {
        snprintf(buf, buflen, "%s", item->valuestring);
    } else if (cJSON_IsNumber(item)) {
        double v = item->valuedouble;
        if (v == (int)v) {
            snprintf(buf, buflen, "%d", (int)v);
        } else {
            snprintf(buf, buflen, "%g", v);
        }
    } else if (cJSON_IsBool(item)) {
        snprintf(buf, buflen, "%s", cJSON_IsTrue(item) ? "yes" : "no");
    } else {
        snprintf(buf, buflen, "-");
    }
}

static void pretty_print_object(const cJSON *obj)
{
    int max_key = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, obj) {
        int len = (int)strlen(item->string);
        if (len > max_key) {
            max_key = len;
        }
    }
    cJSON_ArrayForEach(item, obj) {
        char buf[256];
        format_cell(item, buf, sizeof buf);
        printf("  %-*s  %s\n", max_key, item->string, buf);
    }
}

static void pretty_print_table(const cJSON *arr, const char **fields)
{
    int n_rows = cJSON_GetArraySize(arr);
    if (n_rows == 0) {
        printf("(empty)\n");
        return;
    }

    const cJSON *first = cJSON_GetArrayItem(arr, 0);
    int n_cols = 0;
    const char *col_names[64];

    if (fields != NULL) {
        for (int i = 0; fields[i] != NULL && n_cols < 64; i++) {
            if (cJSON_GetObjectItem(first, fields[i]) != NULL) {
                col_names[n_cols++] = fields[i];
            }
        }
    } else {
        const cJSON *col = NULL;
        cJSON_ArrayForEach(col, first) {
            if (n_cols < 64) {
                col_names[n_cols++] = col->string;
            }
        }
    }

    int col_widths[64];
    for (int c = 0; c < n_cols; c++) {
        col_widths[c] = (int)strlen(col_names[c]);
    }
    for (int r = 0; r < n_rows; r++) {
        const cJSON *row = cJSON_GetArrayItem(arr, r);
        for (int c = 0; c < n_cols; c++) {
            char buf[256];
            const cJSON *item = cJSON_GetObjectItem(row, col_names[c]);
            if (item != NULL) {
                format_cell(item, buf, sizeof buf);
                int len = (int)strlen(buf);
                if (len > col_widths[c]) {
                    col_widths[c] = len;
                }
            }
        }
    }

    for (int c = 0; c < n_cols; c++) {
        if (c > 0) printf("  ");
        printf("%-*s", col_widths[c], col_names[c]);
    }
    printf("\n");

    for (int c = 0; c < n_cols; c++) {
        if (c > 0) printf("  ");
        for (int i = 0; i < col_widths[c]; i++) {
            putchar('-');
        }
    }
    printf("\n");

    for (int r = 0; r < n_rows; r++) {
        const cJSON *row = cJSON_GetArrayItem(arr, r);
        for (int c = 0; c < n_cols; c++) {
            char buf[256];
            const cJSON *item = cJSON_GetObjectItem(row, col_names[c]);
            if (item != NULL) {
                format_cell(item, buf, sizeof buf);
            } else {
                snprintf(buf, sizeof buf, "-");
            }
            if (c > 0) printf("  ");
            printf("%-*s", col_widths[c], buf);
        }
        printf("\n");
    }
}

static void print_json(const char *data)
{
    printf("%s\n", data);
}

static void print_response(const char *data, int pretty)
{
    if (!pretty) {
        print_json(data);
        return;
    }

    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        printf("%s\n", data);
        return;
    }

    if (cJSON_IsArray(json)) {
        pretty_print_table(json, NULL);
    } else if (cJSON_IsObject(json)) {
        cJSON *result = cJSON_GetObjectItem(json, "result");
        if (result != NULL && cJSON_IsArray(result)) {
            static const char *trajectory_fields[] = {
                "id", "upload_type", "screened_status",
                "start_at", "end_at", "uploaded_at", NULL
            };
            pretty_print_table(result, trajectory_fields);
        } else {
            pretty_print_object(json);
        }
    } else {
        print_json(data);
    }

    cJSON_Delete(json);
}

/* --- object-show: list objects with numbered IDs --- */

static cJSON *filter_by_operator(cJSON *arr, const char *operator_id)
{
    cJSON *filtered = cJSON_CreateArray();
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; i++) {
        cJSON *obj = cJSON_GetArrayItem(arr, i);
        cJSON *op = cJSON_GetObjectItem(obj, "operator_id");
        if (op != NULL && cJSON_IsString(op) &&
            strcmp(op->valuestring, operator_id) == 0) {
            cJSON_AddItemToArray(filtered, cJSON_Duplicate(obj, 1));
        }
    }
    return filtered;
}

static int cmd_object_show(const api_config_t *cfg, const char *operator_id,
                           int show_all, int pretty)
{
    api_response_t resp = {0};
    if (api_list_objects(cfg, &resp) != 0) {
        api_response_free(&resp);
        return 1;
    }

    cJSON *all = cJSON_Parse(resp.data);
    api_response_free(&resp);

    if (all == NULL || !cJSON_IsArray(all)) {
        fprintf(stderr, "Unexpected response format\n");
        cJSON_Delete(all);
        return 1;
    }

    /* Filter to our objects unless "all" requested */
    cJSON *arr;
    if (show_all) {
        arr = all;
    } else {
        arr = filter_by_operator(all, operator_id);
        cJSON_Delete(all);
        all = NULL;
    }

    /* Only persist our own objects */
    if (!show_all) {
        char *json = cJSON_PrintUnformatted(arr);
        state_save_objects(json);
        free(json);
    }

    int active = state_get_active();

    if (!pretty) {
        char *json = cJSON_PrintUnformatted(arr);
        printf("%s\n", json);
        free(json);
        cJSON_Delete(arr);
        return 0;
    }

    int n = cJSON_GetArraySize(arr);
    if (n == 0) {
        printf("(no objects)\n");
        cJSON_Delete(arr);
        return 0;
    }

    /* If no active object set yet, default to last in list */
    if (active == 0 && n > 0) {
        active = n;
        state_set_active(active);
    }

    static const char *fields[] = {
        "object_name", "id", "norad_id", "hard_body_radius", "alive", NULL
    };

    /* Measure widths */
    int col_widths[16];
    for (int c = 0; fields[c] != NULL; c++) {
        col_widths[c] = (int)strlen(fields[c]);
    }
    for (int r = 0; r < n; r++) {
        const cJSON *row = cJSON_GetArrayItem(arr, r);
        for (int c = 0; fields[c] != NULL; c++) {
            char buf[256];
            const cJSON *item = cJSON_GetObjectItem(row, fields[c]);
            if (item != NULL) {
                format_cell(item, buf, sizeof buf);
                int len = (int)strlen(buf);
                if (len > col_widths[c]) {
                    col_widths[c] = len;
                }
            }
        }
    }

    /* Reprint header with widths */
    printf("  #  ");
    for (int c = 0; fields[c] != NULL; c++) {
        if (c > 0) printf("  ");
        printf("%-*s", col_widths[c], fields[c]);
    }
    printf("\n");

    printf("  -  ");
    for (int c = 0; fields[c] != NULL; c++) {
        if (c > 0) printf("  ");
        for (int i = 0; i < col_widths[c]; i++) {
            putchar('-');
        }
    }
    printf("\n");

    for (int r = 0; r < n; r++) {
        const cJSON *row = cJSON_GetArrayItem(arr, r);
        char marker = (r + 1 == active) ? '*' : ' ';
        printf("%c %d  ", marker, r + 1);
        for (int c = 0; fields[c] != NULL; c++) {
            char buf[256];
            const cJSON *item = cJSON_GetObjectItem(row, fields[c]);
            if (item != NULL) {
                format_cell(item, buf, sizeof buf);
            } else {
                snprintf(buf, sizeof buf, "-");
            }
            if (c > 0) printf("  ");
            printf("%-*s", col_widths[c], buf);
        }
        printf("\n");
    }

    cJSON_Delete(arr);
    return 0;
}

/* --- Propagation commands --- */

static int cmd_propagate(int argc, char **argv, int cmd_idx, const char *object_id)
{
    if (cmd_idx + 1 >= argc) {
        fprintf(stderr, "propagate: missing OPM file argument\n");
        return 1;
    }
    const char *opm_file = argv[cmd_idx + 1];
    const char *output = find_arg(argc, argv, "--output", cmd_idx);
    const char *dur_str = find_arg(argc, argv, "--duration", cmd_idx);
    const char *step_str = find_arg(argc, argv, "--step", cmd_idx);
    const char *name = find_arg(argc, argv, "--name", cmd_idx);
    const char *epoch_str = find_arg(argc, argv, "--epoch", cmd_idx);

    double duration_hours = dur_str ? atof(dur_str) : SSM_PROP_DURATION_HOURS;
    double step_sec = step_str ? atof(step_str) : SSM_PROP_STEP_SEC;
    double duration_sec = duration_hours * 3600.0;

    opm_state_t opm = {0};
    if (opm_parse(opm_file, name, &opm) != 0) {
        return 1;
    }

    /* Set object ID from state */
    if (object_id != NULL) {
        snprintf(opm.object_id, sizeof opm.object_id, "%s", object_id);
    }

    if (epoch_str != NULL) {
        if (utc_parse_epoch(epoch_str, &opm.epoch) != 0) {
            return 1;
        }
        char ts[64];
        utc_format(&opm.epoch, ts, sizeof ts);
        fprintf(stderr, "Epoch overridden to %s\n", ts);
    }

    int max_pts = (int)(duration_sec / step_sec) + 2;
    ephem_point_t *pts = malloc(max_pts * sizeof *pts);
    if (pts == NULL) {
        fprintf(stderr, "Failed to allocate ephemeris buffer\n");
        return 1;
    }

    int n_pts = 0;
    if (prop_propagate(&opm, duration_sec, step_sec, pts, &n_pts) != 0) {
        free(pts);
        return 1;
    }

    fprintf(stderr, "Propagated %d points over %.1f hours\n", n_pts, duration_hours);

    int result;
    if (output != NULL) {
        result = oem_write_file(output, &opm, pts, n_pts);
        if (result == 0) {
            fprintf(stderr, "Wrote OEM to %s\n", output);
        }
    } else {
        char *buf = malloc(OEM_BUF_SIZE);
        if (buf == NULL) {
            free(pts);
            return 1;
        }
        int len = oem_write(&opm, pts, n_pts, buf, OEM_BUF_SIZE);
        if (len > 0) {
            fwrite(buf, 1, len, stdout);
            result = 0;
        } else {
            result = -1;
        }
        free(buf);
    }

    free(pts);
    return result != 0 ? 1 : 0;
}

static int cmd_upload(const api_config_t *cfg, int argc, char **argv, int cmd_idx,
                      const char *object_id, int pretty)
{
    if (cmd_idx + 1 >= argc) {
        fprintf(stderr, "upload: missing OEM file argument\n");
        return 1;
    }
    const char *oem_file = argv[cmd_idx + 1];
    const char *type = find_arg(argc, argv, "--type", cmd_idx);
    if (type == NULL) {
        type = "hypothetical";
    }

    char *oem_str = read_file(oem_file);
    if (oem_str == NULL) {
        fprintf(stderr, "Error reading OEM file: %s\n", oem_file);
        return 1;
    }

    api_response_t resp = {0};
    int result = api_upload_trajectory(cfg, oem_str, object_id, type, &resp);
    free(oem_str);

    if (result == 0) {
        print_response(resp.data, pretty);
    }
    api_response_free(&resp);
    return result != 0 ? 1 : 0;
}

static int cmd_upload_opm(const api_config_t *cfg, int argc, char **argv, int cmd_idx,
                          const char *object_id, int pretty)
{
    if (cmd_idx + 1 >= argc) {
        fprintf(stderr, "upload-opm: missing OPM file argument\n");
        return 1;
    }
    const char *opm_file = argv[cmd_idx + 1];
    const char *type = find_arg(argc, argv, "--type", cmd_idx);
    const char *dur_str = find_arg(argc, argv, "--duration", cmd_idx);
    const char *step_str = find_arg(argc, argv, "--step", cmd_idx);
    const char *name = find_arg(argc, argv, "--name", cmd_idx);
    const char *epoch_str = find_arg(argc, argv, "--epoch", cmd_idx);

    if (type == NULL) {
        type = "hypothetical";
    }
    double duration_hours = dur_str ? atof(dur_str) : SSM_PROP_DURATION_HOURS;
    double step_sec = step_str ? atof(step_str) : SSM_PROP_STEP_SEC;
    double duration_sec = duration_hours * 3600.0;

    opm_state_t opm = {0};
    if (opm_parse(opm_file, name, &opm) != 0) {
        return 1;
    }

    /* Set object ID from state */
    snprintf(opm.object_id, sizeof opm.object_id, "%s", object_id);

    if (epoch_str != NULL) {
        if (utc_parse_epoch(epoch_str, &opm.epoch) != 0) {
            return 1;
        }
        char ts[64];
        utc_format(&opm.epoch, ts, sizeof ts);
        fprintf(stderr, "Epoch overridden to %s\n", ts);
    }

    int max_pts = (int)(duration_sec / step_sec) + 2;
    ephem_point_t *pts = malloc(max_pts * sizeof *pts);
    if (pts == NULL) {
        fprintf(stderr, "Failed to allocate ephemeris buffer\n");
        return 1;
    }

    int n_pts = 0;
    if (prop_propagate(&opm, duration_sec, step_sec, pts, &n_pts) != 0) {
        free(pts);
        return 1;
    }
    fprintf(stderr, "Propagated %d points over %.1f hours\n", n_pts, duration_hours);

    char *buf = malloc(OEM_BUF_SIZE);
    if (buf == NULL) {
        free(pts);
        return 1;
    }

    int len = oem_write(&opm, pts, n_pts, buf, OEM_BUF_SIZE);
    free(pts);
    if (len < 0) {
        free(buf);
        return 1;
    }

    api_response_t resp = {0};
    int result = api_upload_trajectory(cfg, buf, object_id, type, &resp);
    free(buf);

    if (result == 0) {
        print_response(resp.data, pretty);
    }
    api_response_free(&resp);
    return result != 0 ? 1 : 0;
}

/* --- Main --- */

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    /* First pass: find --environment before state_init */
    const char *env_override = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--environment") == 0 && i + 1 < argc) {
            env_override = argv[i + 1];
            break;
        }
    }

    state_init(env_override);

    api_config_t cfg = {0};

    int pretty = 0;
    int obj_num = -1;
    int store_keys = 0;

    /* Parse global flags and find command index */
    int cmd_idx = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cert") == 0 && i + 1 < argc) {
            snprintf(cfg.cert_path, sizeof cfg.cert_path, "%s", argv[++i]);
        } else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
            snprintf(cfg.key_path, sizeof cfg.key_path, "%s", argv[++i]);
        } else if (strcmp(argv[i], "--pretty") == 0) {
            pretty = 1;
        } else if (strcmp(argv[i], "--store-keys") == 0) {
            store_keys = 1;
        } else if (strcmp(argv[i], "--environment") == 0 && i + 1 < argc) {
            i++; /* already handled above */
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            obj_num = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            cmd_idx = i;
            break;
        }
    }

    if (cmd_idx < 0) {
        usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[cmd_idx];

    /* Ensure environment is set (prompts on first run) */
    if (ensure_environment() != 0) {
        return 1;
    }

    /* Set API base URL from environment */
    if (strcmp(state_get_env(), STATE_ENV_STAGING) == 0) {
        snprintf(cfg.base_url, sizeof cfg.base_url, "%s", SSM_API_BASE_STAGING);
    } else {
        snprintf(cfg.base_url, sizeof cfg.base_url, "%s", SSM_API_BASE_PRODUCTION);
    }

    /* -o must be deferred until after environment is resolved */
    if (obj_num > 0) {
        state_set_active(obj_num);
    }

    /* Ensure credentials are available */
    if (ensure_credentials(&cfg, store_keys) != 0) {
        return 1;
    }

    fprintf(stderr, "[%s]\n", state_get_env());

    curl_global_init(CURL_GLOBAL_DEFAULT);
    api_response_t resp = {0};
    int result = 0;

    /* 'operator' command doesn't need an operator ID */
    if (strcmp(cmd, "operator") == 0) {
        result = api_get_operator(&cfg, &resp);
        if (result == 0) {
            const char *filter = (cmd_idx + 1 < argc) ? argv[cmd_idx + 1] : NULL;
            if (filter != NULL && resp.data != NULL) {
                /* Filter the operator array by regex/substring on name */
                cJSON *arr = cJSON_Parse(resp.data);
                if (arr != NULL && cJSON_IsArray(arr)) {
                    regex_t re;
                    int use_regex = (regcomp(&re, filter, REG_EXTENDED | REG_ICASE) == 0);
                    cJSON *filtered = cJSON_CreateArray();
                    int n = cJSON_GetArraySize(arr);
                    for (int i = 0; i < n; i++) {
                        cJSON *item = cJSON_GetArrayItem(arr, i);
                        cJSON *name_field = cJSON_GetObjectItem(item, "name");
                        if (name_field == NULL || !cJSON_IsString(name_field)) {
                            continue;
                        }
                        int match = 0;
                        if (use_regex) {
                            match = (regexec(&re, name_field->valuestring, 0, NULL, 0) == 0);
                        } else {
                            match = (strcasestr(name_field->valuestring, filter) != NULL);
                        }
                        if (match) {
                            cJSON_AddItemToArray(filtered, cJSON_Duplicate(item, 1));
                        }
                    }
                    if (use_regex) {
                        regfree(&re);
                    }
                    if (pretty) {
                        pretty_print_table(filtered, NULL);
                    } else {
                        char *json = cJSON_PrintUnformatted(filtered);
                        printf("%s\n", json);
                        free(json);
                    }
                    cJSON_Delete(filtered);
                    cJSON_Delete(arr);
                } else {
                    print_response(resp.data, pretty);
                    cJSON_Delete(arr);
                }
            } else {
                print_response(resp.data, pretty);
            }
        }
        api_response_free(&resp);
        curl_global_cleanup();
        return result;
    }

    /* All other commands need operator ID */
    char operator_id[STATE_MAX_ID];
    if (ensure_operator(operator_id, sizeof operator_id) != 0) {
        curl_global_cleanup();
        return 1;
    }

    if (0) {
        /* placeholder for else-if chain below */
    } else if (strcmp(cmd, "object-show") == 0) {
        int show_all = (cmd_idx + 1 < argc && strcmp(argv[cmd_idx + 1], "all") == 0);
        result = cmd_object_show(&cfg, operator_id, show_all, pretty);
    } else if (strcmp(cmd, "object-create") == 0) {
        if (cmd_idx + 2 >= argc) {
            fprintf(stderr, "object-create: need <name> <hard_body_radius_m>\n");
            result = 1;
        } else {
            const char *name = argv[cmd_idx + 1];
            double radius = atof(argv[cmd_idx + 2]);
            result = api_create_object(&cfg, operator_id, name, radius, &resp);
            if (result == 0) {
                print_response(resp.data, pretty);
                fprintf(stderr, "Run 'ssm object-show' to refresh object list.\n");
            }
        }
    } else if (strcmp(cmd, "object-update") == 0) {
        char object_id[STATE_MAX_ID];
        if (get_active_object_id(object_id, sizeof object_id) != 0) {
            result = 1;
        } else {
            const char *norad_str = find_arg(argc, argv, "--norad", cmd_idx);
            int norad_id = norad_str ? atoi(norad_str) : 0;
            result = api_update_object(&cfg, object_id, norad_id, operator_id, &resp);
            if (result == 0) {
                print_response(resp.data, pretty);
            }
        }
    } else if (strcmp(cmd, "propagate") == 0) {
        char object_id[STATE_MAX_ID] = {0};
        get_active_object_id(object_id, sizeof object_id);
        result = cmd_propagate(argc, argv, cmd_idx, object_id[0] ? object_id : NULL);
    } else if (strcmp(cmd, "upload") == 0) {
        char object_id[STATE_MAX_ID];
        if (get_active_object_id(object_id, sizeof object_id) != 0) {
            result = 1;
        } else {
            result = cmd_upload(&cfg, argc, argv, cmd_idx, object_id, pretty);
        }
    } else if (strcmp(cmd, "upload-opm") == 0) {
        char object_id[STATE_MAX_ID];
        if (get_active_object_id(object_id, sizeof object_id) != 0) {
            result = 1;
        } else {
            result = cmd_upload_opm(&cfg, argc, argv, cmd_idx, object_id, pretty);
        }
    } else if (strcmp(cmd, "trajectories") == 0) {
        char object_id[STATE_MAX_ID];
        if (get_active_object_id(object_id, sizeof object_id) != 0) {
            result = 1;
        } else {
            const char *type = find_arg(argc, argv, "--type", cmd_idx);
            result = api_list_trajectories(&cfg, object_id, type, &resp);
            if (result == 0) {
                print_response(resp.data, pretty);
            }
        }
    } else if (strcmp(cmd, "trajectory") == 0) {
        if (cmd_idx + 1 >= argc) {
            fprintf(stderr, "trajectory: missing trajectory ID\n");
            result = 1;
        } else {
            const char *traj_id = argv[cmd_idx + 1];
            int export_tle = has_flag(argc, argv, "--export-tle", cmd_idx);
            result = api_get_trajectory(&cfg, traj_id, &resp);
            if (result == 0) {
                if (export_tle) {
                    char name[128], id[64];
                    utc_time_t epoch = {0};
                    double r[3], v[3];
                    if (tle_parse_oem_first(resp.data, name, sizeof name,
                                            id, sizeof id, &epoch, r, v) == 0) {
                        char path[512];
                        tle_default_filename(id, &epoch, path, sizeof path);
                        if (tle_write_from_state(name, id, &epoch, r, v, path) == 0) {
                            fprintf(stderr, "Wrote TLE to %s\n", path);
                        }
                    }
                } else {
                    print_response(resp.data, pretty);
                }
            }
        }
    } else if (strcmp(cmd, "trajectory-meta") == 0) {
        if (cmd_idx + 1 >= argc) {
            fprintf(stderr, "trajectory-meta: missing trajectory ID\n");
            result = 1;
        } else {
            result = api_get_trajectory_meta(&cfg, argv[cmd_idx + 1], &resp);
            if (result == 0) {
                print_response(resp.data, pretty);
            }
        }
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        usage(argv[0]);
        result = 1;
    }

    api_response_free(&resp);
    curl_global_cleanup();
    return result;
}

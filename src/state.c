/*
    Space Safety Manager  state.c

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

#include "state.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char base_dir[512];   /* ~/.local/state/ssm */
static char env_dir[512];    /* ~/.local/state/ssm/{production|staging} */
static char current_env[32]; /* "production" or "staging" */

static void ensure_dir(const char *path)
{
    mkdir(path, 0755);
}

static int read_file_trim(const char *path, char *buf, int len)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return -1;
    }
    char *r = fgets(buf, len, f);
    fclose(f);
    if (r == NULL) {
        return -1;
    }
    int n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
        n--;
    }
    buf[n] = '\0';
    return n > 0 ? 0 : -1;
}

static int write_file(const char *path, const char *data)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "Cannot write %s\n", path);
        return -1;
    }
    fprintf(f, "%s\n", data);
    fclose(f);
    return 0;
}

static void env_path(char *buf, int len, const char *filename)
{
    snprintf(buf, len, "%s/%s", env_dir, filename);
}

static void base_path(char *buf, int len, const char *filename)
{
    snprintf(buf, len, "%s/%s", base_dir, filename);
}

int state_init(const char *env)
{
    const char *home = getenv("HOME");
    if (home == NULL) {
        home = "/tmp";
    }

    snprintf(base_dir, sizeof base_dir, "%s/.local/state/ssm", home);

    /* Create directory chain */
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s/.local", home);
    ensure_dir(tmp);
    snprintf(tmp, sizeof tmp, "%s/.local/state", home);
    ensure_dir(tmp);
    ensure_dir(base_dir);

    /* Determine environment */
    if (env != NULL) {
        snprintf(current_env, sizeof current_env, "%s", env);
    } else {
        /* Load persisted default */
        char path[512];
        base_path(path, sizeof path, "environment");
        if (read_file_trim(path, current_env, sizeof current_env) != 0) {
            /* No default yet — will be set during first-run */
            current_env[0] = '\0';
        }
    }

    if (current_env[0] != '\0') {
        snprintf(env_dir, sizeof env_dir, "%s/%s", base_dir, current_env);
        ensure_dir(env_dir);
    }

    return 0;
}

const char *state_get_env(void)
{
    return current_env[0] != '\0' ? current_env : NULL;
}

int state_save_default_env(const char *env)
{
    snprintf(current_env, sizeof current_env, "%s", env);
    snprintf(env_dir, sizeof env_dir, "%s/%s", base_dir, current_env);
    ensure_dir(env_dir);

    char path[512];
    base_path(path, sizeof path, "environment");
    return write_file(path, env);
}

int state_load_operator(char *buf, int len)
{
    char path[512];
    env_path(path, sizeof path, "operator");
    return read_file_trim(path, buf, len);
}

int state_save_operator(const char *id)
{
    char path[512];
    env_path(path, sizeof path, "operator");
    return write_file(path, id);
}

int state_load_cert(char *buf, int len)
{
    char path[512];
    env_path(path, sizeof path, "cert_path");
    return read_file_trim(path, buf, len);
}

int state_save_cert(const char *p)
{
    char path[512];
    env_path(path, sizeof path, "cert_path");
    return write_file(path, p);
}

int state_load_key(char *buf, int len)
{
    char path[512];
    env_path(path, sizeof path, "key_path");
    return read_file_trim(path, buf, len);
}

int state_save_key(const char *p)
{
    char path[512];
    env_path(path, sizeof path, "key_path");
    return write_file(path, p);
}

char *state_load_objects(void)
{
    char path[512];
    env_path(path, sizeof path, "objects.json");
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

int state_save_objects(const char *json)
{
    char path[512];
    env_path(path, sizeof path, "objects.json");
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "Cannot write %s\n", path);
        return -1;
    }
    fputs(json, f);
    fclose(f);
    return 0;
}

int state_get_active(void)
{
    char path[512];
    char buf[32];
    env_path(path, sizeof path, "active");
    if (read_file_trim(path, buf, sizeof buf) != 0) {
        return 0;
    }
    int n = atoi(buf);
    return n > 0 ? n : 0;
}

int state_set_active(int n)
{
    char path[512];
    char buf[32];
    env_path(path, sizeof path, "active");
    snprintf(buf, sizeof buf, "%d", n);
    return write_file(path, buf);
}

int state_resolve_object(int n, char *id_buf, int len)
{
    char *json = state_load_objects();
    if (json == NULL) {
        fprintf(stderr, "No objects cached. Run 'ssm object-show' first.\n");
        return -1;
    }

    cJSON *arr = cJSON_Parse(json);
    free(json);
    if (arr == NULL || !cJSON_IsArray(arr)) {
        fprintf(stderr, "Corrupted objects cache. Run 'ssm object-show' to refresh.\n");
        cJSON_Delete(arr);
        return -1;
    }

    int count = cJSON_GetArraySize(arr);
    if (n < 1 || n > count) {
        fprintf(stderr, "Object %d out of range (have %d). Run 'ssm object-show' to list.\n",
                n, count);
        cJSON_Delete(arr);
        return -1;
    }

    const cJSON *obj = cJSON_GetArrayItem(arr, n - 1);
    const cJSON *id = cJSON_GetObjectItem(obj, "id");
    if (id == NULL || !cJSON_IsString(id)) {
        cJSON_Delete(arr);
        return -1;
    }

    snprintf(id_buf, len, "%s", id->valuestring);
    cJSON_Delete(arr);
    return 0;
}

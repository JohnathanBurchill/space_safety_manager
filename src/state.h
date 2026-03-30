/*
    Space Safety Manager  state.h

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

#ifndef STATE_H
#define STATE_H

#define STATE_MAX_ID 64

#define STATE_ENV_PRODUCTION "production"
#define STATE_ENV_STAGING    "staging"

/*
 * Initialize state directory and set active environment.
 * env may be NULL to load the persisted default.
 * Returns 0 on success.
 */
int state_init(const char *env);

/* Get the current environment name ("production" or "staging") */
const char *state_get_env(void);

/* Persist the default environment */
int state_save_default_env(const char *env);

/* Operator ID (per-environment) */
int state_load_operator(char *buf, int len);
int state_save_operator(const char *id);

/* Cert/key paths (per-environment) */
int state_load_cert(char *buf, int len);
int state_save_cert(const char *path);
int state_load_key(char *buf, int len);
int state_save_key(const char *path);

/* Objects: stored as raw JSON array (per-environment) */
char *state_load_objects(void);
int state_save_objects(const char *json);

/* Active object number, 1-based (per-environment). Returns 0 if not set. */
int state_get_active(void);
int state_set_active(int n);

/* Resolve object number to UUID from cached objects. Returns 0 on success. */
int state_resolve_object(int n, char *id_buf, int len);

#endif /* STATE_H */

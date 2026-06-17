# ssm - Space Safety Manager

A command-line tool for managing satellite registrations and ephemeris uploads
to the [SpaceX Space Safety](https://docs.space-safety.starlink.com/) conjunction
screening API. Built for the FrontierSat (CTS-Sat-1) cubesat mission at the
University of Calgary.

## Features

- mTLS-authenticated REST client for the Space Safety API
- Operator and object management with persistent local state
- OPM-to-OEM orbit propagation (J2-perturbed RK4) for trajectory upload
- CCSDS OEM v3.0 output with RTN covariance
- Staging and production environment support
- JSON and human-readable table output

## Building

Requires CMake 3.20+, a C99 compiler, and libcurl (ships with macOS).

```
cmake -B build
cmake --build build
cmake --install build   # installs to ~/bin
```

## Quick Start

You need your mTLS client certificate and key files from SpaceX. Place them in
a working directory and run `ssm` from there.

### 1. Set up environment and discover your operator ID

```
$ cd /path/to/directory/with/cert/and/key
$ ssm --pretty operator
Environment (production/staging) [production]: production
Found credentials in current directory:
  cert: /path/to/client.crt
  key:  /path/to/client.key
Use these and save for production? (y/n) [y]: y
```

This lists all operators on the server. Find your entry by name (case-insensitive
substring or regex):

```
$ ssm --pretty operator Calgary
```

### 2. Register your operator ID

The next command that requires an operator ID will prompt for it:

```
$ ssm --pretty object-show
No operator ID configured for production.
Enter your operator ID (UUID from SpaceX): 925d8acb-...
```

### 3. Create a satellite object

```
$ ssm --pretty object-create "FrontierSat (CTS-Sat-1)" 0.71
```

The second argument is the hard body radius in meters.

### 4. Refresh and select your object

```
$ ssm --pretty object-show
  #  object_name              id                                    norad_id  hard_body_radius  alive
  -  -----------              --                                    --------  ----------------  -----
* 1  FrontierSat (CTS-Sat-1)  xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx  0         0.71              yes
```

The `*` marks the active object. Use `-o N` to switch if you have multiple objects.

### 5. Upload a hypothetical trajectory from an OPM

Preview the OEM output first:

```
$ ssm propagate deployment.opm | head -20
```

Upload (uses `--type hypothetical` by default):

```
$ ssm --pretty upload-opm deployment.opm --epoch now+1h --type hypothetical
```

The `--epoch` flag overrides the deployment epoch for testing when the real
epoch is too far in the future (API limit: 504 hours).

### 6. Check screening status

```
$ ssm --pretty trajectories
$ ssm --pretty trajectory-meta <trajectory-id>
```

### 7. Launch day

Upload definitive ephemeris from GPS-derived state within L+3 hours:

```
$ ssm --pretty upload-opm gps_state.opm --type definitive
```

After USSPACECOM catalogs the satellite:

```
$ ssm object-update --norad <catalog-number>
```

## OPM File Format

Lines starting with `#` are comments. Multi-satellite files are supported;
use `--name <satellite>` to select a specific entry (defaults to `FrontierSat`).

Position is ECEF in meters, velocity in m/s. The tool converts to km and km/s
internally and outputs in the ITRF reference frame.

Two optional keys carry the state-vector uncertainty (per-axis ECEF 1-sigma):

```
  r_ecef_sigma_m: [2.36, 2.34, 2.50]
  v_ecef_sigma_m_per_s: [0.057, 0.058, 0.057]
```

When both are present the OEM covariance is built from them (see Orbit
Propagation); when absent the fixed conservative covariance is used. The
`gnss_opm` tool in the simple_sat_ops project writes this form straight from a
satellite GNSS (BESTXYZA) fix.

## Environment and State

All configuration is stored in `~/.local/state/ssm/` with per-environment
subdirectories:

```
~/.local/state/ssm/
  environment           # default environment (production or staging)
  production/
    operator            # operator UUID
    cert_path           # absolute path to client certificate
    key_path            # absolute path to client key
    objects.json        # cached object list
    active              # active object number
  staging/
    ...
```

Switch environments for a single command with `--environment staging`.

Credential paths are auto-detected on first run from the current directory,
or set explicitly:

```
$ ssm --cert /path/to/client.crt --key /path/to/client.key --store-keys operator
```

## Orbit Propagation

The propagator uses a 4th-order Runge-Kutta integrator with:

- Two-body gravity (mu = 398600.4418 km^3/s^2)
- J2 zonal harmonic (J2 = 1.08263e-3, R_e = 6378.137 km)
- 10-second internal time steps, output at configurable intervals (default 60s)

No atmospheric drag is modelled. This is conservative for conjunction screening:
the real satellite will be at lower altitude than predicted, so any conjunction
flags from SpaceX err on the side of caution.

Covariance: when the OPM supplies per-axis ECEF 1-sigma (`r_ecef_sigma_m` /
`v_ecef_sigma_m_per_s`), each epoch's RTN covariance is built from it — the
diagonal ECEF covariance is grown over the time since epoch with a free-drift
state transition (position variance gains dt^2 * sigma_vel^2, plus a
dt * sigma_vel^2 position-velocity correlation) and then rotated ECEF->RTN,
which turns the per-axis sigmas into the correlated R/T/N matrix. The free-drift
growth ignores gravity-gradient coupling and Earth rotation: a first-order,
conservative approximation. When the OPM carries no uncertainty, a fixed
diagonal RTN matrix is used instead, matching the values accepted by the API in
testing.

## Usage Reference

```
ssm [options] <command> [args...]

options:
  --cert <path>                      client certificate
  --key <path>                       client key
  --pretty                           human-readable output
  -o N                               select object by number
  --environment <production|staging> override environment
  --store-keys                       persist --cert/--key paths

commands:
  operator [<filter>]                 list operators (filter by name)
  object-show                        list your objects
  object-show all                    list all objects on server
  object-create <name> <radius>      create object
  object-update [--norad <id>]       update active object
  propagate <opm-file> [options]     propagate OPM to OEM
      --name <satellite>             satellite name in OPM file
      --epoch <time>                 override epoch
      --duration <hours>             propagation window (default: 3.0)
      --step <seconds>               output step (default: 60)
      --output <oem-file>            write to file instead of stdout
  upload <oem-file> [--type <t>]     upload OEM trajectory
  upload-opm <opm-file> [options]    propagate + upload in one step
      --type <hypothetical|definitive>
  trajectories [--type <t>]          list trajectories for active object
  trajectory <id>                    get trajectory OEM data
  trajectory-meta <id>               get trajectory metadata

epoch formats:
  now, now+3h, now-30m               relative to current UTC
  today+1d-5h+30m                    relative (units: d h m s)
  2026-03-30T14:00:00.000            absolute UTC
```

## Third-Party Software

- **cJSON** by Dave Gamble and contributors (MIT License) -
  vendored in `third_party/`. https://github.com/DaveGamble/cJSON
- **libcurl** (MIT/X derivative License) -
  system library. https://curl.se/

## License

Copyright (C) 2026 Johnathan K Burchill

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

See [LICENSE](LICENSE) for the full text.

"""Sun2Set.py — a Tkinter sunrise / sunset almanac.

Enter a location (latitude / longitude), choose how the time zone should be
handled, and Sun2Set computes the local sunrise time, sunset time, their
azimuths and the day length for today and every day for a year ahead
(366 rows by default).

Features:
  * NOAA solar-position equations (Jean Meeus, "Astronomical Algorithms"):
    typically within a minute or two of published almanac values
  * Zenith 90.833 deg — includes atmospheric refraction + the solar disc
    radius, so "sunrise" is the moment the sun's upper edge appears
  * Time zone: either the system zone (daylight saving applied per day from
    the OS rules) or a fixed UTC offset you type in (e.g. 10, -3.5, 9:30) —
    optionally with manual daylight-saving rules ("1st Sun of Oct" to
    "1st Sun of Apr" at a second offset), which can mirror the system zone
    exactly for any location's local law
  * Sunrise / sunset azimuths — where on the horizon the sun actually
    appears and disappears (degrees clockwise from TRUE north, E=90),
    plus the matching MAGNETIC compass bearings: the embedded World
    Magnetic Model (WMM2025) supplies the local declination for each
    row's own date, so the compass numbers stay current as the magnetic
    pole drifts (true_to_magnetic / magnetic_to_true do one-off
    conversions)
  * Polar day / polar night handled ('--:--:--' with 24 h / 0 h daylight)
  * Valley mode: an optional skyline — hills h degrees above the true
    horizon, as one uniform number or an az:alt profile (60:2, 90:6, ...) —
    delays sunrise and advances sunset (Bennett refraction at the hill
    altitude); days the sun never clears the ridge show '--:--:--'
  * Results become a commented text table; the assumptions (location,
    latitude, longitude, time-zone handling, algorithm) head the file
  * Graph of sunrise, sunset and day length across the whole span, with a
    hover crosshair that reads out exact values per day
  * Save the table anywhere / Load a previously saved table to re-display it
  * Rounded buttons and a dark / light theme toggle (choice persists)
  * Every calculation is also autosaved to %APPDATA%\\Sun2Set\\sun2set_latest.txt
  * Config persistence (window position + last-used parameters)
    in %APPDATA%\\Sun2Set

Run:  python Sun2Set.py          Self-test (headless):  python Sun2Set.py --selftest
"""

import datetime as dt
import functools
import json
import math
import os
import re
import sys
import time
import tkinter as tk
from tkinter import filedialog, font as tkfont

# ----------------------------------------------------------------------------
# Look & feel. Every color the GUI uses lives in one of two selectable
# themes (the choice persists in config.json); the dark palette matches the
# other apps in this repo. FONT is the house font — monospace, resolved per
# platform: "Consolas" only ships with Windows, and an unknown family makes
# Tk substitute the *proportional* system font (ragged table columns, taller
# panel rows).
# ----------------------------------------------------------------------------
FONT = {"win32": "Consolas", "darwin": "Menlo"}.get(sys.platform,
                                                    "DejaVu Sans Mono")

THEMES = {
    "dark": dict(
        bg="#0b0b12", panel="#12121c", edge="#2a2a3a",
        btn="#1d1d2c", btn_edge="#3a3a52", btn_hover="#2a2a3e",
        text="#e6e6ec", sub="#9a9ab0", accent="#ffd91a",
        primary="#ffd91a", primary_fg="#101018", primary_hover="#ffe763",
        disabled="#565670", err="#ff6a6a", ok="#2ecc55",
        band="#26210f", grid="#20263a", axis="#3a3a52",
        rise="#ffd91a", set="#ff6a3d", day="#2ecc55",
        hover="#4a5068", readout="#161622",
    ),
    "light": dict(
        bg="#e9ecf2", panel="#f7f8fb", edge="#c3c8d6",
        btn="#e0e4ee", btn_edge="#b2b9cc", btn_hover="#d2d7e5",
        text="#20222e", sub="#5b6072", accent="#9a6a00",
        primary="#ffd91a", primary_fg="#101018", primary_hover="#ffe763",
        disabled="#a3a8b8", err="#c62828", ok="#177a3e",
        band="#efe5bd", grid="#d7dae5", axis="#9aa0b4",
        rise="#d99a00", set="#e05320", day="#159048",
        hover="#8a90a8", readout="#ffffff",
    ),
}

GRAPH_W, GRAPH_H = 840, 700   # plot canvas size
PANEL_W = 312                 # parameter panel width

DEFAULT_LOCATION = "Sydney, Australia"
DEFAULT_LAT, DEFAULT_LON = -33.8688, 151.2093
DEFAULT_DAYS = 366            # today + one full year (inclusive of both ends)

# Sun center 50 arcmin below the geometric horizon at rise/set:
# 34' standard atmospheric refraction + 16' solar disc radius.
ZENITH_OFFICIAL = 90.833

# The full disc spans two of those 16' radii. Between "upper limb first
# touches the horizon line" and "lower limb clears it" the center truly
# climbs exactly one diameter: both endpoints put a limb on the *same*
# line, so refraction lifts both equally and cancels out of the difference.
SUN_DIAMETER = 32.0 / 60.0


# ----------------------------------------------------------------------------
# Solar mathematics (NOAA / Meeus). Pure functions — no Tk, fully testable.
# ----------------------------------------------------------------------------
def _julian_day(year, month, day):
    """Julian Day at 00:00 UT for a Gregorian calendar date (Meeus ch. 7)."""
    if month <= 2:
        year -= 1
        month += 12
    a = year // 100
    b = 2 - a + a // 4
    return int(365.25 * (year + 4716)) + int(30.6001 * (month + 1)) + day + b - 1524.5


def _solar_coords(jd):
    """Sun declination (deg) and equation of time (minutes) at Julian Day jd.

    The equation of time is how far a sundial runs ahead of the clock; it is
    what makes solar noon drift +/-16 min around 12:00 across the year.
    """
    T = (jd - 2451545.0) / 36525.0          # Julian centuries since J2000.0
    L0 = (280.46646 + T * (36000.76983 + T * 0.0003032)) % 360.0
    M = 357.52911 + T * (35999.05029 - 0.0001537 * T)
    e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T)
    Mr = math.radians(M)
    C = (math.sin(Mr) * (1.914602 - T * (0.004817 + 0.000014 * T))
         + math.sin(2 * Mr) * (0.019993 - 0.000101 * T)
         + math.sin(3 * Mr) * 0.000289)
    omega = math.radians(125.04 - 1934.136 * T)
    lam = L0 + C - 0.00569 - 0.00478 * math.sin(omega)     # apparent longitude
    eps = (23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813)))
                   / 60.0) / 60.0) + 0.00256 * math.cos(omega)
    decl = math.degrees(math.asin(math.sin(math.radians(eps))
                                  * math.sin(math.radians(lam))))
    y = math.tan(math.radians(eps / 2.0)) ** 2
    L0r = math.radians(L0)
    eot = 4.0 * math.degrees(
        y * math.sin(2.0 * L0r)
        - 2.0 * e * math.sin(Mr)
        + 4.0 * e * y * math.sin(Mr) * math.cos(2.0 * L0r)
        - 0.5 * y * y * math.sin(4.0 * L0r)
        - 1.25 * e * e * math.sin(2.0 * Mr))
    return decl, eot


def _hour_angle_deg(lat, decl, zenith=ZENITH_OFFICIAL):
    """Half the daylight arc, in degrees (1 deg = 4 minutes of time).

    Returns (angle, "") on a normal day, or (None, "night"/"day") when the
    sun stays below / above the horizon all day (polar night / midnight sun).
    """
    latr, dr = math.radians(lat), math.radians(decl)
    cos_h = ((math.cos(math.radians(zenith)) - math.sin(latr) * math.sin(dr))
             / (math.cos(latr) * math.cos(dr)))
    if cos_h > 1.0:
        return None, "night"
    if cos_h < -1.0:
        return None, "day"
    return math.degrees(math.acos(cos_h)), ""


# ----------------------------------------------------------------------------
# Skyline (raised horizon) support — for valleys, hills, mountain ridges.
# A profile is a list of (azimuth, altitude) pairs in degrees; [] = flat.
# ----------------------------------------------------------------------------
def _zenith_for_horizon(h):
    """Zenith angle at which the sun's upper limb crosses a skyline h deg
    above the astronomical horizon: 16' solar semi-diameter plus Bennett's
    altitude-dependent refraction (34' at h=0, ~5' at h=10 — a fixed 34'
    would overshoot raised horizons by minutes of time).
    """
    if h == 0.0:
        return ZENITH_OFFICIAL              # keep the NOAA-standard constant
    hc = max(h, -1.5)                       # keep Bennett's formula sane
    refr = 1.0 / math.tan(math.radians(hc + 7.31 / (hc + 4.4)))   # arcmin
    return 90.0 - h + (refr + 16.0) / 60.0


def _sun_azimuth(lat, decl, ha_deg):
    """Sun azimuth (deg from north, clockwise; E=90) at hour angle ha_deg
    (negative = morning, east of the meridian)."""
    latr, dr, hr = (math.radians(v) for v in (lat, decl, ha_deg))
    sin_alt = (math.sin(latr) * math.sin(dr)
               + math.cos(latr) * math.cos(dr) * math.cos(hr))
    cos_alt = math.sqrt(max(1e-9, 1.0 - sin_alt * sin_alt))
    cos_az = ((math.sin(dr) - sin_alt * math.sin(latr))
              / max(1e-9, cos_alt * math.cos(latr)))
    az = math.degrees(math.acos(max(-1.0, min(1.0, cos_az))))
    return 360.0 - az if math.sin(hr) > 0 else az


def parse_horizon(s):
    """Skyline entry -> profile. '' / '0' -> [] (flat); a single number ->
    uniform hills; 'az:alt, az:alt, ...' -> a profile (N=0, E=90)."""
    s = _clean_numeric(s)
    if not s:
        return []
    parts = [p.strip() for p in s.split(",") if p.strip()]
    profile = []
    for part in parts:
        if ":" in part:
            az_s, alt_s = part.split(":", 1)
            az, alt = float(az_s), float(alt_s)
            if not 0.0 <= az <= 360.0:
                raise ValueError("azimuth out of range")
        elif len(parts) == 1:
            az, alt = 0.0, float(part)
        else:
            raise ValueError("lists must use az:alt pairs")
        if not -5.0 <= alt <= 60.0:
            raise ValueError("altitude out of range")
        profile.append((az % 360.0, alt))
    if len(profile) == 1 and profile[0][1] == 0.0:
        return []
    profile.sort()
    return profile


def horizon_alt(profile, az):
    """Skyline altitude toward azimuth az — linear interpolation between the
    profile points, wrapping around north (360 -> 0)."""
    if not profile:
        return 0.0
    if len(profile) == 1:
        return profile[0][1]
    az %= 360.0
    lo, hi = profile[-1], profile[0]        # the wrap-around segment
    for i in range(len(profile) - 1):
        if profile[i][0] <= az <= profile[i + 1][0]:
            lo, hi = profile[i], profile[i + 1]
            break
    span = (hi[0] - lo[0]) % 360.0
    if span == 0.0:
        return lo[1]
    frac = ((az - lo[0]) % 360.0) / span
    return lo[1] + (hi[1] - lo[1]) * frac


def sun_events(date, lat, lon, tz_hours, horizon=None):
    """Sunrise / solar noon / sunset for one local calendar date.

    lat, lon in degrees (+N, +E); tz_hours = local clock offset from UTC;
    horizon = a parse_horizon() skyline profile (None or [] = flat sea-level
    horizon). Returns dict with 'rise', 'noon', 'set' as minutes after local
    midnight (rise/set None when the sun never crosses the skyline),
    'rise_az' / 'set_az' = the sun's azimuth at those moments (degrees
    clockwise from true north, E=90; None with the event), 'rise_dur' /
    'set_dur' = how long the disc takes to cross the line, in minutes
    (upper-limb touch to lower-limb clear; None with the event, and on
    grazing days when the disc peeks over but never fully clears), 'polar'
    in {'', 'night', 'day'} and 'daylight' in minutes.

    Each event iterates to its own fixed point: declination and the equation
    of time are re-evaluated at the event's estimated time (this is what
    matches the NOAA reference calculator to seconds), and with a skyline
    the sun's azimuth there picks the hill altitude, which moves the event,
    which moves the azimuth... Three passes converge well below a second.
    A jagged profile is met at one crossing — this models a smooth skyline,
    not multiple rises through notches.
    """
    jd0 = _julian_day(date.year, date.month, date.day)

    def jd_at(minutes_local):
        return jd0 + (minutes_local - tz_hours * 60.0) / 1440.0

    def clock_noon(eot):
        return 720.0 - 4.0 * lon + tz_hours * 60.0 - eot

    noon = 720.0
    for _ in range(2):                       # equation of time varies slowly
        _, eot = _solar_coords(jd_at(noon))
        noon = clock_noon(eot)

    out = {"rise": None, "rise_az": None, "rise_dur": None, "noon": noon,
           "set": None, "set_az": None, "set_dur": None,
           "polar": "", "daylight": 0.0}
    decl0, eot0 = _solar_coords(jd_at(noon))

    def solve(sign, dz):
        """One crossing (sign -1 = morning, +1 = evening) with the sun's
        center dz degrees above the upper-limb zenith. Returns (time,
        azimuth, '') converged, or (None, None, flag) when the sun never
        reaches / never leaves that line this day."""
        decl, eot = decl0, eot0              # start from the noon position
        h = horizon_alt(horizon, 90.0 if sign < 0 else 270.0)
        t = az = None
        for _ in range(3):
            ha, flag = _hour_angle_deg(lat, decl,
                                       _zenith_for_horizon(h) - dz)
            if ha is None:
                return None, None, flag
            az = _sun_azimuth(lat, decl, sign * ha)
            h = horizon_alt(horizon, az)
            t = clock_noon(eot) + sign * 4.0 * ha
            decl, eot = _solar_coords(jd_at(t))
        return t, az, ""

    flags = []
    for key, sign in (("rise", -1.0), ("set", 1.0)):
        t, az, flag = solve(sign, 0.0)       # upper limb on the skyline
        if t is None:
            flags.append(flag)
        else:
            # Crossing duration: re-solve with the center one diameter
            # higher (= lower limb on the same line; refraction cancels).
            # Stays None when the disc never fully clears (grazing days
            # near the polar circles).
            t_full, _, _ = solve(sign, SUN_DIAMETER)
            if t_full is not None:
                out[key + "_dur"] = sign * (t - t_full)
        out[key] = t
        out[key + "_az"] = az                # converged with t (and skyline)

    if out["rise"] is None or out["set"] is None:
        flag = flags[0] if flags else "night"
        out["rise"] = out["set"] = None
        out["rise_az"] = out["set_az"] = None
        out["rise_dur"] = out["set_dur"] = None
        out["polar"] = flag
        out["daylight"] = 0.0 if flag == "night" else 1440.0
    else:
        out["daylight"] = out["set"] - out["rise"]
    return out


# ----------------------------------------------------------------------------
# Geomagnetism — true vs magnetic bearings. The azimuths above are TRUE
# (geographic) bearings; a hand-held compass points at MAGNETIC north, which
# sits several degrees away and drifts as the pole wanders. The World
# Magnetic Model (NOAA/NCEI + British Geological Survey, public domain) is
# the standard forecast of that offset — the declination — as a degree-12
# spherical-harmonic expansion of the whole field. The block below is the
# official WMM2025 coefficient file verbatim (one row per harmonic: n, m,
# g, h, gdot, hdot in nT and nT/year; valid 2025.0-2030.0, extrapolated
# linearly outside). To update circa 2030: paste the next model's .COF over
# the string and refresh the selftest's official test vectors with it.
# https://www.ncei.noaa.gov/products/world-magnetic-model
# ----------------------------------------------------------------------------
_WMM_COF = """\
    2025.0            WMM-2025     11/13/2024
  1  0  -29351.8       0.0       12.0        0.0
  1  1   -1410.8    4545.4        9.7      -21.5
  2  0   -2556.6       0.0      -11.6        0.0
  2  1    2951.1   -3133.6       -5.2      -27.7
  2  2    1649.3    -815.1       -8.0      -12.1
  3  0    1361.0       0.0       -1.3        0.0
  3  1   -2404.1     -56.6       -4.2        4.0
  3  2    1243.8     237.5        0.4       -0.3
  3  3     453.6    -549.5      -15.6       -4.1
  4  0     895.0       0.0       -1.6        0.0
  4  1     799.5     278.6       -2.4       -1.1
  4  2      55.7    -133.9       -6.0        4.1
  4  3    -281.1     212.0        5.6        1.6
  4  4      12.1    -375.6       -7.0       -4.4
  5  0    -233.2       0.0        0.6        0.0
  5  1     368.9      45.4        1.4       -0.5
  5  2     187.2     220.2        0.0        2.2
  5  3    -138.7    -122.9        0.6        0.4
  5  4    -142.0      43.0        2.2        1.7
  5  5      20.9     106.1        0.9        1.9
  6  0      64.4       0.0       -0.2        0.0
  6  1      63.8     -18.4       -0.4        0.3
  6  2      76.9      16.8        0.9       -1.6
  6  3    -115.7      48.8        1.2       -0.4
  6  4     -40.9     -59.8       -0.9        0.9
  6  5      14.9      10.9        0.3        0.7
  6  6     -60.7      72.7        0.9        0.9
  7  0      79.5       0.0       -0.0        0.0
  7  1     -77.0     -48.9       -0.1        0.6
  7  2      -8.8     -14.4       -0.1        0.5
  7  3      59.3      -1.0        0.5       -0.8
  7  4      15.8      23.4       -0.1        0.0
  7  5       2.5      -7.4       -0.8       -1.0
  7  6     -11.1     -25.1       -0.8        0.6
  7  7      14.2      -2.3        0.8       -0.2
  8  0      23.2       0.0       -0.1        0.0
  8  1      10.8       7.1        0.2       -0.2
  8  2     -17.5     -12.6        0.0        0.5
  8  3       2.0      11.4        0.5       -0.4
  8  4     -21.7      -9.7       -0.1        0.4
  8  5      16.9      12.7        0.3       -0.5
  8  6      15.0       0.7        0.2       -0.6
  8  7     -16.8      -5.2       -0.0        0.3
  8  8       0.9       3.9        0.2        0.2
  9  0       4.6       0.0       -0.0        0.0
  9  1       7.8     -24.8       -0.1       -0.3
  9  2       3.0      12.2        0.1        0.3
  9  3      -0.2       8.3        0.3       -0.3
  9  4      -2.5      -3.3       -0.3        0.3
  9  5     -13.1      -5.2        0.0        0.2
  9  6       2.4       7.2        0.3       -0.1
  9  7       8.6      -0.6       -0.1       -0.2
  9  8      -8.7       0.8        0.1        0.4
  9  9     -12.9      10.0       -0.1        0.1
 10  0      -1.3       0.0        0.1        0.0
 10  1      -6.4       3.3        0.0        0.0
 10  2       0.2       0.0        0.1       -0.0
 10  3       2.0       2.4        0.1       -0.2
 10  4      -1.0       5.3       -0.0        0.1
 10  5      -0.6      -9.1       -0.3       -0.1
 10  6      -0.9       0.4        0.0        0.1
 10  7       1.5      -4.2       -0.1        0.0
 10  8       0.9      -3.8       -0.1       -0.1
 10  9      -2.7       0.9       -0.0        0.2
 10 10      -3.9      -9.1       -0.0       -0.0
 11  0       2.9       0.0        0.0        0.0
 11  1      -1.5       0.0       -0.0       -0.0
 11  2      -2.5       2.9        0.0        0.1
 11  3       2.4      -0.6        0.0       -0.0
 11  4      -0.6       0.2        0.0        0.1
 11  5      -0.1       0.5       -0.1       -0.0
 11  6      -0.6      -0.3        0.0       -0.0
 11  7      -0.1      -1.2       -0.0        0.1
 11  8       1.1      -1.7       -0.1       -0.0
 11  9      -1.0      -2.9       -0.1        0.0
 11 10      -0.2      -1.8       -0.1        0.0
 11 11       2.6      -2.3       -0.1        0.0
 12  0      -2.0       0.0        0.0        0.0
 12  1      -0.2      -1.3        0.0       -0.0
 12  2       0.3       0.7       -0.0        0.0
 12  3       1.2       1.0       -0.0       -0.1
 12  4      -1.3      -1.4       -0.0        0.1
 12  5       0.6      -0.0       -0.0       -0.0
 12  6       0.6       0.6        0.1       -0.0
 12  7       0.5      -0.1       -0.0       -0.0
 12  8      -0.1       0.8        0.0        0.0
 12  9      -0.4       0.1        0.0       -0.0
 12 10      -0.2      -1.0       -0.1       -0.0
 12 11      -1.3       0.1       -0.0        0.0
 12 12      -0.7       0.2       -0.1       -0.1
999999999999999999999999999999999999999999999999
999999999999999999999999999999999999999999999999
"""


@functools.lru_cache(maxsize=1)
def _wmm_model():
    """Parse _WMM_COF -> (name, epoch, nmax, {(n, m): (g, h, gdot, hdot)})."""
    name, epoch, coeffs = "WMM", 2025.0, {}
    for line in _WMM_COF.splitlines():
        tok = line.split()
        if len(tok) == 3 and "." in tok[0]:      # header: epoch, name, date
            epoch, name = float(tok[0]), tok[1].replace("-", "")
        elif len(tok) == 6:                      # n  m  g  h  gdot  hdot
            coeffs[int(tok[0]), int(tok[1])] = tuple(float(v) for v in tok[2:])
    return name, epoch, max(n for n, _ in coeffs), coeffs


def _decimal_year(date):
    y0 = dt.date(date.year, 1, 1).toordinal()
    days_in_year = dt.date(date.year + 1, 1, 1).toordinal() - y0
    return date.year + (date.toordinal() - y0) / days_in_year


@functools.lru_cache(maxsize=4096)
def _wmm_declination(lat, lon, dyear, alt_km):
    """Evaluate the WMM: declination in degrees (+ = magnetic north lies
    east of true north) at geodetic lat/lon, a decimal year, and km above
    the WGS84 ellipsoid.

    The standard evaluation from the WMM technical report: convert to
    geocentric spherical coordinates, sum the Schmidt semi-normalized
    harmonic series for the field vector (Gauss coefficients advanced
    linearly from the epoch), rotate the vector back to the geodetic
    frame, then declination = atan2(east, north). Reproduces the official
    test values to 0.01 deg (selftest-pinned).
    """
    _name, epoch, nmax, coeffs = _wmm_model()
    t = dyear - epoch

    # Geodetic -> geocentric. The ellipsoid shifts latitude by up to
    # ~0.2 deg — enough to fail the official test vectors if skipped.
    a, f = 6378.137, 1.0 / 298.257223563       # WGS84 axis (km), flattening
    e2 = f * (2.0 - f)
    phi = math.radians(max(-89.995, min(89.995, lat)))   # pole is singular
    lam = math.radians(lon)
    sp, cp = math.sin(phi), math.cos(phi)
    rc = a / math.sqrt(1.0 - e2 * sp * sp)
    px = (rc + alt_km) * cp                    # distance from the spin axis
    pz = (rc * (1.0 - e2) + alt_km) * sp       # height above equator plane
    r = math.hypot(px, pz)
    phig = math.atan2(pz, px)                  # geocentric latitude
    mu, cmu = math.sin(phig), math.cos(phig)

    # Schmidt semi-normalized associated Legendre values P[n][m](sin phig)
    # and their latitude derivatives, by the classic recursion: each
    # diagonal term from the previous diagonal, the rest down its column.
    P = [[0.0] * (nmax + 1) for _ in range(nmax + 1)]
    dP = [[0.0] * (nmax + 1) for _ in range(nmax + 1)]
    P[0][0] = 1.0
    for n in range(1, nmax + 1):
        for m in range(n + 1):
            if n == m:
                k = 1.0 if n == 1 else math.sqrt((2.0 * n - 1.0) / (2.0 * n))
                P[n][n] = k * cmu * P[n - 1][n - 1]
                dP[n][n] = k * (cmu * dP[n - 1][n - 1] - mu * P[n - 1][n - 1])
            else:
                norm = math.sqrt(float(n * n - m * m))
                c1 = (2.0 * n - 1.0) / norm
                c2 = math.sqrt((n - 1.0) ** 2 - m * m) / norm
                pm2 = P[n - 2][m] if n - 2 >= m else 0.0
                dpm2 = dP[n - 2][m] if n - 2 >= m else 0.0
                P[n][m] = c1 * mu * P[n - 1][m] - c2 * pm2
                dP[n][m] = (c1 * (mu * dP[n - 1][m] + cmu * P[n - 1][m])
                            - c2 * dpm2)

    # Horizontal field in the geocentric frame: bx = north, by = east.
    cosl = [math.cos(m * lam) for m in range(nmax + 1)]
    sinl = [math.sin(m * lam) for m in range(nmax + 1)]
    ar = 6371.2 / r                            # geomagnetic ref. radius / r
    arn = ar ** 3                              # (a/r)^(n+2) at n = 1
    bx = by = bz = 0.0
    for n in range(1, nmax + 1):
        for m in range(n + 1):
            g, h, gdot, hdot = coeffs[n, m]
            g += t * gdot                      # secular variation
            h += t * hdot
            both = g * cosl[m] + h * sinl[m]
            bx -= arn * both * dP[n][m]
            by += arn * m * (g * sinl[m] - h * cosl[m]) * P[n][m]
            bz -= arn * (n + 1.0) * both * P[n][m]
        arn *= ar
    by /= cmu

    # North/down rotate through the (geocentric - geodetic) latitude gap;
    # east is the same axis in both frames. Declination is the compass
    # angle between the two norths.
    psi = phig - phi
    north = bx * math.cos(psi) - bz * math.sin(psi)
    return math.degrees(math.atan2(by, north))


def magnetic_declination(lat, lon, date=None, alt_km=0.0):
    """Magnetic declination (variation) in degrees at a place and date:
    how far magnetic north sits east (+) or west (-) of true north.
    date defaults to today. Local compass deviation (car bodies, fence
    wire, ore bodies) is beyond any global model and not included."""
    if date is None:
        date = dt.date.today()
    return _wmm_declination(lat, lon, _decimal_year(date), alt_km)


def true_to_magnetic(bearing, lat, lon, date=None):
    """A true-north bearing -> what a magnetic compass reads there/then."""
    return (bearing - magnetic_declination(lat, lon, date)) % 360.0


def magnetic_to_true(bearing, lat, lon, date=None):
    """A magnetic compass bearing -> the true-north bearing it stands for."""
    return (bearing + magnetic_declination(lat, lon, date)) % 360.0


def system_offset_minutes(date):
    """The OS's UTC offset (minutes) for this local date, DST included.

    Evaluated at local *noon*: DST transitions happen around 2-3 AM, so
    noon's offset is the one in force at both sunrise and sunset — even on
    the transition day itself.
    """
    local_noon = dt.datetime(date.year, date.month, date.day, 12, 0)
    return round(local_noon.astimezone().utcoffset().total_seconds() / 60)


# ----------------------------------------------------------------------------
# Manual daylight-saving rules — how DST laws are actually written: "the Nth
# <weekday> of <month>". A rule is a tuple (ordinal, weekday, month) with
# ordinal 1-4 or -1 for "last", weekday 0=Mon..6=Sun, month 1..12.
# ----------------------------------------------------------------------------
ORD_NAMES = {1: "1st", 2: "2nd", 3: "3rd", 4: "4th", -1: "last"}
ORD_VALUE = {v: k for k, v in ORD_NAMES.items()}
DAY_NAMES = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
MONTH_NAMES = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]


def fmt_rule(rule):
    return "%s %s of %s" % (ORD_NAMES[rule[0]], DAY_NAMES[rule[1]],
                            MONTH_NAMES[rule[2] - 1])


def nth_weekday_date(year, month, ordinal, weekday):
    """E.g. the 1st Sunday of October: nth_weekday_date(y, 10, 1, 6)."""
    if ordinal == -1:
        nxt = dt.date(year + month // 12, month % 12 + 1, 1)
        last = nxt - dt.timedelta(days=1)
        return last - dt.timedelta(days=(last.weekday() - weekday) % 7)
    first = dt.date(year, month, 1)
    return first + dt.timedelta(days=(weekday - first.weekday()) % 7
                                + 7 * (ordinal - 1))


def dst_active(date, start_rule, end_rule):
    """Is daylight saving in force at *noon* of this date?

    Start date inclusive, end date exclusive — exactly what per-day noon
    sampling of an OS zone yields, since real transitions happen at 2-3 AM
    (springing forward before noon; falling back before noon too). Southern-
    hemisphere periods (start month after end month) wrap the New Year.
    """
    s = nth_weekday_date(date.year, start_rule[2], start_rule[0], start_rule[1])
    e = nth_weekday_date(date.year, end_rule[2], end_rule[0], end_rule[1])
    if s <= e:
        return s <= date < e          # northern style: DST within one year
    return date >= s or date < e      # southern style: wraps the year end


def compute_rows(lat, lon, start, days, tz_mode="fixed", fixed_hours=0.0,
                 dst=None, horizon=None):
    """The almanac table: one dict per day with times in seconds.

    rise/set are seconds after local midnight (None when polar);
    rise_dur/set_dur = the disc's horizon-crossing time in whole seconds
    (upper-limb touch to lower-limb clear; None when there is no event or
    the disc never fully clears the line — grazing polar-circle days);
    rise_az/set_az = the sun's azimuth at those events, degrees clockwise
    from true north; rise_mag/set_mag = the same bearings for a magnetic
    compass (the WMM declination at that row's date applied) — all four
    rounded to 0.1 so the text file round-trips exactly;
    day = physical daylight seconds; off = that day's UTC offset in minutes.
    Both events use the same per-day offset, so 'day' stays physically true
    even across a DST changeover. In fixed mode, dst may be a dict
    {"hours": float, "start": rule, "end": rule} of manual daylight-saving
    rules (see dst_active) applied on top of fixed_hours. horizon is a
    parse_horizon() skyline profile (None/[] = flat).
    """
    rows = []
    for i in range(days):
        date = start + dt.timedelta(days=i)
        if tz_mode == "system":
            off_min = system_offset_minutes(date)
        elif dst and dst_active(date, dst["start"], dst["end"]):
            off_min = int(round(dst["hours"] * 60.0))
        else:
            off_min = int(round(fixed_hours * 60.0))
        ev = sun_events(date, lat, lon, off_min / 60.0, horizon)
        if ev["polar"]:
            rise_s = set_s = rise_az = set_az = rise_mag = set_mag = None
            rise_dur = set_dur = None
            day_s = 0 if ev["polar"] == "night" else 86400
        else:
            rise_s = int(round(ev["rise"] * 60.0))
            set_s = int(round(ev["set"] * 60.0))
            rise_dur = (None if ev["rise_dur"] is None
                        else int(round(ev["rise_dur"] * 60.0)))
            set_dur = (None if ev["set_dur"] is None
                       else int(round(ev["set_dur"] * 60.0)))
            rise_az = round(ev["rise_az"], 1)
            set_az = round(ev["set_az"], 1)
            # % 360 folds the possible round(359.95+) = 360.0 back to 0.0.
            rise_mag = round(true_to_magnetic(ev["rise_az"], lat, lon, date),
                             1) % 360.0
            set_mag = round(true_to_magnetic(ev["set_az"], lat, lon, date),
                            1) % 360.0
            day_s = set_s - rise_s
        rows.append({"date": date, "rise": rise_s, "rise_dur": rise_dur,
                     "rise_az": rise_az, "rise_mag": rise_mag,
                     "set": set_s, "set_dur": set_dur, "set_az": set_az,
                     "set_mag": set_mag, "day": day_s, "off": off_min})
    return rows


# ----------------------------------------------------------------------------
# Formatting / parsing helpers
# ----------------------------------------------------------------------------
def fmt_clock(sec):
    """Seconds after midnight -> wall clock 'HH:MM:SS' (None -> '--:--:--')."""
    if sec is None:
        return "--:--:--"
    s = int(round(sec)) % 86400
    return "%02d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)


def fmt_span(sec):
    """Duration 'HH:MM:SS' — unlike fmt_clock, 24:00:00 stays 24:00:00."""
    s = int(round(sec))
    return "%02d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)


def fmt_dur(sec):
    """Crossing duration 'MM:SS' — or 'H:MM:SS' should it ever top an hour
    (threshold days near the polar circles). None -> '--:--': no event, or
    the disc never fully clears the horizon line that day."""
    if sec is None:
        return "--:--"
    s = int(round(sec))
    if s >= 3600:
        return "%d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)
    return "%02d:%02d" % (s // 60, s % 60)


def parse_dur(s):
    """'MM:SS' or 'H:MM:SS' -> seconds; leading dashes -> None. The 3-part
    form also covers files from the brief HH:MM:SS duration format."""
    if s.startswith("--"):
        return None
    sec = 0
    for p in s.split(":"):
        sec = sec * 60 + int(p)
    return sec


def fmt_offset(minutes):
    sign = "+" if minutes >= 0 else "-"
    m = abs(int(minutes))
    return "%s%02d:%02d" % (sign, m // 60, m % 60)


def fmt_az(deg):
    """Azimuth -> right-aligned 6-char '123.4' (None -> '---')."""
    return "   ---" if deg is None else "%6.1f" % deg


def parse_az(s):
    """'123.4' -> degrees; '---' -> None."""
    return None if s == "---" else float(s)


def parse_clock(s):
    """'HH:MM:SS' -> seconds; '--:--:--' -> None. Accepts 24:00:00."""
    if s.startswith("--"):
        return None
    h, m, sec = s.split(":")
    return int(h) * 3600 + int(m) * 60 + int(sec)


def parse_offset(s):
    """'+10:00' / '-03:30' -> minutes."""
    sign = -1 if s[0] == "-" else 1
    h, m = s.lstrip("+-").split(":")
    return sign * (int(h) * 60 + int(m))


# Copy-pasted numbers (e.g. coordinates from Wikipedia / Google Maps) arrive
# with Unicode minus signs, degree marks, hard spaces and invisible characters
# that float() rejects even though they look identical on screen.
_NUM_TRANS = str.maketrans({
    "\u2212": "-",                        # true minus (the web favorite)
    "\u2010": "-", "\u2011": "-",        # hyphen, non-breaking hyphen
    "\u2012": "-", "\u2013": "-",        # figure dash, en dash
    "\u2014": "-",                        # em dash
    "\u00a0": " ", "\u2007": " ",        # no-break / figure spaces
    "\u2009": " ", "\u202f": " ",        # thin / narrow no-break spaces
    "\u200b": "", "\ufeff": "",          # zero-width space, BOM
})


def _clean_numeric(s):
    return (s or "").translate(_NUM_TRANS).strip()


_ANGLE_SPLIT = re.compile(r"[\s°º˚′″’”'\"]+")


def parse_angle(s):
    """Latitude/longitude entry -> signed decimal degrees.

    Accepts plain decimals ('-34.2195833'), web-pasted text with Unicode
    minus / degree marks ('−34.2195833°'), a hemisphere letter
    ('34.2195833 S', 'E 149.36625') and degrees-minutes-seconds
    ('34°13′10.5″ S', '34 13 10.5S'). A hemisphere letter
    wins over any sign: S/W make the value negative.
    """
    s = _clean_numeric(s)
    hemi = 0.0
    m = re.search(r"([NSEWnsew])\s*$", s)
    if m:
        hemi = -1.0 if m.group(1).upper() in "SW" else 1.0
        s = s[:m.start()].strip()
    else:
        m = re.match(r"^([NSEWnsew])\s*(?=[\d+.\-])", s)
        if m:
            hemi = -1.0 if m.group(1).upper() in "SW" else 1.0
            s = s[m.end():].strip()
    if not s:
        raise ValueError("empty angle")
    neg = s.startswith("-")
    parts = [p for p in _ANGLE_SPLIT.split(s.lstrip("+-")) if p]
    if not 1 <= len(parts) <= 3:
        raise ValueError("not an angle")
    deg = float(parts[0])
    minutes = float(parts[1]) if len(parts) > 1 else 0.0
    seconds = float(parts[2]) if len(parts) > 2 else 0.0
    if not (0.0 <= minutes < 60.0 and 0.0 <= seconds < 60.0):
        raise ValueError("minutes/seconds out of range")
    value = deg + minutes / 60.0 + seconds / 3600.0
    if hemi:
        return abs(value) * hemi
    return -value if neg else value


def parse_tz_hours(s):
    """User-typed fixed offset: '10', '-3.5', '9:30', '+5:45' -> hours."""
    s = _clean_numeric(s)
    if not s:
        raise ValueError("empty offset")
    sign = -1.0 if s[0] == "-" else 1.0
    body = s.lstrip("+-")
    if ":" in body:
        h, m = body.split(":", 1)
        val = int(h) + int(m) / 60.0
    else:
        val = float(body)
    val *= sign
    if not -14.0 <= val <= 14.0:
        raise ValueError("offset out of range")
    return val


# ----------------------------------------------------------------------------
# The text file: assumptions header + one aligned row per day
# ----------------------------------------------------------------------------
TABLE_HEADER = ("# Date        Sunrise   RiseDur  RiseAz RiseMag   Sunset"
                "     SetDur   SetAz  SetMag   Daylight   UTCoff")


def build_table_text(rows, meta):
    out = []
    a = out.append
    # Plain ASCII throughout the file, so it opens cleanly in any editor.
    a("# Sun2Set almanac - sunrise, sunset and day length")
    a("# Generated : %s" % meta.get("generated", ""))
    if meta.get("location"):
        a("# Location  : %s" % meta["location"])
    a("# Latitude  : %.6f  (degrees, + = north)" % meta.get("lat", 0.0))
    a("# Longitude : %.6f  (degrees, + = east)" % meta.get("lon", 0.0))
    a("# Time zone : %s" % meta.get("tz_desc", ""))
    a("# Horizon   : %s" % meta.get("horizon_desc",
                                    "flat 0.0 deg (open astronomical horizon)"))
    a("# Range     : %d days starting %s" % (len(rows), rows[0]["date"].isoformat()))
    a("# Algorithm : NOAA solar equations (Meeus); sunrise/sunset when the")
    a("#             sun's upper limb crosses the horizon stated above")
    a("#             (flat = zenith 90.833 deg: 34' refraction + 16' solar")
    a("#             radius; raised skylines re-evaluate refraction at the")
    a("#             hill altitude). Typically within 1-2 minutes of")
    a("#             published almanac values (real refraction varies).")
    a("# Times     : local wall clock HH:MM:SS; '--:--:--' means the sun")
    a("#             never rises / never sets that day (Daylight 00 / 24 h).")
    a("# Azimuths  : RiseAz/SetAz = the sun's bearing at each event, degrees")
    a("#             clockwise from TRUE north (N=0, E=90, S=180, W=270);")
    a("#             RiseMag/SetMag = the same bearings as read on a magnetic")
    a("#             compass; '---' on no-event days.")
    a("# Durations : RiseDur/SetDur = how long the disc takes to cross the")
    a("#             horizon line (upper limb first touches -> lower limb")
    a("#             clears, reversed at sunset), as MM:SS; '--:--' when")
    a("#             there is no event or the disc never fully clears the")
    a("#             line (grazing polar-circle days).")
    a("# Magnetic  : %s" % meta.get("mag_desc",
                                    "declination from the embedded WMM model"))
    a("#")
    a(TABLE_HEADER)
    for r in rows:
        a("%s    %s  %7s  %s  %s   %s  %7s  %s  %s   %s   %s" % (
            r["date"].isoformat(), fmt_clock(r["rise"]),
            fmt_dur(r["rise_dur"]), fmt_az(r["rise_az"]),
            fmt_az(r["rise_mag"]), fmt_clock(r["set"]),
            fmt_dur(r["set_dur"]), fmt_az(r["set_az"]),
            fmt_az(r["set_mag"]), fmt_span(r["day"]), fmt_offset(r["off"])))
    a("")
    return "\n".join(out)


def _rule_from_text(text):
    """'1st Sun of Oct' -> (1, 6, 10); None if not one of fmt_rule's forms."""
    m = re.match(r"^(1st|2nd|3rd|4th|last)\s+(%s)\s+of\s+(%s)$"
                 % ("|".join(DAY_NAMES), "|".join(MONTH_NAMES)),
                 (text or "").strip())
    if not m:
        return None
    return (ORD_VALUE[m.group(1)], DAY_NAMES.index(m.group(2)),
            MONTH_NAMES.index(m.group(3)) + 1)


def parse_tz_desc(desc):
    """Invert compute()'s '# Time zone :' header line back into settings.

    Returns a dict of settings, or None when the text isn't one of the
    three forms this app writes (hand-edited / foreign files are left be).
    Keep in sync with the tz_desc strings built in Sun2Set.compute().
    """
    d = (desc or "").split(";")[0].strip()
    if d.startswith("System local zone"):
        return {"tz_mode": "system"}
    m = re.match(r"Fixed UTC offset ([+-]\d{2}:\d{2})"
                 r"(?: with DST ([+-]\d{2}:\d{2}) from (.+) to (.+))?", d)
    if not m:
        return None
    out = {"tz_mode": "fixed", "fixed_hours": parse_offset(m.group(1)) / 60.0,
           "dst_enabled": False}
    if m.group(2):
        start, end = _rule_from_text(m.group(3)), _rule_from_text(m.group(4))
        if start and end:
            out.update(dst_enabled=True,
                       dst_hours=parse_offset(m.group(2)) / 60.0,
                       dst_start=start, dst_end=end)
    return out


def parse_horizon_desc(desc):
    """Invert compute()'s '# Horizon :' header line back into a Skyline
    entry string ('0', '5.0' or the az:alt pair list); None if unknown.
    Keep in sync with the hz_desc strings built in Sun2Set.compute()."""
    d = (desc or "").strip()
    if d.startswith("flat"):
        return "0"
    m = re.match(r"uniform (-?[0-9.]+) deg", d)
    if m:
        return m.group(1)
    m = re.match(r"az:alt profile (.+?) \(deg", d)
    if m:
        return m.group(1).strip()
    return None


def parse_table_text(text):
    """Inverse of build_table_text — tolerant of unknown '#' header lines."""
    meta = {"location": "", "lat": None, "lon": None,
            "tz_desc": "", "horizon_desc": "", "generated": ""}
    rows = []
    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        if s.startswith("#"):
            body = s.lstrip("#").strip()
            if ":" not in body:
                continue
            key, val = body.split(":", 1)
            key, val = key.strip().lower(), val.strip()
            if key == "location":
                meta["location"] = val
            elif key == "latitude":
                meta["lat"] = float(val.split()[0])
            elif key == "longitude":
                meta["lon"] = float(val.split()[0])
            elif key == "time zone":
                meta["tz_desc"] = val
            elif key == "horizon":
                meta["horizon_desc"] = val
            elif key == "generated":
                meta["generated"] = val
            continue
        parts = s.split()
        if len(parts) == 11:
            (date_s, rise_s, rdur_s, raz_s, rmag_s,
             set_s, sdur_s, saz_s, smag_s, day_s, off_s) = parts
        elif len(parts) == 9:               # pre-duration files still load
            (date_s, rise_s, raz_s, rmag_s,
             set_s, saz_s, smag_s, day_s, off_s) = parts
            rdur_s = sdur_s = "--:--"
        elif len(parts) == 7:               # pre-magnetic files still load
            date_s, rise_s, raz_s, set_s, saz_s, day_s, off_s = parts
            rmag_s = smag_s = "---"
            rdur_s = sdur_s = "--:--"
        elif len(parts) == 5:               # pre-azimuth files still load
            date_s, rise_s, set_s, day_s, off_s = parts
            raz_s = saz_s = rmag_s = smag_s = "---"
            rdur_s = sdur_s = "--:--"
        else:
            raise ValueError("unrecognized data line: %r" % line)
        day = parse_clock(day_s)
        rows.append({"date": dt.date.fromisoformat(date_s),
                     "rise": parse_clock(rise_s),
                     "rise_dur": parse_dur(rdur_s),
                     "rise_az": parse_az(raz_s), "rise_mag": parse_az(rmag_s),
                     "set": parse_clock(set_s),
                     "set_dur": parse_dur(sdur_s),
                     "set_az": parse_az(saz_s), "set_mag": parse_az(smag_s),
                     "day": 0 if day is None else day,
                     "off": parse_offset(off_s)})
    if not rows:
        raise ValueError("no data rows found in file")
    return rows, meta


# ----------------------------------------------------------------------------
# Config persistence — %APPDATA%\Sun2Set\config.json (same pattern as the games)
# ----------------------------------------------------------------------------
def _appdata_dir():
    base = os.environ.get("APPDATA") or os.path.expanduser("~")
    return os.path.join(base, "Sun2Set")


def _config_path():
    return os.path.join(_appdata_dir(), "config.json")


def _wm_position(geometry):
    """'1180x762+208+208' -> '+208+208'. A plain split('+') would mangle
    negative coordinates (a monitor left of / above the primary)."""
    m = re.match(r"\d+x\d+([+-]\d+[+-]\d+)$", geometry)
    return m.group(1) if m else "+100+100"


def _default_file_dir():
    """The user's real Documents folder, for the Save/Load dialogs.

    Without an explicit initialdir, a Windows file dialog opens in the last
    folder used by *any* dialog of the same executable — and that memory is
    keyed to pythonw.exe, i.e. shared across every Tkinter app on the
    machine (which is how another app's folder can leak in here). Documents
    may be OneDrive-redirected, so resolve it from the registry rather than
    guessing ~/Documents.
    """
    if sys.platform == "win32":
        try:
            import winreg
            key = (r"Software\Microsoft\Windows\CurrentVersion"
                   r"\Explorer\User Shell Folders")
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key) as k:
                raw, _type = winreg.QueryValueEx(k, "Personal")
            path = os.path.expandvars(raw)
            if os.path.isdir(path):
                return path
        except OSError:
            pass
    docs = os.path.join(os.path.expanduser("~"), "Documents")
    return docs if os.path.isdir(docs) else os.path.expanduser("~")


def _display_path(path):
    """Home-relative form ('~/…') for status messages — keeps even deep
    save/load paths within the status box's fixed line reserve."""
    home = os.path.expanduser("~")
    if path == home or path.startswith(home + os.sep):
        return "~" + path[len(home):]
    return path


def load_config():
    try:
        with open(_config_path(), "r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def save_config(config):
    try:
        os.makedirs(_appdata_dir(), exist_ok=True)
        with open(_config_path(), "w", encoding="utf-8") as f:
            json.dump(config, f, indent=2)
    except Exception:
        pass


# ----------------------------------------------------------------------------
# Rounded-corner widgets. Tk buttons can't round their edges, so these are
# small Canvases drawing the classic smoothed polygon — the doubled corner
# points pin the straight edges while smooth=True bends quadratic curves
# around each corner (same trick as MyPocketTanks' panel buttons).
# ----------------------------------------------------------------------------
class RoundButton(tk.Canvas):
    def __init__(self, parent, text, command, theme, width=130, height=34,
                 radius=11, font=None, primary=False, bg=None):
        super().__init__(parent, width=width, height=height,
                         bg=bg or theme["panel"], highlightthickness=0, bd=0,
                         cursor="hand2")
        self.T = theme
        self.command = command
        self.btn_text = text
        self.btn_font = font or (FONT, 11, "bold")
        self.w, self.h, self.r = width, height, radius
        self.fill = theme["primary"] if primary else theme["btn"]
        self.hover_fill = (theme["primary_hover"] if primary
                           else theme["btn_hover"])
        self.fg = theme["primary_fg"] if primary else theme["text"]
        self.state = "normal"
        self._hover = False
        self._redraw()
        self.bind("<Button-1>", self._on_click)
        self.bind("<Enter>", lambda e: self._set_hover(True))
        self.bind("<Leave>", lambda e: self._set_hover(False))

    def _redraw(self):
        self.delete("all")
        x0, y0, x1, y1 = 1, 1, self.w - 2, self.h - 2
        r = min(self.r, (x1 - x0) / 2, (y1 - y0) / 2)
        pts = [x0 + r, y0, x1 - r, y0, x1, y0, x1, y0 + r,
               x1, y1 - r, x1, y1, x1 - r, y1, x0 + r, y1,
               x0, y1, x0, y1 - r, x0, y0 + r, x0, y0]
        hovering = self._hover and self.state == "normal"
        self.create_polygon(pts, smooth=True,
                            fill=self.hover_fill if hovering else self.fill,
                            outline=self.T["btn_edge"])
        self.create_text(self.w // 2, self.h // 2, text=self.btn_text,
                         fill=self.fg if self.state == "normal"
                         else self.T["disabled"],
                         font=self.btn_font)

    def _set_hover(self, on):
        self._hover = on
        self._redraw()

    def _on_click(self, _event):
        if self.state == "normal" and self.command:
            self.command()

    def set_state(self, state):
        self.state = state
        self.configure(cursor="hand2" if state == "normal" else "arrow")
        self._redraw()

    def set_text(self, text):
        self.btn_text = text
        self._redraw()

    def restyle(self, fill=None, fg=None):
        if fill:
            self.fill = fill
        if fg:
            self.fg = fg
        self._redraw()


class RoundMenuButton(RoundButton):
    """A rounded drop-down: shows its StringVar's value; click pops a menu."""

    def __init__(self, parent, var, values, theme, **kw):
        self.var = var
        super().__init__(parent, var.get(), self._post, theme, **kw)
        self.menu = tk.Menu(self, tearoff=0, bg=theme["btn"],
                            fg=theme["text"], font=(FONT, 9),
                            activebackground=theme["btn_hover"],
                            activeforeground=theme["text"])
        for value in values:
            self.menu.add_command(label=value,
                                  command=lambda v=value: self.var.set(v))
        var.trace_add("write", lambda *_: self.set_text(self.var.get()))

    def _post(self):
        try:
            self.menu.tk_popup(self.winfo_rootx(),
                               self.winfo_rooty() + self.h)
        finally:
            self.menu.grab_release()


# ----------------------------------------------------------------------------
# The app. Sun2Set(root=None) is fully headless: parameters are plain
# attributes, compute()/save/load never touch Tk — that's what --selftest uses.
# ----------------------------------------------------------------------------
class Sun2Set:
    def __init__(self, root=None, persist=True):
        self.root = root
        self.persist = persist
        self.config = load_config() if persist else {}

        cfg = self.config
        self.location = str(cfg.get("location", DEFAULT_LOCATION))
        self.lat = self._cfg_float(cfg, "lat", DEFAULT_LAT, -90.0, 90.0)
        self.lon = self._cfg_float(cfg, "lon", DEFAULT_LON, -180.0, 180.0)
        self.tz_mode = cfg.get("tz_mode") if cfg.get("tz_mode") in ("system", "fixed") else "system"
        self.fixed_hours = self._cfg_float(cfg, "fixed_hours", 10.0, -14.0, 14.0)
        self.dst_enabled = bool(cfg.get("dst_enabled", False))
        self.dst_hours = self._cfg_float(cfg, "dst_hours", 11.0, -14.0, 14.0)
        self.dst_start = self._cfg_rule(cfg, "dst_start", (1, 6, 10))
        self.dst_end = self._cfg_rule(cfg, "dst_end", (1, 6, 4))
        self.horizon_str = str(cfg.get("horizon", "0"))
        try:
            parse_horizon(self.horizon_str)
        except ValueError:
            self.horizon_str = "0"
        self.theme = cfg.get("theme") if cfg.get("theme") in THEMES else "dark"
        self.T = THEMES[self.theme]
        self._pos_applied = False             # restore win_pos only once
        # The raw text of every entry as it was at last close — restored
        # verbatim so the form reopens exactly as you left it, even for
        # values that were never CALCULATEd (or wouldn't validate).
        form = cfg.get("form")
        self.saved_form = form if isinstance(form, dict) else {}
        self._current_tab = (cfg.get("tab")
                             if cfg.get("tab") in ("graph", "table")
                             else "graph")
        self.start = dt.date.today()          # fallback when no saved form
        self.days = int(self._cfg_float(cfg, "days", DEFAULT_DAYS, 1, 1500))

        self.rows = []                        # computed / loaded almanac rows
        self.meta = {}                        # assumptions that produced them
        self.table_text = ""                  # the text-file content
        self.canvas = None                    # set only when a GUI exists
        self._plot = None                     # graph geometry for hover lookups

        if root is not None:
            self._build_gui()
            self._on_calculate()              # show results immediately

    @staticmethod
    def _cfg_float(cfg, key, default, lo, hi):
        try:
            v = float(cfg.get(key, default))
            return v if lo <= v <= hi else default
        except (TypeError, ValueError):
            return default

    @staticmethod
    def _cfg_rule(cfg, key, default):
        try:
            o, w, m = (int(v) for v in cfg.get(key))
            if o in ORD_NAMES and 0 <= w <= 6 and 1 <= m <= 12:
                return (o, w, m)
        except (TypeError, ValueError):
            pass
        return default

    # ---------------------------------------------------------- computation
    def compute(self):
        """Fill rows / meta / table_text from the current parameters."""
        dst = None
        if self.tz_mode == "fixed" and self.dst_enabled:
            dst = {"hours": self.dst_hours,
                   "start": self.dst_start, "end": self.dst_end}
        profile = parse_horizon(self.horizon_str)
        self.rows = compute_rows(self.lat, self.lon, self.start, self.days,
                                 self.tz_mode, self.fixed_hours, dst, profile)
        if self.tz_mode == "system":
            names = " / ".join(n for n in time.tzname if n)
            tz_desc = ("System local zone (%s) - DST aware; each day's "
                       "offset is in the UTCoff column" % names)
        elif dst:
            # Keep everything essential before the ';' — the graph subtitle
            # shows only the part up to it.
            tz_desc = ("Fixed UTC offset %s with DST %s from %s to %s; "
                       "each day's offset is in the UTCoff column"
                       % (fmt_offset(int(round(self.fixed_hours * 60))),
                          fmt_offset(int(round(self.dst_hours * 60))),
                          fmt_rule(self.dst_start), fmt_rule(self.dst_end)))
        else:
            tz_desc = ("Fixed UTC offset %s (no daylight-saving adjustments)"
                       % fmt_offset(int(round(self.fixed_hours * 60))))
        if not profile:
            hz_desc = "flat 0.0 deg (open astronomical horizon)"
        elif len(profile) == 1:
            hz_desc = ("uniform %.1f deg above the astronomical horizon"
                       % profile[0][1])
        else:
            hz_desc = ("az:alt profile %s (deg; N=0 E=90; linear interp)"
                       % ", ".join("%g:%g" % p for p in profile))
        # One human-scale declination summary for the header (mid-span);
        # the RiseMag/SetMag columns themselves use each row's own date.
        mid = self.start + dt.timedelta(days=self.days // 2)
        d_mid = magnetic_declination(self.lat, self.lon, mid)
        mag_desc = ("declination %.1f deg %s (%s at %s): magnetic bearing"
                    " = true %s %.1f deg"
                    % (abs(d_mid), "E" if d_mid >= 0 else "W",
                       _wmm_model()[0], mid.isoformat(),
                       "-" if d_mid >= 0 else "+", abs(d_mid)))
        self.meta = {
            "generated": dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "location": self.location, "lat": self.lat, "lon": self.lon,
            "tz_desc": tz_desc, "horizon_desc": hz_desc, "mag_desc": mag_desc,
        }
        self.table_text = build_table_text(self.rows, self.meta)

    def autosave(self):
        """Write the latest table next to the config; returns the path."""
        if not self.persist:
            return None
        path = os.path.join(_appdata_dir(), "sun2set_latest.txt")
        os.makedirs(_appdata_dir(), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(self.table_text)
        return path

    def save_text_file(self, path):
        with open(path, "w", encoding="utf-8") as f:
            f.write(self.table_text)

    def load_text_file(self, path):
        """Load a saved almanac and restore every assumption its header
        records — coordinates, time-zone / DST settings, skyline and range —
        so calling compute() afterwards regenerates the file as-is."""
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        rows, meta = parse_table_text(text)   # raises ValueError on bad files
        self.rows, self.meta, self.table_text = rows, meta, text
        if meta.get("lat") is not None:
            self.lat = meta["lat"]
        if meta.get("lon") is not None:
            self.lon = meta["lon"]
        if meta.get("location"):
            self.location = meta["location"]
        tz = parse_tz_desc(meta.get("tz_desc"))
        if tz:
            self.tz_mode = tz["tz_mode"]
            if tz["tz_mode"] == "fixed":
                self.fixed_hours = tz["fixed_hours"]
                self.dst_enabled = tz["dst_enabled"]
                if tz["dst_enabled"]:
                    self.dst_hours = tz["dst_hours"]
                    self.dst_start = tz["dst_start"]
                    self.dst_end = tz["dst_end"]
        skyline = parse_horizon_desc(meta.get("horizon_desc"))
        if skyline is not None:
            try:
                parse_horizon(skyline)
                self.horizon_str = skyline
            except ValueError:
                pass                          # malformed header: keep current
        self.start, self.days = rows[0]["date"], len(rows)
        return len(rows)

    # ------------------------------------------------------------------ GUI
    def _build_gui(self):
        root = self.root
        T = self.T
        root.title("Sun2Set — sunrise & sunset almanac")
        root.configure(bg=T["bg"])
        root.resizable(False, False)

        # The panel reports its natural size (no pack_propagate(False)): the
        # same widgets are taller under Aqua than on Windows, and pinning the
        # panel to the graph column's height clipped whatever packed last —
        # the status text. The window instead grows to fit the taller column.
        panel = tk.Frame(root, bg=T["panel"], width=PANEL_W,
                         highlightbackground=T["edge"], highlightthickness=1)
        panel.pack(side="left", fill="y", padx=(10, 6), pady=10)

        def saved(key, fallback):
            value = self.saved_form.get(key)
            return value if isinstance(value, str) else fallback

        self._section(panel, "LOCATION")
        self.loc_entry = self._entry_row(panel, "Name",
                                         saved("location", self.location))
        self.lat_entry = self._entry_row(panel, "Latitude",
                                         saved("lat", "%.4f" % self.lat))
        self.lon_entry = self._entry_row(panel, "Longitude",
                                         saved("lon", "%.4f" % self.lon))
        self._hint(panel, "degrees (+ = N/E); paste or DMS OK,")
        self._hint(panel, "e.g. -34.2196 or 34°13'10.5\" S")

        self._section(panel, "TIME ZONE")
        self.tz_var = tk.StringVar(master=root, value=self.tz_mode)
        for value, label in (("system", "System zone (DST aware)"),
                             ("fixed", "Fixed UTC offset (hours):")):
            tk.Radiobutton(panel, text=label, value=value, variable=self.tz_var,
                           command=self._on_tz_mode, bg=T["panel"],
                           fg=T["text"], selectcolor=T["btn"],
                           activebackground=T["panel"],
                           activeforeground=T["text"], font=(FONT, 10),
                           anchor="w").pack(fill="x", padx=10)
        self.tz_entry = self._entry_row(panel, "Offset",
                                        saved("offset", "%g" % self.fixed_hours),
                                        width=8)
        self._hint(panel, "e.g. 10, -3.5 or 9:30")
        self.dst_on_var = tk.BooleanVar(master=root, value=self.dst_enabled)
        self.dst_check = tk.Checkbutton(
            panel, text="with daylight saving:", variable=self.dst_on_var,
            command=self._on_tz_mode, bg=T["panel"], fg=T["text"],
            selectcolor=T["btn"], activebackground=T["panel"],
            activeforeground=T["text"], font=(FONT, 10),
            anchor="w", disabledforeground=T["disabled"])
        self.dst_check.pack(fill="x", padx=10)
        self._dst_menus = []
        self.dst_entry = self._entry_row(panel, "DST offs.",
                                         saved("dst_offset",
                                               "%g" % self.dst_hours), width=8)
        self.dst_start_vars = self._rule_row(panel, "starts", self.dst_start)
        self.dst_end_vars = self._rule_row(panel, "ends", self.dst_end)

        self._section(panel, "HORIZON")
        self.horizon_entry = self._entry_row(panel, "Skyline",
                                             saved("skyline", self.horizon_str))
        self._hint(panel, "deg above true horizon: 0 = flat,")
        self._hint(panel, "5 = hills, or az:alt 60:2, 90:6, 240:8")

        self._section(panel, "RANGE")
        self.date_entry = self._entry_row(panel, "Start",
                                          saved("start", self.start.isoformat()))
        self.days_entry = self._entry_row(panel, "Days",
                                          saved("days", str(self.days)))

        RoundButton(panel, "CALCULATE", self._on_calculate, T,
                    width=PANEL_W - 24, height=40, radius=12,
                    primary=True).pack(padx=12, pady=(16, 4))
        row = tk.Frame(panel, bg=T["panel"])
        row.pack(padx=12, pady=2)
        half = (PANEL_W - 24 - 6) // 2
        RoundButton(row, "SAVE AS…", self._on_save, T,
                    width=half, height=34).pack(side="left", padx=(0, 6))
        RoundButton(row, "LOAD…", self._on_load, T,
                    width=half, height=34).pack(side="left")

        # Status/feedback box: a fixed six-line reserve (measured from the
        # actual font, so it tracks platform metrics). Fixed, because the
        # panel now propagates its size — an unreserved label would resize
        # the whole window every time the message changed.
        line_h = tkfont.Font(root=root, font=(FONT, 9)).metrics("linespace")
        status_box = tk.Frame(panel, bg=T["panel"], height=6 * line_h + 4)
        status_box.pack(fill="x", padx=12, pady=(10, 12))
        status_box.pack_propagate(False)
        self.status = tk.Label(status_box, text="", bg=T["panel"], fg=T["sub"],
                               font=(FONT, 9), wraplength=PANEL_W - 28,
                               justify="left", anchor="nw")
        self.status.pack(fill="both", expand=True)

        right = tk.Frame(root, bg=T["bg"])
        right.pack(side="left", fill="both", padx=(0, 10), pady=10)
        tabs = tk.Frame(right, bg=T["bg"])
        tabs.pack(fill="x")
        self.tab_btns = {}
        for key, label in (("graph", "GRAPH"), ("table", "TABLE")):
            b = RoundButton(tabs, label, lambda k=key: self._show_tab(k), T,
                            width=110, height=32, bg=T["bg"])
            b.pack(side="left", padx=(0, 6), pady=(0, 6))
            self.tab_btns[key] = b
        RoundButton(tabs, "THEME: %s" % self.theme.upper(), self._on_theme,
                    T, width=150, height=32, bg=T["bg"],
                    font=(FONT, 10, "bold")).pack(side="right", pady=(0, 6))
        tk.Label(tabs, text="hover the graph for exact values", bg=T["bg"],
                 fg=T["sub"], font=(FONT, 9)).pack(side="right", padx=(0, 12))

        container = tk.Frame(right, bg=T["bg"],
                             width=GRAPH_W + 2, height=GRAPH_H + 2)
        container.pack()
        container.grid_propagate(False)
        container.rowconfigure(0, weight=1)
        container.columnconfigure(0, weight=1)

        # The canvas lives in its own frame: Canvas.tkraise() raises canvas
        # *items*, not the widget, so tab switching must raise plain Frames.
        graph_frame = tk.Frame(container, bg=T["bg"])
        graph_frame.grid(row=0, column=0, sticky="nsew")
        self.canvas = tk.Canvas(graph_frame, width=GRAPH_W, height=GRAPH_H,
                                bg=T["bg"], highlightthickness=1,
                                highlightbackground=T["edge"])
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<Motion>", self._on_graph_motion)
        self.canvas.bind("<Leave>", lambda e: self.canvas.delete("hover"))

        table_frame = tk.Frame(container, bg=T["bg"])
        table_frame.grid(row=0, column=0, sticky="nsew")
        scroll = tk.Scrollbar(table_frame)
        scroll.pack(side="right", fill="y")
        # wrap="none" silently clips long rows at the window edge, and the
        # RiseDur/SetDur columns pushed rows past 100 chars — so the table
        # also gets a horizontal scrollbar.
        hscroll = tk.Scrollbar(table_frame, orient="horizontal")
        hscroll.pack(side="bottom", fill="x")
        self.table_widget = tk.Text(table_frame, bg=T["panel"], fg=T["text"],
                                    font=(FONT, 10), relief="flat",
                                    state="disabled", wrap="none",
                                    xscrollcommand=hscroll.set,
                                    yscrollcommand=scroll.set)
        self.table_widget.pack(side="left", fill="both", expand=True)
        scroll.configure(command=self.table_widget.yview)
        hscroll.configure(command=self.table_widget.xview)
        self._tab_frames = {"graph": graph_frame, "table": table_frame}

        self._show_tab(getattr(self, "_current_tab", "graph"))
        self._on_tz_mode()
        root.bind("<Return>", lambda e: self._on_calculate())

        if not self._pos_applied:             # skip on theme rebuilds
            self._pos_applied = True
            pos = self.config.get("win_pos")
            if isinstance(pos, str) and pos.startswith(("+", "-")):
                try:
                    root.geometry(pos)
                except tk.TclError:
                    pass
        root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _section(self, parent, text):
        tk.Label(parent, text=text, bg=self.T["panel"], fg=self.T["accent"],
                 font=(FONT, 10, "bold"), anchor="w").pack(
            fill="x", padx=12, pady=(10, 2))

    def _hint(self, parent, text):
        tk.Label(parent, text=text, bg=self.T["panel"], fg=self.T["sub"],
                 font=(FONT, 8)).pack(anchor="w", padx=12)

    def _entry_row(self, parent, label, initial, width=16):
        T = self.T
        row = tk.Frame(parent, bg=T["panel"])
        row.pack(fill="x", padx=12, pady=2)
        tk.Label(row, text=label, bg=T["panel"], fg=T["text"], font=(FONT, 10),
                 width=9, anchor="w").pack(side="left")
        e = tk.Entry(row, bg=T["btn"], fg=T["text"],
                     insertbackground=T["text"], relief="flat",
                     highlightthickness=1, highlightbackground=T["btn_edge"],
                     highlightcolor=T["accent"], font=(FONT, 10), width=width,
                     disabledbackground=T["panel"],
                     disabledforeground=T["disabled"])
        e.insert(0, initial)
        e.pack(side="left", fill="x", expand=True)
        return e

    def _rule_row(self, parent, label, rule):
        """A '1st / Sun / Oct' dropdown triple; returns its three StringVars."""
        T = self.T
        row = tk.Frame(parent, bg=T["panel"])
        row.pack(fill="x", padx=12, pady=2)
        tk.Label(row, text=label, bg=T["panel"], fg=T["text"], font=(FONT, 10),
                 width=9, anchor="w").pack(side="left")
        out = []
        for values, initial in ((list(ORD_NAMES.values()), ORD_NAMES[rule[0]]),
                                (DAY_NAMES, DAY_NAMES[rule[1]]),
                                (MONTH_NAMES, MONTH_NAMES[rule[2] - 1])):
            var = tk.StringVar(master=self.root, value=initial)
            mb = RoundMenuButton(row, var, values, T, width=52, height=26,
                                 radius=9, font=(FONT, 9))
            mb.pack(side="left", padx=(0, 4))
            self._dst_menus.append(mb)
            out.append(var)
        return out

    @staticmethod
    def _rule_from_vars(vars_):
        o, d, m = (v.get() for v in vars_)
        return (ORD_VALUE[o], DAY_NAMES.index(d), MONTH_NAMES.index(m) + 1)

    def _show_tab(self, key):
        self._current_tab = key
        self._tab_frames[key].tkraise()
        for k, b in self.tab_btns.items():
            if k == key:
                b.restyle(fill=self.T["btn_hover"], fg=self.T["accent"])
            else:
                b.restyle(fill=self.T["btn"], fg=self.T["sub"])

    def _on_tz_mode(self):
        fixed = self.tz_var.get() == "fixed"
        self.tz_entry.configure(state="normal" if fixed else "disabled")
        self.dst_check.configure(state="normal" if fixed else "disabled")
        dst_on = fixed and self.dst_on_var.get()
        self.dst_entry.configure(state="normal" if dst_on else "disabled")
        for mb in self._dst_menus:
            mb.set_state("normal" if dst_on else "disabled")

    def _on_theme(self):
        """Flip dark <-> light: rebuild the GUI, preserving all form state."""
        entries = ("loc_entry", "lat_entry", "lon_entry", "tz_entry",
                   "dst_entry", "horizon_entry", "date_entry", "days_entry")
        raw = [getattr(self, name).get() for name in entries]
        tz_mode, dst_on = self.tz_var.get(), self.dst_on_var.get()
        rules = ([v.get() for v in self.dst_start_vars],
                 [v.get() for v in self.dst_end_vars])
        status = self.status.cget("text")

        self.theme = "light" if self.theme == "dark" else "dark"
        self.T = THEMES[self.theme]
        self.config["theme"] = self.theme
        if self.persist:
            save_config(self.config)          # survive a non-close exit too

        for widget in self.root.winfo_children():
            widget.destroy()
        self._build_gui()
        for name, text in zip(entries, raw):
            self._set_entry_text(getattr(self, name), text)
        self.tz_var.set(tz_mode)
        self.dst_on_var.set(dst_on)
        for vars_, saved in zip((self.dst_start_vars, self.dst_end_vars),
                                rules):
            for var, value in zip(vars_, saved):
                var.set(value)
        self._on_tz_mode()
        self._refresh_views()
        fg = (self.T["ok"] if status.startswith("✓")
              else self.T["err"] if status.startswith("✗") else None)
        self._set_status(status, fg)

    def _set_status(self, text, color=None):
        self.status.configure(text=text, fg=color or self.T["sub"])

    # ------------------------------------------------------- button actions
    def _read_form(self):
        """Entries -> attributes. Returns an error message or None."""
        try:
            lat = parse_angle(self.lat_entry.get())
            if not -90.0 <= lat <= 90.0:
                raise ValueError
        except ValueError:
            return ("Latitude must be -90..90 — e.g. -34.2196, "
                    "34.2196 S or 34°13'10.5\" S")
        try:
            lon = parse_angle(self.lon_entry.get())
            if not -180.0 <= lon <= 180.0:
                raise ValueError
        except ValueError:
            return ("Longitude must be -180..180 — e.g. 149.3663, "
                    "149.3663 E or 149°21'58.5\" E")
        tz_mode = self.tz_var.get()
        fixed, dst_hours = self.fixed_hours, self.dst_hours
        dst_on = bool(self.dst_on_var.get())
        dst_start, dst_end = self.dst_start, self.dst_end
        if tz_mode == "fixed":
            try:
                fixed = parse_tz_hours(self.tz_entry.get())
            except ValueError:
                return "Fixed offset must look like 10, -3.5 or 9:30 (|h| <= 14)"
            if dst_on:
                try:
                    dst_hours = parse_tz_hours(self.dst_entry.get())
                except ValueError:
                    return "DST offset must look like 11, -2.5 or 10:30 (|h| <= 14)"
                dst_start = self._rule_from_vars(self.dst_start_vars)
                dst_end = self._rule_from_vars(self.dst_end_vars)
        horizon_str = self.horizon_entry.get().strip() or "0"
        try:
            parse_horizon(horizon_str)
        except ValueError:
            return ("Horizon must be 0, one altitude, or az:alt pairs "
                    "like 60:2, 90:6 (alt -5..60, az 0..360)")
        try:
            start = dt.date.fromisoformat(self.date_entry.get().strip())
        except ValueError:
            return "Start date must be YYYY-MM-DD"
        try:
            days = int(self.days_entry.get().strip())
            if not 1 <= days <= 1500:
                raise ValueError
        except ValueError:
            return "Days must be a whole number in 1..1500"
        self.location = self.loc_entry.get().strip()
        self.lat, self.lon = lat, lon
        self.tz_mode, self.fixed_hours = tz_mode, fixed
        self.dst_enabled, self.dst_hours = dst_on, dst_hours
        self.dst_start, self.dst_end = dst_start, dst_end
        self.horizon_str = horizon_str
        self.start, self.days = start, days
        return None

    def _on_calculate(self):
        err = self._read_form()
        if err:
            self._set_status("✗ " + err, self.T["err"])
            return
        self.compute()
        self._refresh_views()
        msg = "✓ Calculated %d days from %s." % (len(self.rows),
                                                 self.start.isoformat())
        try:
            path = self.autosave()
            if path:
                msg += "\n\nAutosaved to:\n%s" % _display_path(path)
        except OSError as e:
            msg += "\n\n(autosave failed: %s)" % e
        self._set_status(msg, self.T["ok"])

    def _on_save(self):
        if not self.rows:
            self._set_status("✗ Nothing to save — CALCULATE first.",
                             self.T["err"])
            return
        slug = "".join(c if c.isalnum() else "-" for c in self.location).strip("-")
        initial = "Sun2Set_%s_%s.txt" % (slug or "data",
                                         self.rows[0]["date"].isoformat())
        path = filedialog.asksaveasfilename(
            parent=self.root, title="Save almanac as…",
            defaultextension=".txt", initialfile=initial,
            initialdir=self._file_dialog_dir(),
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")])
        if not path:
            return
        try:
            self.save_text_file(path)
            self._remember_file_dir(path)
            self._set_status("✓ Saved %d days to:\n%s"
                             % (len(self.rows), _display_path(path)),
                             self.T["ok"])
        except OSError as e:
            self._set_status("✗ Save failed: %s" % e, self.T["err"])

    def _file_dialog_dir(self):
        """Where Save As… / Load… should open: the folder you last used for
        an almanac (persisted), else your real Documents folder."""
        d = self.config.get("file_dir")
        if isinstance(d, str) and os.path.isdir(d):
            return d
        return _default_file_dir()

    def _remember_file_dir(self, path):
        self.config["file_dir"] = os.path.dirname(os.path.abspath(path))
        if self.persist:
            save_config(self.config)

    def _on_load(self, path=None):
        if not path:
            path = filedialog.askopenfilename(
                parent=self.root, title="Load a saved almanac…",
                initialdir=self._file_dialog_dir(),
                filetypes=[("Text files", "*.txt"), ("All files", "*.*")])
        if not path:
            return
        self._remember_file_dir(path)
        try:
            n = self.load_text_file(path)
        except (OSError, ValueError) as e:
            self._set_status("✗ Load failed: %s" % e, self.T["err"])
            return
        # Reflect every restored assumption back into the form, so pressing
        # CALCULATE regenerates the loaded file as-is.
        for entry, value in ((self.loc_entry, self.location),
                             (self.lat_entry, "%.6f" % self.lat),
                             (self.lon_entry, "%.6f" % self.lon),
                             (self.tz_entry, "%g" % self.fixed_hours),
                             (self.dst_entry, "%g" % self.dst_hours),
                             (self.horizon_entry, self.horizon_str),
                             (self.date_entry, self.rows[0]["date"].isoformat()),
                             (self.days_entry, str(n))):
            self._set_entry_text(entry, str(value))
        self.tz_var.set(self.tz_mode)
        self.dst_on_var.set(self.dst_enabled)
        for rule_vars, rule in ((self.dst_start_vars, self.dst_start),
                                (self.dst_end_vars, self.dst_end)):
            for var, value in zip(rule_vars, (ORD_NAMES[rule[0]],
                                              DAY_NAMES[rule[1]],
                                              MONTH_NAMES[rule[2] - 1])):
                var.set(value)
        self._on_tz_mode()
        self._refresh_views()
        self._set_status("✓ Loaded %d days from:\n%s" % (n, _display_path(path)),
                         self.T["ok"])

    @staticmethod
    def _set_entry_text(entry, text):
        """Rewrite an Entry even when it is disabled (state-restore dance)."""
        state = entry.cget("state")
        entry.configure(state="normal")
        entry.delete(0, "end")
        entry.insert(0, text)
        entry.configure(state=state)

    def _on_close(self):
        self.config.update({
            "win_pos": _wm_position(self.root.geometry()),
            # last-validated values (fallbacks if the form snapshot is lost)
            "location": self.location, "lat": self.lat, "lon": self.lon,
            "fixed_hours": self.fixed_hours, "dst_hours": self.dst_hours,
            "horizon": self.horizon_str, "days": self.days,
            # the live widget state, exactly as clicked right now...
            "tz_mode": self.tz_var.get(),
            "dst_enabled": bool(self.dst_on_var.get()),
            "dst_start": list(self._rule_from_vars(self.dst_start_vars)),
            "dst_end": list(self._rule_from_vars(self.dst_end_vars)),
            "tab": self._current_tab,
            "theme": self.theme,
            # ...and every entry exactly as typed, calculated or not
            "form": {
                "location": self.loc_entry.get(),
                "lat": self.lat_entry.get(),
                "lon": self.lon_entry.get(),
                "offset": self.tz_entry.get(),
                "dst_offset": self.dst_entry.get(),
                "skyline": self.horizon_entry.get(),
                "start": self.date_entry.get(),
                "days": self.days_entry.get(),
            },
        })
        if self.persist:
            save_config(self.config)
        self.root.destroy()

    def _refresh_views(self):
        self.table_widget.configure(state="normal")
        self.table_widget.delete("1.0", "end")
        self.table_widget.insert("1.0", self.table_text)
        self.table_widget.configure(state="disabled")
        self._draw_graph()

    # ------------------------------------------------------------ the graph
    def _draw_graph(self):
        c = self.canvas
        th = self.T                           # theme (T is the top margin)
        c.configure(bg=th["bg"])
        c.delete("all")
        self._plot = None
        rows = self.rows
        if not rows:
            c.create_text(GRAPH_W // 2, GRAPH_H // 2, fill=th["sub"],
                          font=(FONT, 12), text="No data — press CALCULATE")
            return

        L, R, T, B = 64, 16, 64, 40           # margins around the plot area
        pw, ph = GRAPH_W - L - R, GRAPH_H - T - B
        n = len(rows)

        def X(i):
            return L + pw * i / max(1, n - 1)

        def Y(hours):                          # 00:00 bottom … 24:00 top
            return T + ph * (1.0 - hours / 24.0)

        def clock_h(sec):
            return (sec % 86400) / 3600.0

        # Shaded daylight band: one polygon per contiguous non-polar run.
        run = []
        for i, r in enumerate(rows + [{"rise": None, "set": None}]):
            if r["rise"] is not None and r["set"] is not None:
                run.append(i)
            elif run:
                if len(run) >= 2:
                    pts = [(X(i2), Y(clock_h(rows[i2]["rise"]))) for i2 in run]
                    pts += [(X(i2), Y(clock_h(rows[i2]["set"])))
                            for i2 in reversed(run)]
                    c.create_polygon(pts, fill=th["band"], outline="")
                run = []

        # Grid: hours (every 2 h) and month boundaries.
        for h in range(0, 25, 2):
            y = Y(h)
            c.create_line(L, y, GRAPH_W - R, y, fill=th["grid"])
            c.create_text(L - 8, y, anchor="e", fill=th["sub"],
                          font=(FONT, 8), text="%02d:00" % h)
        for i, r in enumerate(rows):
            if r["date"].day == 1 and n > 27:
                x = X(i)
                c.create_line(x, T, x, T + ph, fill=th["grid"])
                label = r["date"].strftime("%b")
                if r["date"].month == 1:
                    label += "\n" + str(r["date"].year)
                c.create_text(x, T + ph + 6, anchor="n", fill=th["sub"],
                              font=(FONT, 8), text=label, justify="center")
        c.create_rectangle(L, T, GRAPH_W - R, T + ph, outline=th["axis"])

        # Curves, split wherever a value is missing (polar days).
        def curve(getter, color):
            seg = []
            for i, r in enumerate(rows + [{"rise": None, "set": None, "day": None}]):
                v = getter(r) if i < n else None
                if v is None:
                    if len(seg) >= 4:
                        c.create_line(*seg, fill=color, width=2)
                    elif len(seg) == 2:
                        c.create_oval(seg[0] - 1, seg[1] - 1, seg[0] + 1,
                                      seg[1] + 1, fill=color, outline=color)
                    seg = []
                else:
                    seg += [X(i), Y(v)]

        curve(lambda r: None if r["rise"] is None else clock_h(r["rise"]),
              th["rise"])
        curve(lambda r: None if r["set"] is None else clock_h(r["set"]),
              th["set"])
        curve(lambda r: None if r["day"] is None else r["day"] / 3600.0,
              th["day"])

        # Title + legend.
        title = self.meta.get("location") or ""
        lat, lon = self.meta.get("lat"), self.meta.get("lon")
        if lat is not None and lon is not None:
            title += "  (%+.4f°, %+.4f°)" % (lat, lon)
        c.create_text(L, 14, anchor="w", fill=th["text"],
                      font=(FONT, 13, "bold"),
                      text=title.strip() or "Sun almanac")
        tz_short = self.meta.get("tz_desc", "").split(";")[0]
        sub = "%s → %s   ·   %s" % (rows[0]["date"].isoformat(),
                                    rows[-1]["date"].isoformat(), tz_short)
        c.create_text(L, 34, anchor="w", fill=th["sub"], font=(FONT, 8),
                      text=sub)
        hz = self.meta.get("horizon_desc", "")
        if hz and not hz.startswith("flat"):
            short = hz.split(" above")[0].split(" (")[0]
            if len(short) > 60:
                short = short[:57] + "..."
            c.create_text(L, 49, anchor="w", fill=th["sub"], font=(FONT, 8),
                          text="skyline: %s" % short)
        lx = GRAPH_W - R
        for label, color in (("day length", th["day"]), ("sunset", th["set"]),
                             ("sunrise", th["rise"])):
            t = c.create_text(lx, 52, anchor="e", fill=th["sub"],
                              font=(FONT, 9), text=label)
            x0 = c.bbox(t)[0]
            c.create_line(x0 - 24, 52, x0 - 6, 52, fill=color, width=3)
            lx = x0 - 32

        self._plot = {"L": L, "T": T, "pw": pw, "ph": ph, "n": n}

    def _on_graph_motion(self, ev):
        p = self._plot
        c = self.canvas
        c.delete("hover")
        if not p or not self.rows:
            return
        n = p["n"]
        i = int(round((ev.x - p["L"]) / p["pw"] * (n - 1))) if n > 1 else 0
        if not (0 <= i < n and p["L"] - 6 <= ev.x <= p["L"] + p["pw"] + 6):
            return
        r = self.rows[i]
        th = self.T
        x = p["L"] + p["pw"] * i / max(1, n - 1)
        c.create_line(x, p["T"], x, p["T"] + p["ph"], fill=th["hover"],
                      dash=(2, 3), tags="hover")
        def event(sec, az):                   # "07:00:57 az 63.4°"
            clock = fmt_clock(sec)
            return clock if az is None else "%s az %.1f°" % (clock, az)
        txt = "%s   rise %s   set %s   day %s   UTC%s" % (
            r["date"].isoformat(), event(r["rise"], r["rise_az"]),
            event(r["set"], r["set_az"]),
            fmt_span(r["day"]), fmt_offset(r["off"]))
        if r["rise_dur"] is not None or r["set_dur"] is not None:
            txt += ("\nhorizon crossing   rise %s   set %s"
                    % (fmt_dur(r["rise_dur"]), fmt_dur(r["set_dur"])))
        if r["rise_mag"] is not None and r["set_mag"] is not None:
            txt += ("\nmagnetic compass   rise az %.1f°   set az %.1f°"
                    % (r["rise_mag"], r["set_mag"]))
        tid = c.create_text(p["L"] + 10, p["T"] + 10, anchor="nw",
                            fill=th["text"], font=(FONT, 10), text=txt,
                            tags="hover")
        x0, y0, x1, y1 = c.bbox(tid)
        rect = c.create_rectangle(x0 - 6, y0 - 4, x1 + 6, y1 + 4,
                                  fill=th["readout"], outline=th["edge"],
                                  tags="hover")
        c.tag_lower(rect, tid)


# ----------------------------------------------------------------------------
# Self-test (headless — no window). Reference times from published almanacs;
# tolerances cover refraction-model differences between sources.
# ----------------------------------------------------------------------------
def _hms(h, m, s=0):
    return h * 3600 + m * 60 + s


def selftest():
    print("Sun2Set selftest...")

    # 1. Known sunrise/sunset times (local wall clock at the given offset)
    #    and the sun's azimuth at those moments, verified against
    #    WolframAlpha; tolerances cover its minute rounding plus
    #    refraction-model differences between sources (±2 min of time is
    #    up to ~1° of azimuth at high latitudes).
    cases = [
        ("London Jun-21 solstice", 51.5074, -0.1278, 1.0,
         dt.date(2026, 6, 21), _hms(4, 43), _hms(21, 21), 48.9, 311.0),
        ("Sydney Dec-21 solstice", -33.8688, 151.2093, 11.0,
         dt.date(2026, 12, 21), _hms(5, 40), _hms(20, 5), 119.3, 240.8),
        ("Reykjavik Dec-21", 64.1466, -21.9426, 0.0,
         dt.date(2026, 12, 21), _hms(11, 22), _hms(15, 29), 151.8, 208.0),
    ]
    for (label, lat, lon, tzh, date,
         want_rise, want_set, want_raz, want_saz) in cases:
        ev = sun_events(date, lat, lon, tzh)
        for got, want, tol, what in (
                (ev["rise"] * 60.0, want_rise, 120, "rise"),
                (ev["set"] * 60.0, want_set, 120, "set"),
                (ev["rise_az"], want_raz, 2.0, "rise az"),
                (ev["set_az"], want_saz, 2.0, "set az")):
            assert abs(got - want) <= tol, (
                "%s %s: got %s, want ~%s" % (label, what, got, want))
    print("  reference sunrise/sunset times + azimuths OK (%d locations)"
          % len(cases))

    # 1b. Horizon-crossing durations: RiseDur/SetDur = the disc climbing its
    #     own 32' diameter (SUN_DIAMETER; refraction cancels — both
    #     endpoints put a limb on the same line). Two independent checks:
    #     the slant formula — the sun's vertical speed at the horizon is
    #     15 deg/h * cos(lat) * sin(azimuth), so duration ~ diameter/speed
    #     (runs a few % long at Reykjavik, where the azimuth drifts
    #     visibly across an 11-minute crossing) — and pinned values.
    for label, lat, lon, tzh, date, *_ in cases:
        ev = sun_events(date, lat, lon, tzh)
        for what, dur, az in (("rise", ev["rise_dur"], ev["rise_az"]),
                              ("set", ev["set_dur"], ev["set_az"])):
            assert dur is not None, (label, what)
            rate = (15.0 * math.cos(math.radians(lat))
                    * abs(math.sin(math.radians(az))))        # deg/hour
            approx = SUN_DIAMETER / rate * 60.0               # minutes
            assert abs(dur - approx) <= max(0.15, 0.08 * approx), (
                label, what, dur, approx)
    for lat, lon, tzh, date, want_dur in (
            (-33.8688, 151.2093, 11.0, dt.date(2026, 12, 21), 2.94),
            (51.5074, -0.1278, 1.0, dt.date(2026, 6, 21), 4.52),
            (64.1466, -21.9426, 0.0, dt.date(2026, 12, 21), 10.83)):
        ev = sun_events(date, lat, lon, tzh)
        assert abs(ev["rise_dur"] - want_dur) < 0.2, (lat, ev["rise_dur"])
        assert abs(ev["set_dur"] - want_dur) < 0.2, (lat, ev["set_dur"])
    print("  horizon-crossing durations OK (Sydney solstice ~2m56s, "
          "Reykjavik midwinter ~10m50s)")

    # 2. Polar night and midnight sun (Longyearbyen, Svalbard, 78.22 N).
    for date, expect in ((dt.date(2026, 12, 21), "night"),
                         (dt.date(2026, 6, 21), "day")):
        ev = sun_events(date, 78.2232, 15.6267, 1.0)
        assert ev["polar"] == expect and ev["rise"] is None and ev["set"] is None
        assert ev["rise_az"] is None and ev["set_az"] is None
        assert ev["rise_dur"] is None and ev["set_dur"] is None
        assert ev["daylight"] == (0.0 if expect == "night" else 1440.0)
    # Grazing days: when the sun first returns after the polar night it
    # peeks over the horizon without ever fully clearing it — the rise
    # exists but its duration doesn't, until a couple of days later (and
    # the first full crossing still takes ~40 minutes).
    rows_g = compute_rows(78.2232, 15.6267, dt.date(2026, 2, 10), 12,
                          "fixed", 1.0)
    grazing = [r for r in rows_g
               if r["rise"] is not None and r["rise_dur"] is None]
    full = [r for r in rows_g if r["rise_dur"] is not None]
    assert grazing and full and full[0]["rise_dur"] > 1200, (
        len(grazing), len(full))
    print("  polar night / midnight sun OK (+%d grazing days: sun up, disc "
          "never fully clear)" % len(grazing))

    # 3. Equinox at the equator: day length just over 12 h (refraction),
    #    sun rising due east and setting due west.
    ev = sun_events(dt.date(2027, 3, 20), 0.0, 0.0, 0.0)
    d = (ev["set"] - ev["rise"]) * 60.0
    assert _hms(12, 4) < d < _hms(12, 10), fmt_span(d)
    assert abs(ev["rise_az"] - 90.0) < 1.0, ev["rise_az"]
    assert abs(ev["set_az"] - 270.0) < 1.0, ev["set_az"]
    # Rising vertically, the fastest crossing on Earth: ~2 min 8 s.
    assert abs(ev["rise_dur"] - 2.13) < 0.05, ev["rise_dur"]
    print("  equator equinox day length OK (%s, rise az %.1f, crossing %s)"
          % (fmt_span(d), ev["rise_az"],
             fmt_dur(int(round(ev["rise_dur"] * 60.0)))))

    # 4. Magnetic declination — the embedded WMM2025 must reproduce the
    #    official NOAA/BGS test vectors (WMM2025_TestValues.txt ships with
    #    the model; declination stated to 0.01 deg) across both epochs,
    #    both test altitudes and some awkward high-latitude points.
    vectors = [
        # (decimal year, alt km, lat, lon, declination)
        (2025.0,   0.0,  80.0,    0.0,   1.28),
        (2025.0,   0.0,   0.0,  120.0,  -0.16),
        (2025.0,   0.0, -80.0,  240.0,  68.78),
        (2025.0, 100.0,  80.0,    0.0,   0.85),
        (2025.0, 100.0,   0.0,  120.0,  -0.15),
        (2025.0, 100.0, -80.0,  240.0,  68.21),
        (2027.5,   0.0,  80.0,    0.0,   2.59),
        (2027.5,   0.0,   0.0,  120.0,  -0.24),
        (2027.5,   0.0, -80.0,  240.0,  68.49),
        (2027.5, 100.0,  80.0,    0.0,   2.16),
        (2027.5, 100.0,   0.0,  120.0,  -0.23),
        (2027.5, 100.0, -80.0,  240.0,  67.93),
        (2025.0,  28.0,  89.0, -121.0, -99.77),    # 1 deg off the pole
        (2025.0,  48.0,  80.0,  -96.0, -29.91),
        (2025.0,  18.0,   0.0,   21.0,   1.29),
        (2025.5,   6.0, -36.0, -137.0,  20.28),
    ]
    for dyear, alt, la, lo, want in vectors:
        got = _wmm_declination(la, lo, dyear, alt)
        assert abs(got - want) <= 0.01, (dyear, la, lo, got, want)
    name, epoch, nmax, coeffs = _wmm_model()
    assert name == "WMM2025" and epoch == 2025.0 and nmax == 12
    assert len(coeffs) == 90                    # sum(n+1 for n in 1..12)
    # The date-based wrapper against NOAA's online calculator (same model,
    # same date 2026-07-04): Sydney, Binda NSW, London, Reykjavik.
    day = dt.date(2026, 7, 4)
    assert abs(_decimal_year(day) - 2026.50411) < 1e-5
    for la, lo, want in ((-33.8688, 151.2093, 12.82083),
                         (-34.2195833, 149.36625, 12.29126),
                         (51.5074, -0.1278, 1.16280),
                         (64.1466, -21.9426, -11.13349)):
        got = magnetic_declination(la, lo, day)
        assert abs(got - want) < 0.01, (la, lo, got, want)
    # true <-> magnetic: inverse pair, wrapping through north (Binda's
    # declination is ~ +12.3 E, so a true 005 reads as ~ 352.7 magnetic).
    def angdiff(x, y):
        return abs((x - y + 180.0) % 360.0 - 180.0)
    for b in (0.0, 5.0, 63.4, 180.0, 351.9):
        m = true_to_magnetic(b, -34.2195833, 149.36625, day)
        assert 0.0 <= m < 360.0
        back = magnetic_to_true(m, -34.2195833, 149.36625, day)
        assert angdiff(back, b) < 1e-9, (b, m, back)
    assert true_to_magnetic(5.0, -34.2195833, 149.36625, day) > 350.0
    assert magnetic_to_true(352.7, -34.2195833, 149.36625, day) < 10.0
    d_binda = magnetic_declination(-34.2195833, 149.36625, day)
    print("  magnetic declination OK (%d official WMM vectors; Binda "
          "2026-07-04: %+.2f deg)" % (len(vectors), d_binda))

    # 5. A full 366-day Sydney batch (fixed offset): internal consistency.
    rows = compute_rows(-33.8688, 151.2093, dt.date(2026, 7, 3), 366,
                        "fixed", 10.0)
    assert len(rows) == 366 and rows[-1]["date"] == dt.date(2027, 7, 3)
    for a, b in zip(rows, rows[1:]):
        assert (b["date"] - a["date"]).days == 1
    for r in rows:
        assert r["rise"] is not None and r["set"] > r["rise"]
        assert r["day"] == r["set"] - r["rise"]
        assert r["off"] == 600
        # Crossing durations: 2:34 (equinox) .. 2:56 (solstice) at Sydney's
        # latitude, whole seconds, rise/set nearly symmetric within a day.
        assert 145 <= r["rise_dur"] <= 185 and 145 <= r["set_dur"] <= 185, r
        assert abs(r["rise_dur"] - r["set_dur"]) <= 5, r
        # Azimuths mirror around the meridian; only the declination's drift
        # between the two events (a fraction of a degree) breaks the symmetry.
        assert 0.0 < r["rise_az"] < 180.0 < r["set_az"] < 360.0
        assert abs(r["rise_az"] + r["set_az"] - 360.0) < 1.5, r
        # The magnetic columns are the true bearings minus that day's own
        # declination (each rounded to 0.1 independently).
        d = magnetic_declination(-33.8688, 151.2093, r["date"])
        assert angdiff((r["rise_az"] - d) % 360.0, r["rise_mag"]) < 0.11, r
        assert angdiff((r["set_az"] - d) % 360.0, r["set_mag"]) < 0.11, r
    hours = [r["day"] / 3600.0 for r in rows]
    assert 9.4 < min(hours) < 10.2, min(hours)    # shortest ~9h53m (June)
    assert 14.0 < max(hours) < 14.9, max(hours)   # longest ~14h25m (December)
    print("  366-day batch consistency OK (day length %.2f..%.2f h)"
          % (min(hours), max(hours)))

    # 6. System-zone mode runs and yields sane per-day offsets.
    rows2 = compute_rows(-33.8688, 151.2093, dt.date(2026, 7, 3), 30, "system")
    assert all(isinstance(r["off"], int) and -14 * 60 <= r["off"] <= 14 * 60
               for r in rows2)
    print("  system time-zone mode OK (offset today: %s)"
          % fmt_offset(rows2[0]["off"]))

    # 7. Manual daylight-saving rules. Sydney law: DST from the 1st Sunday
    #    of October (inclusive) to the 1st Sunday of April (exclusive).
    assert nth_weekday_date(2026, 10, 1, 6) == dt.date(2026, 10, 4)
    assert nth_weekday_date(2027, 4, 1, 6) == dt.date(2027, 4, 4)
    assert nth_weekday_date(2027, 3, -1, 6) == dt.date(2027, 3, 28)   # last Sun
    assert nth_weekday_date(2026, 11, 1, 6) == dt.date(2026, 11, 1)   # day 1
    SYD_DST = {"hours": 11.0, "start": (1, 6, 10), "end": (1, 6, 4)}
    rows_man = compute_rows(-33.8688, 151.2093, dt.date(2026, 7, 3), 366,
                            "fixed", 10.0, SYD_DST)
    offs = {r["date"]: r["off"] for r in rows_man}
    assert offs[dt.date(2026, 10, 3)] == 600 and offs[dt.date(2026, 10, 4)] == 660
    assert offs[dt.date(2027, 4, 3)] == 660 and offs[dt.date(2027, 4, 4)] == 600
    assert offs[dt.date(2026, 7, 3)] == 600 and offs[dt.date(2026, 12, 21)] == 660
    # Northern-hemisphere rules must not wrap the year end (EU: last Sun of
    # Mar -> last Sun of Oct; in 2026 that's Mar 29 and Oct 25).
    EU_DST = {"hours": 1.0, "start": (-1, 6, 3), "end": (-1, 6, 10)}
    offs_eu = {r["date"]: r["off"] for r in compute_rows(
        51.5074, -0.1278, dt.date(2026, 1, 1), 365, "fixed", 0.0, EU_DST)}
    assert offs_eu[dt.date(2026, 3, 28)] == 0 and offs_eu[dt.date(2026, 3, 29)] == 60
    assert offs_eu[dt.date(2026, 10, 24)] == 60 and offs_eu[dt.date(2026, 10, 25)] == 0
    assert offs_eu[dt.date(2026, 6, 21)] == 60 and offs_eu[dt.date(2026, 12, 21)] == 0
    # The user-facing promise: with the right rules, fixed mode reproduces
    # the system zone exactly (only provable on a Sydney-rules machine).
    rows_sys = compute_rows(-33.8688, 151.2093, dt.date(2026, 7, 3), 366,
                            "system")
    if [r["off"] for r in rows_sys] == [r["off"] for r in rows_man]:
        assert rows_sys == rows_man
        print("  manual DST rules reproduce the system zone exactly")
    else:
        print("  (system zone here isn't Sydney-like; equivalence check skipped)")

    # 8. Raised horizon (valley / hills skyline).
    assert abs(_zenith_for_horizon(0.0) - ZENITH_OFFICIAL) < 1e-12
    assert abs(_zenith_for_horizon(10.0) - 80.357) < 0.01  # Bennett ~5.4'+16'
    assert parse_horizon("") == [] and parse_horizon("0") == []
    assert parse_horizon("5") == [(0.0, 5.0)]
    prof = parse_horizon("90:4, 270:8")
    assert horizon_alt(prof, 90) == 4.0 and horizon_alt(prof, 270) == 8.0
    assert abs(horizon_alt(prof, 0) - 6.0) < 1e-9      # wraps through north
    assert abs(horizon_alt(prof, 180) - 6.0) < 1e-9
    assert horizon_alt([], 123.4) == 0.0
    for bad in ("abc", "90:4, x", "5, 6", "0:99", "400:5"):
        try:
            parse_horizon(bad)
            raise AssertionError("parse_horizon accepted %r" % bad)
        except ValueError:
            pass
    flat = sun_events(dt.date(2026, 12, 21), -33.8688, 151.2093, 11.0)
    hills = sun_events(dt.date(2026, 12, 21), -33.8688, 151.2093, 11.0,
                       parse_horizon("5"))
    d_rise = (hills["rise"] - flat["rise"])            # minutes later
    d_set = (flat["set"] - hills["set"])               # minutes earlier
    assert 15.0 < d_rise < 60.0 and 15.0 < d_set < 60.0, (d_rise, d_set)
    # The azimuths move with the events: Sydney's December morning sun
    # tracks from the ESE horizon toward north, so a rise delayed by a
    # ridge happens at a *smaller* azimuth; the evening mirrors it.
    assert 2.0 < flat["rise_az"] - hills["rise_az"] < 20.0, (
        flat["rise_az"], hills["rise_az"])
    assert 2.0 < hills["set_az"] - flat["set_az"] < 20.0, (
        flat["set_az"], hills["set_az"])
    # Crossing durations exist over a ridge too (the lower-limb solve
    # re-converges its own azimuth and hill altitude).
    assert 2.0 < hills["rise_dur"] < 4.0, hills["rise_dur"]
    assert 2.0 < hills["set_dur"] < 4.0, hills["set_dur"]
    # An explicitly flat profile must match the default bit-for-bit.
    assert sun_events(dt.date(2026, 12, 21), -33.8688, 151.2093, 11.0,
                      []) == flat
    # Nordic valley in December: noon sun peaks at ~6.6 deg, ridge is 12.
    ev = sun_events(dt.date(2026, 12, 21), 60.0, 10.0, 1.0,
                    parse_horizon("12"))
    assert ev["polar"] == "night" and ev["rise"] is None
    # The parameter flows through compute_rows and shortens every day.
    rows_hz = compute_rows(-33.8688, 151.2093, dt.date(2026, 7, 3), 30,
                           "fixed", 10.0, horizon=parse_horizon("5"))
    for hz_r, flat_r in zip(rows_hz, rows):
        assert hz_r["day"] < flat_r["day"]
    print("  raised-horizon skyline OK (5 deg hills: Sydney solstice rise "
          "%s -> %s)" % (fmt_clock(flat["rise"] * 60),
                         fmt_clock(hills["rise"] * 60)))

    # 9. Text table round-trip, including polar '--:--:--' rows.
    meta = {"generated": "2026-07-03 00:00:00", "location": "Testville",
            "lat": -33.8688, "lon": 151.2093, "tz_desc": "Fixed UTC offset +10:00"}
    text = build_table_text(rows, meta)
    rows_b, meta_b = parse_table_text(text)
    assert rows_b == rows
    assert abs(meta_b["lat"] - meta["lat"]) < 1e-9
    assert abs(meta_b["lon"] - meta["lon"]) < 1e-9
    assert meta_b["location"] == "Testville"
    rows3 = compute_rows(78.2232, 15.6267, dt.date(2026, 10, 1), 150,
                         "fixed", 1.0)
    assert any(r["rise"] is None and r["day"] == 0 for r in rows3)  # polar night
    assert all(r["rise_mag"] is None and r["set_mag"] is None
               for r in rows3 if r["rise"] is None)
    text3 = build_table_text(rows3, meta)
    rows3_b, _ = parse_table_text(text3)
    assert rows3_b == rows3
    assert any(" ---" in ln for ln in text3.splitlines()
               if not ln.startswith("#"))   # polar days show '---' azimuths
    # Duration formatting: MM:SS, spilling to H:MM:SS on the rare threshold
    # days near the polar circles where a crossing tops an hour.
    for sec, txt in ((174, "02:54"), (2421, "40:21"), (0, "00:00"),
                     (3599, "59:59"), (4530, "1:15:30"), (None, "--:--")):
        assert fmt_dur(sec) == txt, (sec, fmt_dur(sec))
        assert parse_dur(txt) == sec, txt
    assert parse_dur("00:02:54") == 174     # the brief HH:MM:SS format
    assert parse_dur("--:--:--") is None
    legacy11 = (TABLE_HEADER + "\n"
                "2026-07-03    07:00:57  00:02:54    62.6    49.8   16:57:53"
                "  00:02:54   297.4   284.6   09:56:56   +10:00\n")
    row_l11 = parse_table_text(legacy11)[0][0]
    assert row_l11["rise_dur"] == 174 and row_l11["set_dur"] == 174
    # Files saved before the RiseDur/SetDur columns existed (9 columns),
    # before the magnetic columns (7), and before the azimuth columns (5),
    # must still load.
    legacy9 = (TABLE_HEADER + "\n"
               "2026-07-03    07:00:57    62.6    49.8   16:57:53   297.4"
               "   284.6   09:56:56   +10:00\n")
    row_l9 = parse_table_text(legacy9)[0][0]
    assert row_l9["rise"] == _hms(7, 0, 57) and row_l9["rise_az"] == 62.6
    assert row_l9["rise_mag"] == 49.8 and row_l9["set_mag"] == 284.6
    assert row_l9["rise_dur"] is None and row_l9["set_dur"] is None
    legacy7 = (TABLE_HEADER + "\n"
               "2026-07-03    07:00:57    63.4   16:57:53   296.6   09:56:56"
               "   +10:00\n")
    row_l7 = parse_table_text(legacy7)[0][0]
    assert row_l7["rise_az"] == 63.4 and row_l7["set_az"] == 296.6
    assert row_l7["rise_mag"] is None and row_l7["set_mag"] is None
    assert row_l7["rise_dur"] is None and row_l7["set_dur"] is None
    legacy = (TABLE_HEADER + "\n"
              "2026-07-03    07:00:57   16:57:53   09:56:56   +10:00\n")
    row_l = parse_table_text(legacy)[0][0]
    assert row_l["rise"] == _hms(7, 0, 57) and row_l["off"] == 600
    assert row_l["rise_az"] is None and row_l["set_az"] is None
    assert row_l["rise_mag"] is None and row_l["set_mag"] is None
    assert row_l["rise_dur"] is None and row_l["set_dur"] is None
    print("  table text round-trip OK (incl. polar rows, legacy files)")

    # 10. The entry parsers: fixed offsets, and coordinates as typed OR as
    #    pasted from the web (Unicode minus U+2212, degree marks, hemisphere
    #    letters, DMS). Binda NSW's Wikipedia coordinate is the test case.
    for s, want in (("10", 10.0), ("+10", 10.0), ("-3.5", -3.5),
                    ("9:30", 9.5), ("-3:30", -3.5), ("5.75", 5.75), ("0", 0.0),
                    ("−3:30", -3.5)):
        assert parse_tz_hours(s) == want, s
    for bad in ("abc", "", "15", "-15", "10:xx"):
        try:
            parse_tz_hours(bad)
            raise AssertionError("parse_tz_hours accepted %r" % bad)
        except ValueError:
            pass
    binda = 34.0 + 13.0 / 60.0 + 10.5 / 3600.0     # 34.2195833...
    for s, want in (("-34.2195833", -34.2195833),
                    ("−34.2195833", -34.2195833),      # unicode minus
                    ("-34.2195833°", -34.2195833),     # degree sign
                    ("−34.2195833 ", -34.2195833),  # hard space
                    ("34.2195833 S", -34.2195833),
                    ("-34.2195833 s", -34.2195833),         # S beats the sign
                    ("149.36625 E", 149.36625),
                    ("W 71.06", -71.06),
                    ("34°13′10.5″S", -binda),
                    ("34 13 10.5 S", -binda),
                    ("34°13'10.5\" S", -binda),
                    ("0", 0.0)):
        got = parse_angle(s)
        assert abs(got - want) < 1e-9, (s, got, want)
    for bad in ("", "abc", "34,22", "12 61", "1 2 3 4", "12 30 -5"):
        try:
            parse_angle(bad)
            raise AssertionError("parse_angle accepted %r" % bad)
        except ValueError:
            pass
    print("  entry parsers OK (incl. web-pasted coordinates)")

    # 11. Window-position parsing (negative coords = monitor left of/above
    #     the primary; a naive split('+') mangles them).
    assert _wm_position("1180x762+208+208") == "+208+208"
    assert _wm_position("800x600-1200+30") == "-1200+30"
    assert _wm_position("800x600+30-1200") == "+30-1200"
    assert _wm_position("garbage") == "+100+100"
    dialog_dir = _default_file_dir()
    assert os.path.isdir(dialog_dir), dialog_dir
    print("  window-position parsing OK; file dialogs default to %s"
          % dialog_dir)

    # 12. Headless app instance: compute + save/load round-trip, no Tk, no
    #     real config/autosave files (persist=False). Load must restore every
    #     assumption from the header, so the file regenerates itself.
    assert parse_tz_desc("System local zone (AEST) - DST aware; x")["tz_mode"] \
        == "system"
    assert parse_tz_desc("Fixed UTC offset +09:30 (no daylight-saving "
                         "adjustments)") == {"tz_mode": "fixed",
                                             "fixed_hours": 9.5,
                                             "dst_enabled": False}
    assert parse_tz_desc("total nonsense") is None
    assert parse_horizon_desc("flat 0.0 deg (open astronomical horizon)") == "0"
    assert parse_horizon_desc("uniform 5.5 deg above the astronomical "
                              "horizon") == "5.5"
    assert parse_horizon_desc("mystery") is None
    import tempfile
    app = Sun2Set(root=None, persist=False)
    app.location, app.lat, app.lon = "Testville", 40.0, -75.0
    app.tz_mode, app.fixed_hours = "fixed", -5.0
    app.dst_enabled, app.dst_hours = True, -4.0
    app.dst_start, app.dst_end = (2, 6, 3), (1, 6, 11)      # US-style rules
    app.horizon_str = "90:4, 270:8"
    app.start, app.days = dt.date(2026, 3, 1), 30           # spans 2nd Sun Mar
    app.compute()
    assert app.autosave() is None            # persist=False writes nothing
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "almanac.txt")
        app.save_text_file(path)
        app2 = Sun2Set(root=None, persist=False)
        n = app2.load_text_file(path)
        assert n == 30 and app2.rows == app.rows
        assert app2.location == "Testville"
        assert abs(app2.lat - 40.0) < 1e-9 and abs(app2.lon - -75.0) < 1e-9
        # every assumption came back from the header...
        assert app2.tz_mode == "fixed" and app2.fixed_hours == -5.0
        assert app2.dst_enabled and app2.dst_hours == -4.0
        assert app2.dst_start == (2, 6, 3) and app2.dst_end == (1, 6, 11)
        assert app2.horizon_str == "90:4, 270:8"
        assert app2.start == dt.date(2026, 3, 1) and app2.days == 30
        # ...so the loaded file regenerates itself exactly
        app2.compute()
        assert app2.rows == app.rows
    print("  headless save/load round-trip OK (the file regenerates itself)")

    print("All Sun2Set self-tests passed.")


def main():
    if "--selftest" in sys.argv:
        selftest()
        return
    root = tk.Tk()
    Sun2Set(root)
    root.mainloop()


if __name__ == "__main__":
    main()

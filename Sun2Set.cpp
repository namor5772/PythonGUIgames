// Sun2Set.cpp — a C++ port of Sun2Set.py with identical functionality.
//
// A sunrise / sunset almanac: NOAA/Meeus solar equations with per-event
// refinement, true AND magnetic azimuths (the official WMM2025 model is
// embedded verbatim), horizon-crossing durations, system or fixed time
// zones with manual DST rules, raised-horizon skylines, a hover-readout
// graph and a text table that saves/loads with every assumption in its
// header — plus the same `--selftest` reference anchors as the .py
// (WMM test vectors to 0.01 deg, pinned solar times, table round-trips)
// and the same %APPDATA%\Sun2Set\config.json.
//
// No third-party libraries — same pattern as MyTetris.cpp/MyPocketTanks.cpp:
// a platform-free core that renders into a draw-command scene, followed by
// a Win32 + GDI backend and a Cocoa backend (this one file compiles as
// Objective-C++ on macOS).
//
// Build (Windows):  .\build_native.ps1 -App Sun2Set
// Build (macOS):    ./build_native.command Sun2Set
// Self-test:        Sun2Set.exe --selftest

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

static const double PI = 3.14159265358979323846;
static double rad(double d) { return d * PI / 180.0; }
static double deg(double r) { return r * 180.0 / PI; }

// ----------------------------------------------------------------------------
// Look & feel — the same two THEMES as the .py; FONT resolved per platform.
// ----------------------------------------------------------------------------
struct RGB { uint8_t r, g, b; };
static RGB hexColor(const char* h) {
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    return RGB{ (uint8_t)(nib(h[1]) * 16 + nib(h[2])),
                (uint8_t)(nib(h[3]) * 16 + nib(h[4])),
                (uint8_t)(nib(h[5]) * 16 + nib(h[6])) };
}

struct Theme {
    RGB bg, panel, edge, btn, btn_edge, btn_hover, text, sub, accent,
        primary, primary_fg, primary_hover, disabled, err, ok,
        band, grid, axis, rise, set, day, hover, readout;
};

static Theme themeByName(const std::string& name) {
    if (name == "light") {
        return Theme{
            hexColor("#e9ecf2"), hexColor("#f7f8fb"), hexColor("#c3c8d6"),
            hexColor("#e0e4ee"), hexColor("#b2b9cc"), hexColor("#d2d7e5"),
            hexColor("#20222e"), hexColor("#5b6072"), hexColor("#9a6a00"),
            hexColor("#ffd91a"), hexColor("#101018"), hexColor("#ffe763"),
            hexColor("#a3a8b8"), hexColor("#c62828"), hexColor("#177a3e"),
            hexColor("#efe5bd"), hexColor("#d7dae5"), hexColor("#9aa0b4"),
            hexColor("#d99a00"), hexColor("#e05320"), hexColor("#159048"),
            hexColor("#8a90a8"), hexColor("#ffffff"),
        };
    }
    return Theme{                                    // "dark"
        hexColor("#0b0b12"), hexColor("#12121c"), hexColor("#2a2a3a"),
        hexColor("#1d1d2c"), hexColor("#3a3a52"), hexColor("#2a2a3e"),
        hexColor("#e6e6ec"), hexColor("#9a9ab0"), hexColor("#ffd91a"),
        hexColor("#ffd91a"), hexColor("#101018"), hexColor("#ffe763"),
        hexColor("#565670"), hexColor("#ff6a6a"), hexColor("#2ecc55"),
        hexColor("#26210f"), hexColor("#20263a"), hexColor("#3a3a52"),
        hexColor("#ffd91a"), hexColor("#ff6a3d"), hexColor("#2ecc55"),
        hexColor("#4a5068"), hexColor("#161622"),
    };
}

static const int GRAPH_W = 840, GRAPH_H = 700;   // plot canvas size
static const int PANEL_W = 312;                  // parameter panel width

static const char* DEFAULT_LOCATION = "Sydney, Australia";
static const double DEFAULT_LAT = -33.8688, DEFAULT_LON = 151.2093;
static const int DEFAULT_DAYS = 366;

static const double ZENITH_OFFICIAL = 90.833;
static const double SUN_DIAMETER = 32.0 / 60.0;

// ----------------------------------------------------------------------------
// Dates — a tiny proleptic-Gregorian date type (the slice of datetime.date
// the .py uses): ordinals, weekdays (Mon=0), ISO parsing, day arithmetic.
// ----------------------------------------------------------------------------
struct Date {
    int y = 1970, m = 1, d = 1;
    bool operator==(const Date& o) const { return y == o.y && m == o.m && d == o.d; }
    bool operator!=(const Date& o) const { return !(*this == o); }
    bool operator<(const Date& o) const {
        if (y != o.y) return y < o.y;
        if (m != o.m) return m < o.m;
        return d < o.d;
    }
    bool operator<=(const Date& o) const { return *this < o || *this == o; }
    bool operator>=(const Date& o) const { return !(*this < o); }
};

// Days since 1970-01-01 (Howard Hinnant's civil-date algorithm).
static long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static Date civilFromDays(long z) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp + (mp < 10 ? 3 : -9);
    return Date{ (int)(y + (m <= 2)), (int)m, (int)d };
}

static long dateOrdinal(const Date& dt) { return daysFromCivil(dt.y, dt.m, dt.d); }
static Date dateAddDays(const Date& dt, long days) {
    return civilFromDays(dateOrdinal(dt) + days);
}
static long dateDiff(const Date& a, const Date& b) {           // a - b, days
    return dateOrdinal(a) - dateOrdinal(b);
}
static int dateWeekday(const Date& dt) {                       // Mon=0 .. Sun=6
    long z = dateOrdinal(dt);                                  // 1970-01-01 = Thu
    return (int)(((z + 3) % 7 + 7) % 7);
}
static std::string dateIso(const Date& dt) {
    char buf[16];
    snprintf(buf, sizeof buf, "%04d-%02d-%02d", dt.y, dt.m, dt.d);
    return buf;
}
static bool dateFromIso(const std::string& s, Date& out) {
    int y, m, d;
    char extra;
    if (sscanf(s.c_str(), "%d-%d-%d%c", &y, &m, &d, &extra) != 3) return false;
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    if (m < 1 || m > 12 || d < 1 || d > 31) return false;
    Date probe{ y, m, d };
    if (civilFromDays(dateOrdinal(probe)) != probe) return false;  // e.g. Feb 30
    return (out = probe), true;
}
static Date dateToday() {
    time_t t = time(nullptr);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    return Date{ lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday };
}

// ----------------------------------------------------------------------------
// Solar mathematics (NOAA / Meeus) — line-for-line from the .py.
// ----------------------------------------------------------------------------
static double julianDay(int year, int month, int day) {
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    long a = year >= 0 ? year / 100 : -((-year + 99) / 100);   // floor div
    long b = 2 - a + a / 4;
    return (long)(365.25 * (year + 4716)) + (long)(30.6001 * (month + 1))
           + day + b - 1524.5;
}

static void solarCoords(double jd, double& declOut, double& eotOut) {
    double T = (jd - 2451545.0) / 36525.0;
    double L0 = fmod(280.46646 + T * (36000.76983 + T * 0.0003032), 360.0);
    if (L0 < 0) L0 += 360.0;
    double M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
    double e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
    double Mr = rad(M);
    double C = std::sin(Mr) * (1.914602 - T * (0.004817 + 0.000014 * T))
               + std::sin(2 * Mr) * (0.019993 - 0.000101 * T)
               + std::sin(3 * Mr) * 0.000289;
    double omega = rad(125.04 - 1934.136 * T);
    double lam = L0 + C - 0.00569 - 0.00478 * std::sin(omega);
    double eps = (23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059
                  - T * 0.001813))) / 60.0) / 60.0)
                 + 0.00256 * std::cos(omega);
    declOut = deg(std::asin(std::sin(rad(eps)) * std::sin(rad(lam))));
    double yv = std::tan(rad(eps / 2.0));
    yv *= yv;
    double L0r = rad(L0);
    eotOut = 4.0 * deg(yv * std::sin(2.0 * L0r)
                       - 2.0 * e * std::sin(Mr)
                       + 4.0 * e * yv * std::sin(Mr) * std::cos(2.0 * L0r)
                       - 0.5 * yv * yv * std::sin(4.0 * L0r)
                       - 1.25 * e * e * std::sin(2.0 * Mr));
}

// Half the daylight arc in degrees; nullopt with flag "night"/"day" when the
// sun stays below / above the horizon all day.
static std::optional<double> hourAngleDeg(double lat, double decl,
                                          double zenith, std::string& flag) {
    double latr = rad(lat), dr = rad(decl);
    double cosH = (std::cos(rad(zenith)) - std::sin(latr) * std::sin(dr))
                  / (std::cos(latr) * std::cos(dr));
    if (cosH > 1.0) { flag = "night"; return std::nullopt; }
    if (cosH < -1.0) { flag = "day"; return std::nullopt; }
    flag.clear();
    return deg(std::acos(cosH));
}

// ----------------------------------------------------------------------------
// Skyline (raised horizon) support.
// ----------------------------------------------------------------------------
static double zenithForHorizon(double h) {
    if (h == 0.0) return ZENITH_OFFICIAL;
    double hc = std::max(h, -1.5);
    double refr = 1.0 / std::tan(rad(hc + 7.31 / (hc + 4.4)));   // arcmin
    return 90.0 - h + (refr + 16.0) / 60.0;
}

static double sunAzimuth(double lat, double decl, double haDeg) {
    double latr = rad(lat), dr = rad(decl), hr = rad(haDeg);
    double sinAlt = std::sin(latr) * std::sin(dr)
                    + std::cos(latr) * std::cos(dr) * std::cos(hr);
    double cosAlt = std::sqrt(std::max(1e-9, 1.0 - sinAlt * sinAlt));
    double cosAz = (std::sin(dr) - sinAlt * std::sin(latr))
                   / std::max(1e-9, cosAlt * std::cos(latr));
    double az = deg(std::acos(std::max(-1.0, std::min(1.0, cosAz))));
    return std::sin(hr) > 0 ? 360.0 - az : az;
}

using HorizonProfile = std::vector<std::pair<double, double>>;  // (az, alt)

static double horizonAlt(const HorizonProfile& profile, double az) {
    if (profile.empty()) return 0.0;
    if (profile.size() == 1) return profile[0].second;
    az = fmod(az, 360.0);
    if (az < 0) az += 360.0;
    std::pair<double, double> lo = profile.back(), hi = profile.front();
    for (size_t i = 0; i + 1 < profile.size(); i++)
        if (profile[i].first <= az && az <= profile[i + 1].first) {
            lo = profile[i];
            hi = profile[i + 1];
            break;
        }
    double span = fmod(hi.first - lo.first + 360.0, 360.0);
    if (span == 0.0) return lo.second;
    double frac = fmod(az - lo.first + 360.0, 360.0) / span;
    return lo.second + (hi.second - lo.second) * frac;
}

// ----------------------------------------------------------------------------
// sun_events — the heart: per-event fixed-point refinement (see the .py).
// ----------------------------------------------------------------------------
struct SunEvents {
    std::optional<double> rise, set;           // minutes after local midnight
    std::optional<double> rise_az, set_az;     // deg clockwise from true north
    std::optional<double> rise_dur, set_dur;   // disc crossing time, minutes
    double noon = 720.0;
    std::string polar;                          // "", "night", "day"
    double daylight = 0.0;                      // minutes
    bool operator==(const SunEvents& o) const {
        return rise == o.rise && set == o.set && rise_az == o.rise_az
               && set_az == o.set_az && rise_dur == o.rise_dur
               && set_dur == o.set_dur && noon == o.noon && polar == o.polar
               && daylight == o.daylight;
    }
};

static SunEvents sunEvents(const Date& date, double lat, double lon,
                           double tzHours,
                           const HorizonProfile* horizon = nullptr) {
    static const HorizonProfile kFlat;
    const HorizonProfile& prof = horizon ? *horizon : kFlat;
    double jd0 = julianDay(date.y, date.m, date.d);

    auto jdAt = [&](double minutesLocal) {
        return jd0 + (minutesLocal - tzHours * 60.0) / 1440.0;
    };
    auto clockNoon = [&](double eot) {
        return 720.0 - 4.0 * lon + tzHours * 60.0 - eot;
    };

    double noon = 720.0;
    for (int i = 0; i < 2; i++) {
        double decl, eot;
        solarCoords(jdAt(noon), decl, eot);
        noon = clockNoon(eot);
    }

    SunEvents out;
    out.noon = noon;
    double decl0, eot0;
    solarCoords(jdAt(noon), decl0, eot0);

    // One crossing (sign -1 = morning, +1 = evening) with the sun's center
    // dz degrees above the upper-limb zenith.
    auto solve = [&](double sign, double dz, std::optional<double>& tOut,
                     std::optional<double>& azOut, std::string& flagOut) {
        double decl = decl0, eot = eot0;
        double h = horizonAlt(prof, sign < 0 ? 90.0 : 270.0);
        double t = 0, az = 0;
        bool have = false;
        for (int i = 0; i < 3; i++) {
            std::string flag;
            std::optional<double> ha =
                hourAngleDeg(lat, decl, zenithForHorizon(h) - dz, flag);
            if (!ha) {
                tOut.reset();
                azOut.reset();
                flagOut = flag;
                return;
            }
            az = sunAzimuth(lat, decl, sign * *ha);
            h = horizonAlt(prof, az);
            t = clockNoon(eot) + sign * 4.0 * *ha;
            solarCoords(jdAt(t), decl, eot);
            have = true;
        }
        (void)have;
        tOut = t;
        azOut = az;
        flagOut.clear();
    };

    std::vector<std::string> flags;
    const struct { const char* key; double sign; } EVENTS[2] = {
        { "rise", -1.0 }, { "set", 1.0 }
    };
    for (auto& evd : EVENTS) {
        std::optional<double> t, az;
        std::string flag;
        solve(evd.sign, 0.0, t, az, flag);
        std::optional<double>* dur =
            evd.sign < 0 ? &out.rise_dur : &out.set_dur;
        if (!t) {
            flags.push_back(flag);
        } else {
            std::optional<double> tFull, azFull;
            std::string f2;
            solve(evd.sign, SUN_DIAMETER, tFull, azFull, f2);
            if (tFull) *dur = evd.sign * (*t - *tFull);
        }
        if (evd.sign < 0) { out.rise = t; out.rise_az = az; }
        else { out.set = t; out.set_az = az; }
    }

    if (!out.rise || !out.set) {
        std::string flag = flags.empty() ? "night" : flags[0];
        out.rise.reset(); out.set.reset();
        out.rise_az.reset(); out.set_az.reset();
        out.rise_dur.reset(); out.set_dur.reset();
        out.polar = flag;
        out.daylight = flag == "night" ? 0.0 : 1440.0;
    } else {
        out.daylight = *out.set - *out.rise;
    }
    return out;
}

// ----------------------------------------------------------------------------
// Geomagnetism — the embedded WMM2025 coefficient file (verbatim; see the .py
// header comment for provenance) evaluated per the WMM technical report.
// ----------------------------------------------------------------------------
static const char* WMM_COF =
R"COF(    2025.0            WMM-2025     11/13/2024
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
)COF"
;

struct WmmModel {
    std::string name;
    double epoch = 2025.0;
    int nmax = 12;
    // coeffs[n][m] = {g, h, gdot, hdot}
    double c[14][14][4] = {};
};

static const WmmModel& wmmModel() {
    static WmmModel model;
    static bool built = false;
    if (built) return model;
    std::istringstream in(WMM_COF);
    std::string line;
    int nmax = 0;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::vector<std::string> tok;
        std::string t;
        while (ls >> t) tok.push_back(t);
        if (tok.size() == 3 && tok[0].find('.') != std::string::npos) {
            model.epoch = atof(tok[0].c_str());
            model.name = tok[1];
            std::string clean;
            for (char ch : model.name) if (ch != '-') clean += ch;
            model.name = clean;
        } else if (tok.size() == 6) {
            int n = atoi(tok[0].c_str()), m = atoi(tok[1].c_str());
            if (n >= 0 && n < 14 && m >= 0 && m < 14) {
                for (int i = 0; i < 4; i++)
                    model.c[n][m][i] = atof(tok[2 + i].c_str());
                nmax = std::max(nmax, n);
            }
        }
    }
    model.nmax = nmax;
    built = true;
    return model;
}

static int wmmCoeffCount() {                     // sum(n+1 for n in 1..nmax)
    const WmmModel& mdl = wmmModel();
    int count = 0;
    for (int n = 1; n <= mdl.nmax; n++) count += n + 1;
    return count;
}

static double wmmDeclination(double lat, double lon, double dyear,
                             double altKm) {
    // Small per-run cache — the .py lru_caches this per (place, day).
    struct Key {
        double lat, lon, dyear, altKm;
        bool operator<(const Key& o) const {
            if (lat != o.lat) return lat < o.lat;
            if (lon != o.lon) return lon < o.lon;
            if (dyear != o.dyear) return dyear < o.dyear;
            return altKm < o.altKm;
        }
    };
    static std::map<Key, double> cache;
    Key key{ lat, lon, dyear, altKm };
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    const WmmModel& mdl = wmmModel();
    int nmax = mdl.nmax;
    double t = dyear - mdl.epoch;

    // Geodetic -> geocentric (WGS84) — skipping this fails the test vectors.
    double a = 6378.137, f = 1.0 / 298.257223563;
    double e2 = f * (2.0 - f);
    double phi = rad(std::max(-89.995, std::min(89.995, lat)));
    double lam = rad(lon);
    double sp = std::sin(phi), cp = std::cos(phi);
    double rc = a / std::sqrt(1.0 - e2 * sp * sp);
    double px = (rc + altKm) * cp;
    double pz = (rc * (1.0 - e2) + altKm) * sp;
    double r = std::hypot(px, pz);
    double phig = std::atan2(pz, px);
    double mu = std::sin(phig), cmu = std::cos(phig);

    // Schmidt semi-normalized associated Legendre values + derivatives.
    double P[14][14] = {}, dP[14][14] = {};
    P[0][0] = 1.0;
    for (int n = 1; n <= nmax; n++) {
        for (int m = 0; m <= n; m++) {
            if (n == m) {
                double k = n == 1 ? 1.0
                                  : std::sqrt((2.0 * n - 1.0) / (2.0 * n));
                P[n][n] = k * cmu * P[n - 1][n - 1];
                dP[n][n] = k * (cmu * dP[n - 1][n - 1] - mu * P[n - 1][n - 1]);
            } else {
                double norm = std::sqrt((double)(n * n - m * m));
                double c1 = (2.0 * n - 1.0) / norm;
                double c2 = std::sqrt((n - 1.0) * (n - 1.0) - m * m) / norm;
                double pm2 = n - 2 >= m ? P[n - 2][m] : 0.0;
                double dpm2 = n - 2 >= m ? dP[n - 2][m] : 0.0;
                P[n][m] = c1 * mu * P[n - 1][m] - c2 * pm2;
                dP[n][m] = c1 * (mu * dP[n - 1][m] + cmu * P[n - 1][m])
                           - c2 * dpm2;
            }
        }
    }

    double cosl[14], sinl[14];
    for (int m = 0; m <= nmax; m++) {
        cosl[m] = std::cos(m * lam);
        sinl[m] = std::sin(m * lam);
    }
    double ar = 6371.2 / r;
    double arn = ar * ar * ar;                  // (a/r)^(n+2) at n = 1
    double bx = 0, by = 0, bz = 0;
    for (int n = 1; n <= nmax; n++) {
        for (int m = 0; m <= n; m++) {
            double g = mdl.c[n][m][0] + t * mdl.c[n][m][2];
            double h = mdl.c[n][m][1] + t * mdl.c[n][m][3];
            double both = g * cosl[m] + h * sinl[m];
            bx -= arn * both * dP[n][m];
            by += arn * m * (g * sinl[m] - h * cosl[m]) * P[n][m];
            bz -= arn * (n + 1.0) * both * P[n][m];
        }
        arn *= ar;
    }
    by /= cmu;

    double psi = phig - phi;
    double north = bx * std::cos(psi) - bz * std::sin(psi);
    double result = deg(std::atan2(by, north));
    if (cache.size() < 8192) cache[key] = result;
    return result;
}

static double decimalYear(const Date& date) {
    long y0 = dateOrdinal(Date{ date.y, 1, 1 });
    long daysInYear = dateOrdinal(Date{ date.y + 1, 1, 1 }) - y0;
    return date.y + (double)(dateOrdinal(date) - y0) / daysInYear;
}

static double magneticDeclination(double lat, double lon, const Date& date,
                                  double altKm = 0.0) {
    return wmmDeclination(lat, lon, decimalYear(date), altKm);
}

static double trueToMagnetic(double bearing, double lat, double lon,
                             const Date& date) {
    double v = fmod(bearing - magneticDeclination(lat, lon, date), 360.0);
    return v < 0 ? v + 360.0 : v;
}

static double magneticToTrue(double bearing, double lat, double lon,
                             const Date& date) {
    double v = fmod(bearing + magneticDeclination(lat, lon, date), 360.0);
    return v < 0 ? v + 360.0 : v;
}

// ----------------------------------------------------------------------------
// System time zone: the OS's UTC offset (minutes) at local noon of a date.
// ----------------------------------------------------------------------------
static int systemOffsetMinutes(const Date& date);   // per-platform, below
static std::string systemZoneNames();               // "AEST / AEDT" etc.

// ----------------------------------------------------------------------------
// Manual daylight-saving rules — (ordinal, weekday, month), ordinal -1=last.
// ----------------------------------------------------------------------------
struct Rule {
    int ordinal = 1, weekday = 6, month = 10;
    bool operator==(const Rule& o) const {
        return ordinal == o.ordinal && weekday == o.weekday && month == o.month;
    }
};

static const char* ORD_NAMES_LIST[5] = { "1st", "2nd", "3rd", "4th", "last" };
static const int ORD_VALUES_LIST[5] = { 1, 2, 3, 4, -1 };
static const char* DAY_NAMES[7] = { "Mon", "Tue", "Wed", "Thu", "Fri",
                                    "Sat", "Sun" };
static const char* MONTH_NAMES[12] = { "Jan", "Feb", "Mar", "Apr", "May",
                                       "Jun", "Jul", "Aug", "Sep", "Oct",
                                       "Nov", "Dec" };

static std::string ordName(int ordinal) {
    for (int i = 0; i < 5; i++)
        if (ORD_VALUES_LIST[i] == ordinal) return ORD_NAMES_LIST[i];
    return "1st";
}

static std::string fmtRule(const Rule& r) {
    return ordName(r.ordinal) + " " + DAY_NAMES[r.weekday] + " of "
           + MONTH_NAMES[r.month - 1];
}

static Date nthWeekdayDate(int year, int month, int ordinal, int weekday) {
    if (ordinal == -1) {
        Date nxt{ year + month / 12, month % 12 + 1, 1 };
        Date last = dateAddDays(nxt, -1);
        return dateAddDays(last, -(((dateWeekday(last) - weekday) % 7 + 7) % 7));
    }
    Date first{ year, month, 1 };
    return dateAddDays(first, ((weekday - dateWeekday(first)) % 7 + 7) % 7
                              + 7L * (ordinal - 1));
}

static bool dstActive(const Date& date, const Rule& startRule,
                      const Rule& endRule) {
    Date s = nthWeekdayDate(date.y, startRule.month, startRule.ordinal,
                            startRule.weekday);
    Date e = nthWeekdayDate(date.y, endRule.month, endRule.ordinal,
                            endRule.weekday);
    if (s <= e) return s <= date && date < e;    // northern style
    return date >= s || date < e;                // southern: wraps New Year
}

// ----------------------------------------------------------------------------
// compute_rows — the almanac table.
// ----------------------------------------------------------------------------
struct DstSpec {
    double hours = 11.0;
    Rule start, end;
};

struct Row {
    Date date;
    std::optional<int> rise, set;              // seconds after local midnight
    std::optional<int> rise_dur, set_dur;      // crossing, whole seconds
    std::optional<double> rise_az, set_az;     // true bearings, 0.1-rounded
    std::optional<double> rise_mag, set_mag;   // magnetic bearings
    int day = 0;                               // daylight seconds
    int off = 0;                               // UTC offset minutes
    bool operator==(const Row& o) const {
        return date == o.date && rise == o.rise && set == o.set
               && rise_dur == o.rise_dur && set_dur == o.set_dur
               && rise_az == o.rise_az && set_az == o.set_az
               && rise_mag == o.rise_mag && set_mag == o.set_mag
               && day == o.day && off == o.off;
    }
};

static double round1(double v) { return std::floor(v * 10.0 + 0.5) / 10.0; }

static std::vector<Row> computeRows(double lat, double lon, const Date& start,
                                    int days, const std::string& tzMode,
                                    double fixedHours,
                                    const DstSpec* dst = nullptr,
                                    const HorizonProfile* horizon = nullptr) {
    std::vector<Row> rows;
    rows.reserve(days);
    for (int i = 0; i < days; i++) {
        Date date = dateAddDays(start, i);
        int offMin;
        if (tzMode == "system")
            offMin = systemOffsetMinutes(date);
        else if (dst && dstActive(date, dst->start, dst->end))
            offMin = (int)std::lround(dst->hours * 60.0);
        else
            offMin = (int)std::lround(fixedHours * 60.0);
        SunEvents ev = sunEvents(date, lat, lon, offMin / 60.0, horizon);
        Row r;
        r.date = date;
        r.off = offMin;
        if (!ev.polar.empty()) {
            r.day = ev.polar == "night" ? 0 : 86400;
        } else {
            r.rise = (int)std::lround(*ev.rise * 60.0);
            r.set = (int)std::lround(*ev.set * 60.0);
            if (ev.rise_dur) r.rise_dur = (int)std::lround(*ev.rise_dur * 60.0);
            if (ev.set_dur) r.set_dur = (int)std::lround(*ev.set_dur * 60.0);
            r.rise_az = round1(*ev.rise_az);
            r.set_az = round1(*ev.set_az);
            // % 360 folds a possible round(359.95+) = 360.0 back to 0.0.
            r.rise_mag = fmod(round1(trueToMagnetic(*ev.rise_az, lat, lon,
                                                    date)), 360.0);
            r.set_mag = fmod(round1(trueToMagnetic(*ev.set_az, lat, lon,
                                                   date)), 360.0);
            r.day = *r.set - *r.rise;
        }
        rows.push_back(r);
    }
    return rows;
}

// ----------------------------------------------------------------------------
// Formatting / parsing helpers — exact ports; parse errors throw ValueError.
// ----------------------------------------------------------------------------
struct ValueError {
    std::string msg;
    explicit ValueError(std::string m) : msg(std::move(m)) {}
};

static std::string fmtClock(const std::optional<int>& sec) {
    if (!sec) return "--:--:--";
    int s = ((*sec % 86400) + 86400) % 86400;
    char buf[16];
    snprintf(buf, sizeof buf, "%02d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    return buf;
}

static std::string fmtSpan(int sec) {          // 24:00:00 stays 24:00:00
    char buf[16];
    snprintf(buf, sizeof buf, "%02d:%02d:%02d", sec / 3600, sec / 60 % 60,
             sec % 60);
    return buf;
}

static std::string fmtDur(const std::optional<int>& sec) {
    if (!sec) return "--:--";
    int s = *sec;
    char buf[16];
    if (s >= 3600)
        snprintf(buf, sizeof buf, "%d:%02d:%02d", s / 3600, s / 60 % 60,
                 s % 60);
    else
        snprintf(buf, sizeof buf, "%02d:%02d", s / 60, s % 60);
    return buf;
}

static std::optional<int> parseDur(const std::string& s) {
    if (s.rfind("--", 0) == 0) return std::nullopt;
    int sec = 0;
    std::istringstream in(s);
    std::string p;
    while (std::getline(in, p, ':')) sec = sec * 60 + atoi(p.c_str());
    return sec;
}

static std::string fmtOffset(int minutes) {
    char buf[16];
    int m = std::abs(minutes);
    snprintf(buf, sizeof buf, "%c%02d:%02d", minutes >= 0 ? '+' : '-',
             m / 60, m % 60);
    return buf;
}

static std::string fmtAz(const std::optional<double>& degv) {
    if (!degv) return "   ---";
    char buf[16];
    snprintf(buf, sizeof buf, "%6.1f", *degv);
    return buf;
}

static std::optional<double> parseAz(const std::string& s) {
    if (s == "---") return std::nullopt;
    return atof(s.c_str());
}

static std::optional<int> parseClockStr(const std::string& s) {
    if (s.rfind("--", 0) == 0) return std::nullopt;
    int h, m, sec;
    if (sscanf(s.c_str(), "%d:%d:%d", &h, &m, &sec) != 3)
        throw ValueError("bad clock: " + s);
    return h * 3600 + m * 60 + sec;
}

static int parseOffsetStr(const std::string& s) {
    if (s.empty()) throw ValueError("empty offset");
    int sign = s[0] == '-' ? -1 : 1;
    std::string body = s;
    while (!body.empty() && (body[0] == '+' || body[0] == '-'))
        body.erase(0, 1);
    int h, m;
    if (sscanf(body.c_str(), "%d:%d", &h, &m) != 2)
        throw ValueError("bad offset: " + s);
    return sign * (h * 60 + m);
}

// Web-pasted numbers arrive with Unicode minus signs, degree marks, hard
// spaces and invisible characters that strtod rejects even though they look
// identical on screen. Normalize the known offenders (UTF-8 sequences).
static std::string cleanNumeric(const std::string& s) {
    struct Sub { const char* seq; const char* repl; };
    static const Sub SUBS[] = {
        { "\xE2\x88\x92", "-" },               // U+2212 true minus
        { "\xE2\x80\x90", "-" },               // U+2010 hyphen
        { "\xE2\x80\x91", "-" },               // U+2011 non-breaking hyphen
        { "\xE2\x80\x92", "-" },               // U+2012 figure dash
        { "\xE2\x80\x93", "-" },               // U+2013 en dash
        { "\xE2\x80\x94", "-" },               // U+2014 em dash
        { "\xC2\xA0", " " },                   // U+00A0 no-break space
        { "\xE2\x80\x87", " " },               // U+2007 figure space
        { "\xE2\x80\x89", " " },               // U+2009 thin space
        { "\xE2\x80\xAF", " " },               // U+202F narrow no-break
        { "\xE2\x80\x8B", "" },                // U+200B zero-width space
        { "\xEF\xBB\xBF", "" },                // U+FEFF BOM
    };
    std::string out = s;
    for (const Sub& sub : SUBS) {
        size_t pos = 0, n = strlen(sub.seq);
        while ((pos = out.find(sub.seq, pos)) != std::string::npos) {
            out.replace(pos, n, sub.repl);
            pos += strlen(sub.repl);
        }
    }
    size_t b = out.find_first_not_of(" \t\r\n");
    size_t e = out.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? "" : out.substr(b, e - b + 1);
}

static double parseFloatStrict(const std::string& s) {
    if (s.empty()) throw ValueError("empty number");
    char* end = nullptr;
    double v = strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) throw ValueError("bad number: " + s);
    return v;
}

// parse_angle: decimals, web-pasted text, hemisphere letters, DMS.
static double parseAngle(const std::string& raw) {
    std::string s = cleanNumeric(raw);
    double hemi = 0.0;
    // Trailing hemisphere letter.
    if (!s.empty()) {
        size_t e = s.find_last_not_of(" \t");
        if (e != std::string::npos && strchr("NSEWnsew", s[e])) {
            char c = (char)toupper((unsigned char)s[e]);
            hemi = (c == 'S' || c == 'W') ? -1.0 : 1.0;
            s = s.substr(0, e);
            size_t e2 = s.find_last_not_of(" \t");
            s = e2 == std::string::npos ? "" : s.substr(0, e2 + 1);
        } else if (strchr("NSEWnsew", s[0])) {
            // Leading letter, when followed by a digit / sign / dot.
            size_t i = 1;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
            if (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '+'
                                 || s[i] == '.' || s[i] == '-')) {
                char c = (char)toupper((unsigned char)s[0]);
                hemi = (c == 'S' || c == 'W') ? -1.0 : 1.0;
                s = s.substr(i);
            }
        }
    }
    if (s.empty()) throw ValueError("empty angle");
    bool neg = s[0] == '-';
    // Strip leading signs, then split on whitespace / degree-minute-second
    // marks (the UTF-8 sequences Tk users paste).
    size_t start = 0;
    while (start < s.size() && (s[start] == '+' || s[start] == '-')) start++;
    std::string body = s.substr(start);
    static const char* SEPS[] = {
        "\xC2\xB0",          // ° degree sign
        "\xC2\xBA",          // º masculine ordinal (looks like a degree)
        "\xCB\x9A",          // ˚ ring above
        "\xE2\x80\xB2",      // ′ prime
        "\xE2\x80\xB3",      // ″ double prime
        "\xE2\x80\x99",      // ’ right single quote
        "\xE2\x80\x9D",      // ” right double quote
    };
    for (const char* sep : SEPS) {
        size_t pos = 0, n = strlen(sep);
        while ((pos = body.find(sep, pos)) != std::string::npos)
            body.replace(pos, n, " ");
    }
    for (char& c : body)
        if (c == '\'' || c == '"' || c == '\t') c = ' ';
    std::vector<std::string> parts;
    std::istringstream in(body);
    std::string p;
    while (in >> p) parts.push_back(p);
    if (parts.empty() || parts.size() > 3) throw ValueError("not an angle");
    double degv = parseFloatStrict(parts[0]);
    double minutes = parts.size() > 1 ? parseFloatStrict(parts[1]) : 0.0;
    double seconds = parts.size() > 2 ? parseFloatStrict(parts[2]) : 0.0;
    if (!(0.0 <= minutes && minutes < 60.0 && 0.0 <= seconds && seconds < 60.0))
        throw ValueError("minutes/seconds out of range");
    double value = degv + minutes / 60.0 + seconds / 3600.0;
    if (hemi != 0.0) return std::fabs(value) * hemi;
    return neg ? -value : value;
}

static double parseTzHours(const std::string& raw) {
    std::string s = cleanNumeric(raw);
    if (s.empty()) throw ValueError("empty offset");
    double sign = s[0] == '-' ? -1.0 : 1.0;
    std::string body = s;
    while (!body.empty() && (body[0] == '+' || body[0] == '-'))
        body.erase(0, 1);
    double val;
    size_t colon = body.find(':');
    if (colon != std::string::npos) {
        std::string hs = body.substr(0, colon), ms = body.substr(colon + 1);
        char* e1 = nullptr, * e2 = nullptr;
        long h = strtol(hs.c_str(), &e1, 10);
        long m = strtol(ms.c_str(), &e2, 10);
        if (hs.empty() || ms.empty() || *e1 || *e2)
            throw ValueError("bad offset");
        val = h + m / 60.0;
    } else {
        val = parseFloatStrict(body);
    }
    val *= sign;
    if (!(-14.0 <= val && val <= 14.0)) throw ValueError("offset out of range");
    return val;
}

// Skyline entry -> profile ('' / '0' = flat; one number = uniform hills;
// 'az:alt, ...' = a profile).
static HorizonProfile parseHorizon(const std::string& raw) {
    std::string s = cleanNumeric(raw);
    if (s.empty()) return {};
    std::vector<std::string> parts;
    std::istringstream in(s);
    std::string piece;
    while (std::getline(in, piece, ',')) {
        size_t b = piece.find_first_not_of(" \t");
        size_t e = piece.find_last_not_of(" \t");
        if (b != std::string::npos) parts.push_back(piece.substr(b, e - b + 1));
    }
    if (parts.empty()) return {};
    HorizonProfile profile;
    for (const std::string& part : parts) {
        double az, alt;
        size_t colon = part.find(':');
        if (colon != std::string::npos) {
            az = parseFloatStrict(part.substr(0, colon));
            alt = parseFloatStrict(part.substr(colon + 1));
            if (!(0.0 <= az && az <= 360.0))
                throw ValueError("azimuth out of range");
        } else if (parts.size() == 1) {
            az = 0.0;
            alt = parseFloatStrict(part);
        } else {
            throw ValueError("lists must use az:alt pairs");
        }
        if (!(-5.0 <= alt && alt <= 60.0))
            throw ValueError("altitude out of range");
        profile.emplace_back(fmod(az, 360.0), alt);
    }
    if (profile.size() == 1 && profile[0].second == 0.0) return {};
    std::sort(profile.begin(), profile.end());
    return profile;
}

// ----------------------------------------------------------------------------
// The text file: assumptions header + one aligned row per day.
// ----------------------------------------------------------------------------
static const char* TABLE_HEADER =
    "# Date        Sunrise   RiseDur  RiseAz RiseMag   Sunset"
    "     SetDur   SetAz  SetMag   Daylight   UTCoff";

struct Meta {
    std::string generated, location, tz_desc, horizon_desc, mag_desc;
    std::optional<double> lat, lon;
};

static std::string buildTableText(const std::vector<Row>& rows,
                                  const Meta& meta) {
    std::vector<std::string> out;
    auto a = [&](const std::string& s) { out.push_back(s); };
    char buf[256];
    a("# Sun2Set almanac - sunrise, sunset and day length");
    a("# Generated : " + meta.generated);
    if (!meta.location.empty()) a("# Location  : " + meta.location);
    snprintf(buf, sizeof buf, "# Latitude  : %.6f  (degrees, + = north)",
             meta.lat.value_or(0.0));
    a(buf);
    snprintf(buf, sizeof buf, "# Longitude : %.6f  (degrees, + = east)",
             meta.lon.value_or(0.0));
    a(buf);
    a("# Time zone : " + meta.tz_desc);
    a("# Horizon   : " + (meta.horizon_desc.empty()
                          ? "flat 0.0 deg (open astronomical horizon)"
                          : meta.horizon_desc));
    snprintf(buf, sizeof buf, "# Range     : %d days starting %s",
             (int)rows.size(), dateIso(rows[0].date).c_str());
    a(buf);
    a("# Algorithm : NOAA solar equations (Meeus); sunrise/sunset when the");
    a("#             sun's upper limb crosses the horizon stated above");
    a("#             (flat = zenith 90.833 deg: 34' refraction + 16' solar");
    a("#             radius; raised skylines re-evaluate refraction at the");
    a("#             hill altitude). Typically within 1-2 minutes of");
    a("#             published almanac values (real refraction varies).");
    a("# Times     : local wall clock HH:MM:SS; '--:--:--' means the sun");
    a("#             never rises / never sets that day (Daylight 00 / 24 h).");
    a("# Azimuths  : RiseAz/SetAz = the sun's bearing at each event, degrees");
    a("#             clockwise from TRUE north (N=0, E=90, S=180, W=270);");
    a("#             RiseMag/SetMag = the same bearings as read on a magnetic");
    a("#             compass; '---' on no-event days.");
    a("# Durations : RiseDur/SetDur = how long the disc takes to cross the");
    a("#             horizon line (upper limb first touches -> lower limb");
    a("#             clears, reversed at sunset), as MM:SS; '--:--' when");
    a("#             there is no event or the disc never fully clears the");
    a("#             line (grazing polar-circle days).");
    a("# Magnetic  : " + (meta.mag_desc.empty()
                          ? "declination from the embedded WMM model"
                          : meta.mag_desc));
    a("#");
    a(TABLE_HEADER);
    for (const Row& r : rows) {
        snprintf(buf, sizeof buf, "%s    %s  %7s  %s  %s   %s  %7s  %s  %s"
                 "   %s   %s",
                 dateIso(r.date).c_str(), fmtClock(r.rise).c_str(),
                 fmtDur(r.rise_dur).c_str(), fmtAz(r.rise_az).c_str(),
                 fmtAz(r.rise_mag).c_str(), fmtClock(r.set).c_str(),
                 fmtDur(r.set_dur).c_str(), fmtAz(r.set_az).c_str(),
                 fmtAz(r.set_mag).c_str(), fmtSpan(r.day).c_str(),
                 fmtOffset(r.off).c_str());
        a(buf);
    }
    a("");
    std::string joined;
    for (size_t i = 0; i < out.size(); i++) {
        joined += out[i];
        if (i + 1 < out.size()) joined += "\n";
    }
    return joined;
}

static std::optional<Rule> ruleFromText(const std::string& textRaw) {
    std::string text = textRaw;
    size_t b = text.find_first_not_of(" \t");
    size_t e = text.find_last_not_of(" \t");
    if (b == std::string::npos) return std::nullopt;
    text = text.substr(b, e - b + 1);
    std::istringstream in(text);
    std::string ord, day, of, mon;
    if (!(in >> ord >> day >> of >> mon) || of != "of") return std::nullopt;
    std::string rest;
    if (in >> rest) return std::nullopt;
    Rule r;
    bool okOrd = false, okDay = false, okMon = false;
    for (int i = 0; i < 5; i++)
        if (ord == ORD_NAMES_LIST[i]) { r.ordinal = ORD_VALUES_LIST[i]; okOrd = true; }
    for (int i = 0; i < 7; i++)
        if (day == DAY_NAMES[i]) { r.weekday = i; okDay = true; }
    for (int i = 0; i < 12; i++)
        if (mon == MONTH_NAMES[i]) { r.month = i + 1; okMon = true; }
    if (!okOrd || !okDay || !okMon) return std::nullopt;
    return r;
}

struct TzSettings {
    std::string tz_mode;                       // "system" | "fixed"
    double fixed_hours = 0.0;
    bool dst_enabled = false;
    double dst_hours = 0.0;
    Rule dst_start, dst_end;
};

// Invert compute()'s '# Time zone :' header line (keep in sync with it).
static std::optional<TzSettings> parseTzDesc(const std::string& descRaw) {
    std::string d = descRaw.substr(0, descRaw.find(';'));
    size_t b = d.find_first_not_of(" \t");
    size_t e = d.find_last_not_of(" \t");
    d = b == std::string::npos ? "" : d.substr(b, e - b + 1);
    if (d.rfind("System local zone", 0) == 0) {
        TzSettings out;
        out.tz_mode = "system";
        return out;
    }
    static const char* PREFIX = "Fixed UTC offset ";
    if (d.rfind(PREFIX, 0) != 0) return std::nullopt;
    std::string rest = d.substr(strlen(PREFIX));
    // The offset token: [+-]HH:MM
    if (rest.size() < 6 || (rest[0] != '+' && rest[0] != '-'))
        return std::nullopt;
    std::string off = rest.substr(0, 6);
    if (!isdigit((unsigned char)off[1]) || !isdigit((unsigned char)off[2])
        || off[3] != ':' || !isdigit((unsigned char)off[4])
        || !isdigit((unsigned char)off[5]))
        return std::nullopt;
    TzSettings out;
    out.tz_mode = "fixed";
    out.fixed_hours = parseOffsetStr(off) / 60.0;
    out.dst_enabled = false;
    rest = rest.substr(6);
    static const char* WITH = " with DST ";
    if (rest.rfind(WITH, 0) == 0) {
        std::string tail = rest.substr(strlen(WITH));
        if (tail.size() >= 6 && (tail[0] == '+' || tail[0] == '-')) {
            std::string dstOff = tail.substr(0, 6);
            tail = tail.substr(6);
            static const char* FROM = " from ";
            size_t toPos = tail.find(" to ");
            if (tail.rfind(FROM, 0) == 0 && toPos != std::string::npos) {
                std::string startTxt = tail.substr(strlen(FROM),
                                                   toPos - strlen(FROM));
                std::string endTxt = tail.substr(toPos + 4);
                auto start = ruleFromText(startTxt);
                auto end = ruleFromText(endTxt);
                if (start && end) {
                    out.dst_enabled = true;
                    out.dst_hours = parseOffsetStr(dstOff) / 60.0;
                    out.dst_start = *start;
                    out.dst_end = *end;
                }
            }
        }
    }
    return out;
}

// Invert compute()'s '# Horizon :' header line (keep in sync with it).
static std::optional<std::string> parseHorizonDesc(const std::string& descRaw) {
    std::string d = descRaw;
    size_t b = d.find_first_not_of(" \t");
    size_t e = d.find_last_not_of(" \t");
    d = b == std::string::npos ? "" : d.substr(b, e - b + 1);
    if (d.rfind("flat", 0) == 0) return std::string("0");
    static const char* UNI = "uniform ";
    if (d.rfind(UNI, 0) == 0) {
        std::string rest = d.substr(strlen(UNI));
        size_t sp = rest.find(" deg");
        if (sp != std::string::npos) {
            std::string num = rest.substr(0, sp);
            char* end2 = nullptr;
            strtod(num.c_str(), &end2);
            if (!num.empty() && end2 == num.c_str() + num.size()) return num;
        }
        return std::nullopt;
    }
    static const char* PROF = "az:alt profile ";
    if (d.rfind(PROF, 0) == 0) {
        std::string rest = d.substr(strlen(PROF));
        size_t paren = rest.find(" (deg");
        if (paren != std::string::npos) {
            std::string body = rest.substr(0, paren);
            size_t b2 = body.find_first_not_of(" \t");
            size_t e2 = body.find_last_not_of(" \t");
            if (b2 != std::string::npos)
                return body.substr(b2, e2 - b2 + 1);
        }
    }
    return std::nullopt;
}

// Inverse of buildTableText — tolerant of unknown '#' header lines and of
// the 9/7/5-column layouts of older files.
static std::vector<Row> parseTableText(const std::string& text, Meta& meta) {
    meta = Meta{};
    std::vector<Row> rows;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        std::string s = line.substr(b);
        if (s[0] == '#') {
            std::string body = s;
            size_t h = body.find_first_not_of("#");
            body = h == std::string::npos ? "" : body.substr(h);
            size_t bb = body.find_first_not_of(" \t");
            body = bb == std::string::npos ? "" : body.substr(bb);
            size_t colon = body.find(':');
            if (colon == std::string::npos) continue;
            std::string key = body.substr(0, colon);
            std::string val = body.substr(colon + 1);
            size_t ke = key.find_last_not_of(" \t");
            key = ke == std::string::npos ? "" : key.substr(0, ke + 1);
            for (char& c : key) c = (char)tolower((unsigned char)c);
            size_t vb = val.find_first_not_of(" \t");
            size_t ve = val.find_last_not_of(" \t");
            val = vb == std::string::npos ? "" : val.substr(vb, ve - vb + 1);
            if (key == "location") meta.location = val;
            else if (key == "latitude") meta.lat = atof(val.c_str());
            else if (key == "longitude") meta.lon = atof(val.c_str());
            else if (key == "time zone") meta.tz_desc = val;
            else if (key == "horizon") meta.horizon_desc = val;
            else if (key == "generated") meta.generated = val;
            continue;
        }
        std::vector<std::string> parts;
        std::istringstream ls(s);
        std::string tok;
        while (ls >> tok) parts.push_back(tok);
        std::string date_s, rise_s, rdur_s, raz_s, rmag_s, set_s, sdur_s,
                    saz_s, smag_s, day_s, off_s;
        if (parts.size() == 11) {
            date_s = parts[0]; rise_s = parts[1]; rdur_s = parts[2];
            raz_s = parts[3]; rmag_s = parts[4]; set_s = parts[5];
            sdur_s = parts[6]; saz_s = parts[7]; smag_s = parts[8];
            day_s = parts[9]; off_s = parts[10];
        } else if (parts.size() == 9) {        // pre-duration files
            date_s = parts[0]; rise_s = parts[1]; raz_s = parts[2];
            rmag_s = parts[3]; set_s = parts[4]; saz_s = parts[5];
            smag_s = parts[6]; day_s = parts[7]; off_s = parts[8];
            rdur_s = sdur_s = "--:--";
        } else if (parts.size() == 7) {        // pre-magnetic files
            date_s = parts[0]; rise_s = parts[1]; raz_s = parts[2];
            set_s = parts[3]; saz_s = parts[4]; day_s = parts[5];
            off_s = parts[6];
            rmag_s = smag_s = "---";
            rdur_s = sdur_s = "--:--";
        } else if (parts.size() == 5) {        // pre-azimuth files
            date_s = parts[0]; rise_s = parts[1]; set_s = parts[2];
            day_s = parts[3]; off_s = parts[4];
            raz_s = saz_s = rmag_s = smag_s = "---";
            rdur_s = sdur_s = "--:--";
        } else {
            throw ValueError("unrecognized data line: " + line);
        }
        Row r;
        if (!dateFromIso(date_s, r.date))
            throw ValueError("bad date: " + date_s);
        r.rise = parseClockStr(rise_s);
        r.rise_dur = parseDur(rdur_s);
        r.rise_az = parseAz(raz_s);
        r.rise_mag = parseAz(rmag_s);
        r.set = parseClockStr(set_s);
        r.set_dur = parseDur(sdur_s);
        r.set_az = parseAz(saz_s);
        r.set_mag = parseAz(smag_s);
        std::optional<int> day = parseClockStr(day_s);
        r.day = day.value_or(0);
        r.off = parseOffsetStr(off_s);
        rows.push_back(r);
    }
    if (rows.empty()) throw ValueError("no data rows found in file");
    return rows;
}

// ----------------------------------------------------------------------------
// Minimal JSON (parse + dump) — defensive, like the Python loaders. NOTE:
// JParser keeps pointers into the text; the text must outlive the parser.
// ----------------------------------------------------------------------------
struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;

    const JVal* get(const std::string& key) const {
        if (type != Obj) return nullptr;
        for (auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    void set(const std::string& key, JVal v) {
        for (auto& kv : obj) if (kv.first == key) { kv.second = std::move(v); return; }
        obj.emplace_back(key, std::move(v));
        type = Obj;
    }
    static JVal number(double v) { JVal j; j.type = Num; j.num = v; return j; }
    static JVal boolean(bool v) { JVal j; j.type = Bool; j.b = v; return j; }
    static JVal string(const std::string& s) { JVal j; j.type = Str; j.str = s; return j; }
    static JVal object() { JVal j; j.type = Obj; return j; }
    static JVal array() { JVal j; j.type = Arr; return j; }
};

struct JParser {
    const char* p, * end;
    bool ok = true;
    JParser(const std::string& s) : p(s.c_str()), end(s.c_str() + s.size()) {}
    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
    bool lit(const char* s) {
        size_t n = strlen(s);
        if ((size_t)(end - p) >= n && strncmp(p, s, n) == 0) { p += n; return true; }
        return false;
    }
    JVal parse() { ws(); JVal v = value(); ws(); if (p != end) ok = false; return v; }
    JVal value() {
        ws();
        if (p >= end) { ok = false; return JVal(); }
        if (*p == '{') return objectV();
        if (*p == '[') return arrayV();
        if (*p == '"') { JVal v; v.type = JVal::Str; v.str = stringV(); return v; }
        if (lit("true"))  return JVal::boolean(true);
        if (lit("false")) return JVal::boolean(false);
        if (lit("null"))  return JVal();
        char* q = nullptr;
        double d = strtod(p, &q);
        if (q == p) { ok = false; return JVal(); }
        p = q;
        return JVal::number(d);
    }
    std::string stringV() {
        std::string out;
        if (p >= end || *p != '"') { ok = false; return out; }
        p++;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                p++;
                switch (*p) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (end - p >= 5) {
                        unsigned v = 0;
                        for (int i = 1; i <= 4; i++) {
                            char c = p[i]; v <<= 4;
                            if (c >= '0' && c <= '9') v += c - '0';
                            else if (c >= 'a' && c <= 'f') v += c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') v += c - 'A' + 10;
                            else ok = false;
                        }
                        p += 4;
                        if (v < 0x80) out += (char)v;
                        else if (v < 0x800) {
                            out += (char)(0xC0 | (v >> 6));
                            out += (char)(0x80 | (v & 0x3F));
                        } else {
                            out += (char)(0xE0 | (v >> 12));
                            out += (char)(0x80 | ((v >> 6) & 0x3F));
                            out += (char)(0x80 | (v & 0x3F));
                        }
                    } else ok = false;
                    break;
                }
                default: out += *p; break;
                }
                p++;
            } else out += *p++;
        }
        if (p < end) p++; else ok = false;
        return out;
    }
    JVal objectV() {
        JVal v; v.type = JVal::Obj;
        p++; ws();
        if (p < end && *p == '}') { p++; return v; }
        while (p < end) {
            ws();
            std::string key = stringV();
            ws();
            if (p >= end || *p != ':') { ok = false; break; }
            p++;
            v.obj.emplace_back(key, value());
            ws();
            if (p < end && *p == ',') { p++; continue; }
            if (p < end && *p == '}') { p++; break; }
            ok = false; break;
        }
        return v;
    }
    JVal arrayV() {
        JVal v; v.type = JVal::Arr;
        p++; ws();
        if (p < end && *p == ']') { p++; return v; }
        while (p < end) {
            v.arr.push_back(value());
            ws();
            if (p < end && *p == ',') { p++; continue; }
            if (p < end && *p == ']') { p++; break; }
            ok = false; break;
        }
        return v;
    }
};

static void jsonDump(const JVal& v, std::string& out, int indent) {
    auto pad = [&](int n) { out.append(n, ' '); };
    switch (v.type) {
    case JVal::Null: out += "null"; break;
    case JVal::Bool: out += v.b ? "true" : "false"; break;
    case JVal::Num: {
        char buf[64];
        if (v.num == (long long)v.num && std::fabs(v.num) < 1e15)
            snprintf(buf, sizeof buf, "%lld", (long long)v.num);
        else
            snprintf(buf, sizeof buf, "%.10g", v.num);
        out += buf;
        break;
    }
    case JVal::Str:
        out += '"';
        for (char c : v.str) {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c == '\n') out += "\\n";
            else if (c == '\t') out += "\\t";
            else if (c == '\r') out += "\\r";
            else if ((unsigned char)c < 0x20) {
                char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf;
            }
            else out += c;
        }
        out += '"';
        break;
    case JVal::Arr:
        if (v.arr.empty()) { out += "[]"; break; }
        out += "[\n";
        for (size_t i = 0; i < v.arr.size(); i++) {
            pad(indent + 2);
            jsonDump(v.arr[i], out, indent + 2);
            if (i + 1 < v.arr.size()) out += ',';
            out += '\n';
        }
        pad(indent); out += ']';
        break;
    case JVal::Obj:
        if (v.obj.empty()) { out += "{}"; break; }
        out += "{\n";
        for (size_t i = 0; i < v.obj.size(); i++) {
            pad(indent + 2);
            jsonDump(JVal::string(v.obj[i].first), out, 0);
            out += ": ";
            jsonDump(v.obj[i].second, out, indent + 2);
            if (i + 1 < v.obj.size()) out += ',';
            out += '\n';
        }
        pad(indent); out += '}';
        break;
    }
}

// ----------------------------------------------------------------------------
// Config persistence — %APPDATA%\Sun2Set\config.json (macOS: ~/Sun2Set/).
// ----------------------------------------------------------------------------
static std::string homeDir() {
#ifdef _WIN32
    const char* h = getenv("USERPROFILE");
#else
    const char* h = getenv("HOME");
#endif
    return h && *h ? h : ".";
}

static std::string appDataDir() {
    const char* base = getenv("APPDATA");
    std::string dir = (base && *base) ? base : homeDir();
#ifdef _WIN32
    return dir + "\\Sun2Set";
#else
    return dir + "/Sun2Set";
#endif
}

static std::string pathJoin(const std::string& dir, const char* name) {
#ifdef _WIN32
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

static void makeAppDir();                       // per-platform
static bool isDir(const std::string& path);     // per-platform
static std::string defaultFileDir();            // per-platform (Documents)

static std::string displayPath(const std::string& path) {
    std::string home = homeDir();
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (path == home) return "~";
    if (path.rfind(home + sep, 0) == 0) return "~" + path.substr(home.size());
    return path;
}

// '1180x762+208+208' -> '+208+208' (the .py's _wm_position; selftest-pinned).
static std::string wmPosition(const std::string& geometry) {
    const char* s = geometry.c_str();
    const char* p = s;
    if (!isdigit((unsigned char)*p)) return "+100+100";
    while (isdigit((unsigned char)*p)) p++;
    if (*p != 'x') return "+100+100";
    p++;
    if (!isdigit((unsigned char)*p)) return "+100+100";
    while (isdigit((unsigned char)*p)) p++;
    const char* pos = p;
    for (int i = 0; i < 2; i++) {
        if (*p != '+' && *p != '-') return "+100+100";
        p++;
        if (!isdigit((unsigned char)*p)) return "+100+100";
        while (isdigit((unsigned char)*p)) p++;
    }
    if (*p != '\0') return "+100+100";
    return std::string(pos);
}

static JVal loadConfig() {
    std::ifstream f(pathJoin(appDataDir(), "config.json"), std::ios::binary);
    if (!f) return JVal::object();
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();               // must outlive the parser!
    JParser parser(text);
    JVal data = parser.parse();
    if (!parser.ok || data.type != JVal::Obj) return JVal::object();
    return data;
}

static void saveConfig(const JVal& config) {
    makeAppDir();
    std::ofstream f(pathJoin(appDataDir(), "config.json"),
                    std::ios::binary | std::ios::trunc);
    if (!f) return;
    std::string out;
    jsonDump(config, out, 0);
    f << out;
}

// ----------------------------------------------------------------------------
// The scene: draw commands the platform backends rasterize (the Tk canvas +
// widget tree, reified — entries, radios and dropdowns included).
// ----------------------------------------------------------------------------
struct Cmd {
    enum Type { Line, Rect, RoundRect, Text, Oval, Poly, Polyline,
                ClipPush, ClipPop } type;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    RGB fill{}; bool hasFill = false;
    RGB outline{}; bool hasOutline = false;
    double width = 1;
    double radius = 0;
    bool dash = false;                          // Line: dashed (hover cursor)
    std::vector<std::pair<double, double>> pts;
    std::string text;                           // UTF-8
    int size = 10; bool bold = false;
    enum Anchor { Center, W, E, N, NW } anchor = Center;
};

struct Button { double x0, y0, x1, y1; std::string action; };

enum class Key {
    None, Left, Right, Up, Down, Home, End, PgUp, PgDn, Tab, Enter,
    Escape, Backspace, Delete,
};

// Window layout (client coordinates).
static const int PAD = 10;
static const int PANEL_X = PAD, PANEL_Y = PAD;
static const int RIGHT_X = PAD + PANEL_W + 6;
static const int TABS_H = 38;
static const int GRAPH_X = RIGHT_X + 1, GRAPH_Y = PAD + TABS_H + 1;
static const int CLIENT_W = RIGHT_X + GRAPH_W + 2 + PAD;      // 1180
static const int CLIENT_H = PAD + TABS_H + GRAPH_H + 2 + PAD; // 760
static const int PANEL_H_PX = CLIENT_H - 2 * PAD;

// ----------------------------------------------------------------------------
// The app core — a port of the Sun2Set class: fully headless when gui=false
// (the selftest), immediate-mode widgets when true.
// ----------------------------------------------------------------------------
struct EntryState {
    std::string text;
    size_t caret = 0;
    bool enabled = true;
    double x = 0, y = 0, w = 0, h = 0;          // last-drawn rect (hit test)
};

class Sun2SetApp {
public:
    bool persist;
    bool gui;
    JVal config;

    // Validated parameters (the .py's attributes).
    std::string location = DEFAULT_LOCATION;
    double lat = DEFAULT_LAT, lon = DEFAULT_LON;
    std::string tz_mode = "system";             // "system" | "fixed"
    double fixed_hours = 10.0;
    bool dst_enabled = false;
    double dst_hours = 11.0;
    Rule dst_start{ 1, 6, 10 }, dst_end{ 1, 6, 4 };
    std::string horizon_str = "0";
    std::string theme = "dark";
    Theme T;
    Date start;
    int days = DEFAULT_DAYS;

    std::vector<Row> rows;
    Meta meta;
    std::string table_text;

    // GUI state.
    std::vector<Cmd> scene;
    std::vector<Button> buttons;
    std::map<std::string, EntryState> entries;
    std::vector<std::string> entryOrder;
    std::string focusedEntry;
    int ruleValue[6] = { 0, 6, 9, 0, 6, 3 };    // ord/day/mon x start,end
    int openMenu = -1;                          // 0..5, -1 = closed
    std::string currentTab = "graph";
    std::string statusText;
    int statusKind = 0;                         // 0 sub, 1 ok, 2 err
    int tableScroll = 0, tableHScroll = 0;
    double mouseX = -1, mouseY = -1;
    long frame = 0;
    std::function<double(int, bool)> charW =    // monospace advance (px)
        [](int size, bool) { return size * 0.62 * 96.0 / 72.0; };
    std::function<std::string(bool, const std::string&, const std::string&)>
        fileDialog;                             // (save?, dir, name) -> path
    std::function<void()> requestClose;

    Sun2SetApp(bool gui_, bool persistFlag)
        : persist(persistFlag), gui(gui_), T(themeByName("dark")) {
        config = persist ? loadConfig() : JVal::object();
        location = cfgStr("location", DEFAULT_LOCATION);
        lat = cfgFloat("lat", DEFAULT_LAT, -90, 90);
        lon = cfgFloat("lon", DEFAULT_LON, -180, 180);
        std::string tm = cfgStr("tz_mode", "system");
        tz_mode = (tm == "system" || tm == "fixed") ? tm : "system";
        fixed_hours = cfgFloat("fixed_hours", 10.0, -14, 14);
        const JVal* de = config.get("dst_enabled");
        dst_enabled = de && de->type == JVal::Bool && de->b;
        dst_hours = cfgFloat("dst_hours", 11.0, -14, 14);
        dst_start = cfgRule("dst_start", Rule{ 1, 6, 10 });
        dst_end = cfgRule("dst_end", Rule{ 1, 6, 4 });
        horizon_str = cfgStr("horizon", "0");
        try {
            parseHorizon(horizon_str);
        } catch (const ValueError&) {
            horizon_str = "0";
        }
        std::string th = cfgStr("theme", "dark");
        theme = (th == "dark" || th == "light") ? th : "dark";
        T = themeByName(theme);
        std::string tab = cfgStr("tab", "graph");
        currentTab = (tab == "graph" || tab == "table") ? tab : "graph";
        start = dateToday();
        days = (int)cfgFloat("days", DEFAULT_DAYS, 1, 1500);
        syncRuleWidgets();
        if (gui) initEntries();
    }

    std::string cfgStr(const char* key, const char* fallback) const {
        const JVal* v = config.get(key);
        return v && v->type == JVal::Str ? v->str : fallback;
    }
    double cfgFloat(const char* key, double fallback, double lo,
                    double hi) const {
        const JVal* v = config.get(key);
        if (!v || v->type != JVal::Num) return fallback;
        return lo <= v->num && v->num <= hi ? v->num : fallback;
    }
    Rule cfgRule(const char* key, Rule fallback) const {
        const JVal* v = config.get(key);
        if (!v || v->type != JVal::Arr || v->arr.size() != 3) return fallback;
        for (const JVal& e : v->arr)
            if (e.type != JVal::Num) return fallback;
        int o = (int)v->arr[0].num, w = (int)v->arr[1].num,
            m = (int)v->arr[2].num;
        bool okOrd = false;
        for (int val : ORD_VALUES_LIST) if (val == o) okOrd = true;
        if (okOrd && 0 <= w && w <= 6 && 1 <= m && m <= 12)
            return Rule{ o, w, m };
        return fallback;
    }

    void syncRuleWidgets() {
        auto ordIndex = [](int ordinal) {
            for (int i = 0; i < 5; i++)
                if (ORD_VALUES_LIST[i] == ordinal) return i;
            return 0;
        };
        ruleValue[0] = ordIndex(dst_start.ordinal);
        ruleValue[1] = dst_start.weekday;
        ruleValue[2] = dst_start.month - 1;
        ruleValue[3] = ordIndex(dst_end.ordinal);
        ruleValue[4] = dst_end.weekday;
        ruleValue[5] = dst_end.month - 1;
    }

    Rule ruleFromWidgets(int base) const {       // base 0 = start, 3 = end
        return Rule{ ORD_VALUES_LIST[ruleValue[base]], ruleValue[base + 1],
                     ruleValue[base + 2] + 1 };  // widget holds month INDEX
    }

    void initEntries() {
        // The same ids as the .py's form snapshot keys, restored verbatim.
        const JVal* form = config.get("form");
        auto saved = [&](const char* key, const std::string& fallback) {
            if (form && form->type == JVal::Obj) {
                const JVal* v = form->get(key);
                if (v && v->type == JVal::Str) return v->str;
            }
            return fallback;
        };
        char buf[64];
        snprintf(buf, sizeof buf, "%.4f", lat);
        std::string latS = buf;
        snprintf(buf, sizeof buf, "%.4f", lon);
        std::string lonS = buf;
        snprintf(buf, sizeof buf, "%g", fixed_hours);
        std::string offS = buf;
        snprintf(buf, sizeof buf, "%g", dst_hours);
        std::string dstS = buf;
        const struct { const char* id; std::string value; } DEFS[] = {
            { "location", saved("location", location) },
            { "lat", saved("lat", latS) },
            { "lon", saved("lon", lonS) },
            { "offset", saved("offset", offS) },
            { "dst_offset", saved("dst_offset", dstS) },
            { "skyline", saved("skyline", horizon_str) },
            { "start", saved("start", dateIso(start)) },
            { "days", saved("days", std::to_string(days)) },
        };
        for (auto& defn : DEFS) {
            EntryState e;
            e.text = defn.value;
            e.caret = e.text.size();
            entries[defn.id] = e;
            entryOrder.push_back(defn.id);
        }
        updateEnabledStates();
    }

    void updateEnabledStates() {                 // the .py's _on_tz_mode
        bool fixed = tz_mode == "fixed";
        entries["offset"].enabled = fixed;
        bool dstOn = fixed && dst_enabled;
        entries["dst_offset"].enabled = dstOn;
        if (!fixed && openMenu >= 0) openMenu = -1;
    }

    // ---------------------------------------------------------- computation
    void compute() {
        DstSpec dstSpec;
        const DstSpec* dst = nullptr;
        if (tz_mode == "fixed" && dst_enabled) {
            dstSpec.hours = dst_hours;
            dstSpec.start = dst_start;
            dstSpec.end = dst_end;
            dst = &dstSpec;
        }
        HorizonProfile profile = parseHorizon(horizon_str);
        rows = computeRows(lat, lon, start, days, tz_mode, fixed_hours, dst,
                           profile.empty() ? nullptr : &profile);
        char buf[512];
        std::string tzDesc;
        if (tz_mode == "system") {
            tzDesc = "System local zone (" + systemZoneNames()
                     + ") - DST aware; each day's offset is in the UTCoff "
                       "column";
        } else if (dst) {
            snprintf(buf, sizeof buf,
                     "Fixed UTC offset %s with DST %s from %s to %s; "
                     "each day's offset is in the UTCoff column",
                     fmtOffset((int)std::lround(fixed_hours * 60)).c_str(),
                     fmtOffset((int)std::lround(dst_hours * 60)).c_str(),
                     fmtRule(dst_start).c_str(), fmtRule(dst_end).c_str());
            tzDesc = buf;
        } else {
            snprintf(buf, sizeof buf,
                     "Fixed UTC offset %s (no daylight-saving adjustments)",
                     fmtOffset((int)std::lround(fixed_hours * 60)).c_str());
            tzDesc = buf;
        }
        std::string hzDesc;
        if (profile.empty()) {
            hzDesc = "flat 0.0 deg (open astronomical horizon)";
        } else if (profile.size() == 1) {
            snprintf(buf, sizeof buf,
                     "uniform %.1f deg above the astronomical horizon",
                     profile[0].second);
            hzDesc = buf;
        } else {
            std::string pairs;
            for (size_t i = 0; i < profile.size(); i++) {
                snprintf(buf, sizeof buf, "%g:%g", profile[i].first,
                         profile[i].second);
                if (i) pairs += ", ";
                pairs += buf;
            }
            hzDesc = "az:alt profile " + pairs
                     + " (deg; N=0 E=90; linear interp)";
        }
        Date mid = dateAddDays(start, days / 2);
        double dMid = magneticDeclination(lat, lon, mid);
        snprintf(buf, sizeof buf,
                 "declination %.1f deg %s (%s at %s): magnetic bearing"
                 " = true %s %.1f deg",
                 std::fabs(dMid), dMid >= 0 ? "E" : "W",
                 wmmModel().name.c_str(), dateIso(mid).c_str(),
                 dMid >= 0 ? "-" : "+", std::fabs(dMid));
        time_t nowT = time(nullptr);
        struct tm lt;
#ifdef _WIN32
        localtime_s(&lt, &nowT);
#else
        localtime_r(&nowT, &lt);
#endif
        char gen[32];
        strftime(gen, sizeof gen, "%Y-%m-%d %H:%M:%S", &lt);
        meta = Meta{};
        meta.generated = gen;
        meta.location = location;
        meta.lat = lat;
        meta.lon = lon;
        meta.tz_desc = tzDesc;
        meta.horizon_desc = hzDesc;
        meta.mag_desc = buf;
        table_text = buildTableText(rows, meta);
    }

    std::string autosave() {                     // returns path, "" if none
        if (!persist) return "";
        makeAppDir();
        std::string path = pathJoin(appDataDir(), "sun2set_latest.txt");
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) throw ValueError("cannot write " + path);
        f << table_text;
        return path;
    }

    void saveTextFile(const std::string& path) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) throw ValueError("cannot write " + path);
        f << table_text;
    }

    int loadTextFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw ValueError("cannot read " + path);
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        Meta m;
        std::vector<Row> loaded = parseTableText(text, m);
        rows = loaded;
        meta = m;
        table_text = text;
        if (m.lat) lat = *m.lat;
        if (m.lon) lon = *m.lon;
        if (!m.location.empty()) location = m.location;
        auto tz = parseTzDesc(m.tz_desc);
        if (tz) {
            tz_mode = tz->tz_mode;
            if (tz->tz_mode == "fixed") {
                fixed_hours = tz->fixed_hours;
                dst_enabled = tz->dst_enabled;
                if (tz->dst_enabled) {
                    dst_hours = tz->dst_hours;
                    dst_start = tz->dst_start;
                    dst_end = tz->dst_end;
                }
            }
        }
        auto skyline = parseHorizonDesc(m.horizon_desc);
        if (skyline) {
            try {
                parseHorizon(*skyline);
                horizon_str = *skyline;
            } catch (const ValueError&) {}
        }
        start = rows[0].date;
        days = (int)rows.size();
        syncRuleWidgets();
        return (int)rows.size();
    }

    // ------------------------------------------------------- button actions
    std::string entryText(const char* id) {
        auto it = entries.find(id);
        return it == entries.end() ? "" : it->second.text;
    }

    static std::string strip(const std::string& s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        return b == std::string::npos ? "" : s.substr(b, e - b + 1);
    }

    std::string readForm() {                     // "" = OK, else the error
        double newLat, newLon;
        try {
            newLat = parseAngle(entryText("lat"));
            if (!(-90.0 <= newLat && newLat <= 90.0)) throw ValueError("");
        } catch (const ValueError&) {
            return "Latitude must be -90..90 - e.g. -34.2196, "
                   "34.2196 S or 34\xC2\xB0" "13'10.5\" S";
        }
        try {
            newLon = parseAngle(entryText("lon"));
            if (!(-180.0 <= newLon && newLon <= 180.0)) throw ValueError("");
        } catch (const ValueError&) {
            return "Longitude must be -180..180 - e.g. 149.3663, "
                   "149.3663 E or 149\xC2\xB0" "21'58.5\" E";
        }
        double fixed = fixed_hours, dstH = dst_hours;
        Rule newStart = dst_start, newEnd = dst_end;
        if (tz_mode == "fixed") {
            try {
                fixed = parseTzHours(entryText("offset"));
            } catch (const ValueError&) {
                return "Fixed offset must look like 10, -3.5 or 9:30 "
                       "(|h| <= 14)";
            }
            if (dst_enabled) {
                try {
                    dstH = parseTzHours(entryText("dst_offset"));
                } catch (const ValueError&) {
                    return "DST offset must look like 11, -2.5 or 10:30 "
                           "(|h| <= 14)";
                }
                newStart = ruleFromWidgets(0);
                newEnd = ruleFromWidgets(3);
            }
        }
        std::string hz = strip(entryText("skyline"));
        if (hz.empty()) hz = "0";
        try {
            parseHorizon(hz);
        } catch (const ValueError&) {
            return "Horizon must be 0, one altitude, or az:alt pairs "
                   "like 60:2, 90:6 (alt -5..60, az 0..360)";
        }
        Date newStartDate;
        if (!dateFromIso(strip(entryText("start")), newStartDate))
            return "Start date must be YYYY-MM-DD";
        std::string daysS = strip(entryText("days"));
        char* end = nullptr;
        long newDays = strtol(daysS.c_str(), &end, 10);
        if (daysS.empty() || *end || newDays < 1 || newDays > 1500)
            return "Days must be a whole number in 1..1500";
        location = strip(entryText("location"));
        lat = newLat;
        lon = newLon;
        fixed_hours = fixed;
        dst_hours = dstH;
        dst_start = newStart;
        dst_end = newEnd;
        horizon_str = hz;
        start = newStartDate;
        days = (int)newDays;
        return "";
    }

    void setStatus(const std::string& text, int kind) {
        statusText = text;
        statusKind = kind;
    }

    void onCalculate() {
        std::string err = readForm();
        if (!err.empty()) {
            setStatus("ERROR: " + err, 2);
            return;
        }
        compute();
        tableScroll = tableHScroll = 0;
        std::string msg = "OK: Calculated " + std::to_string(rows.size())
                          + " days from " + dateIso(start) + ".";
        try {
            std::string path = autosave();
            if (!path.empty())
                msg += "\n\nAutosaved to:\n" + displayPath(path);
        } catch (const ValueError& e) {
            msg += "\n\n(autosave failed: " + e.msg + ")";
        }
        setStatus(msg, 1);
    }

    std::string fileDialogDir() {
        const JVal* d = config.get("file_dir");
        if (d && d->type == JVal::Str && isDir(d->str)) return d->str;
        return defaultFileDir();
    }

    void rememberFileDir(const std::string& path) {
        size_t cut = path.find_last_of("/\\");
        if (cut == std::string::npos) return;
        config.set("file_dir", JVal::string(path.substr(0, cut)));
        if (persist) saveConfig(config);
    }

    void onSave() {
        if (rows.empty()) {
            setStatus("ERROR: Nothing to save - CALCULATE first.", 2);
            return;
        }
        if (!fileDialog) return;
        std::string slug;
        for (char c : location)
            slug += isalnum((unsigned char)c) ? c : '-';
        while (!slug.empty() && slug.front() == '-') slug.erase(0, 1);
        while (!slug.empty() && slug.back() == '-') slug.pop_back();
        std::string initial = "Sun2Set_" + (slug.empty() ? "data" : slug)
                              + "_" + dateIso(rows[0].date) + ".txt";
        std::string path = fileDialog(true, fileDialogDir(), initial);
        if (path.empty()) return;
        try {
            saveTextFile(path);
            rememberFileDir(path);
            setStatus("OK: Saved " + std::to_string(rows.size())
                      + " days to:\n" + displayPath(path), 1);
        } catch (const ValueError& e) {
            setStatus("ERROR: Save failed: " + e.msg, 2);
        }
    }

    void onLoad() {
        if (!fileDialog) return;
        std::string path = fileDialog(false, fileDialogDir(), "");
        if (path.empty()) return;
        rememberFileDir(path);
        int n;
        try {
            n = loadTextFile(path);
        } catch (const ValueError& e) {
            setStatus("ERROR: Load failed: " + e.msg, 2);
            return;
        }
        // Reflect every restored assumption back into the form.
        char buf[64];
        snprintf(buf, sizeof buf, "%.6f", lat);
        setEntryText("lat", buf);
        snprintf(buf, sizeof buf, "%.6f", lon);
        setEntryText("lon", buf);
        snprintf(buf, sizeof buf, "%g", fixed_hours);
        setEntryText("offset", buf);
        snprintf(buf, sizeof buf, "%g", dst_hours);
        setEntryText("dst_offset", buf);
        setEntryText("location", location);
        setEntryText("skyline", horizon_str);
        setEntryText("start", dateIso(rows[0].date));
        setEntryText("days", std::to_string(n));
        updateEnabledStates();
        tableScroll = tableHScroll = 0;
        setStatus("OK: Loaded " + std::to_string(n) + " days from:\n"
                  + displayPath(path), 1);
    }

    void setEntryText(const char* id, const std::string& text) {
        auto it = entries.find(id);
        if (it == entries.end()) return;
        it->second.text = text;
        it->second.caret = text.size();
    }

    void onClose(int winX, int winY) {
        char pos[32];
        snprintf(pos, sizeof pos, "+%d+%d", winX, winY);
        config.set("win_pos", JVal::string(pos));
        config.set("location", JVal::string(location));
        config.set("lat", JVal::number(lat));
        config.set("lon", JVal::number(lon));
        config.set("fixed_hours", JVal::number(fixed_hours));
        config.set("dst_hours", JVal::number(dst_hours));
        config.set("horizon", JVal::string(horizon_str));
        config.set("days", JVal::number(days));
        config.set("tz_mode", JVal::string(tz_mode));
        config.set("dst_enabled", JVal::boolean(dst_enabled));
        Rule rs = ruleFromWidgets(0), re = ruleFromWidgets(3);
        JVal a1 = JVal::array();
        a1.arr = { JVal::number(rs.ordinal), JVal::number(rs.weekday),
                   JVal::number(rs.month) };
        config.set("dst_start", a1);
        JVal a2 = JVal::array();
        a2.arr = { JVal::number(re.ordinal), JVal::number(re.weekday),
                   JVal::number(re.month) };
        config.set("dst_end", a2);
        config.set("tab", JVal::string(currentTab));
        config.set("theme", JVal::string(theme));
        JVal form = JVal::object();
        form.set("location", JVal::string(entryText("location")));
        form.set("lat", JVal::string(entryText("lat")));
        form.set("lon", JVal::string(entryText("lon")));
        form.set("offset", JVal::string(entryText("offset")));
        form.set("dst_offset", JVal::string(entryText("dst_offset")));
        form.set("skyline", JVal::string(entryText("skyline")));
        form.set("start", JVal::string(entryText("start")));
        form.set("days", JVal::string(entryText("days")));
        config.set("form", form);
        if (persist) saveConfig(config);
    }

    bool savedWinPos(int& x, int& y) const {
        const JVal* v = config.get("win_pos");
        if (!v || v->type != JVal::Str) return false;
        const char* s = v->str.c_str();
        if (*s != '+' && *s != '-') return false;
        char sign1, sign2;
        int a, b;
        if (sscanf(s, "%c%d%c%d", &sign1, &a, &sign2, &b) == 4
            && (sign2 == '+' || sign2 == '-')) {
            x = sign1 == '-' ? -a : a;
            y = sign2 == '-' ? -b : b;
            return true;
        }
        return false;
    }

    // ----------------------------------------------------------------- input
    void doAction(const std::string& action) {
        auto starts = [&](const char* p) { return action.rfind(p, 0) == 0; };
        if (action == "calc") onCalculate();
        else if (action == "save") onSave();
        else if (action == "load") onLoad();
        else if (action == "theme") {
            theme = theme == "dark" ? "light" : "dark";
            T = themeByName(theme);
            config.set("theme", JVal::string(theme));
            if (persist) saveConfig(config);
        } else if (starts("tab:")) {
            currentTab = action.substr(4);
        } else if (action == "tz:system") {
            tz_mode = "system";
            updateEnabledStates();
        } else if (action == "tz:fixed") {
            tz_mode = "fixed";
            updateEnabledStates();
        } else if (action == "dst:toggle") {
            if (tz_mode == "fixed") {
                dst_enabled = !dst_enabled;
                updateEnabledStates();
            }
        } else if (starts("menu:")) {
            int idx = atoi(action.c_str() + 5);
            openMenu = openMenu == idx ? -1 : idx;
        } else if (starts("item:")) {
            if (openMenu >= 0) {
                ruleValue[openMenu] = atoi(action.c_str() + 5);
                openMenu = -1;
            }
        } else if (starts("entry:")) {
            focusedEntry = action.substr(6);
        }
    }

    void onClick(double x, double y) {
        // An open dropdown swallows the click (like a Tk popup menu).
        if (openMenu >= 0) {
            for (const Button& b : buttons)
                if (b.action.rfind("item:", 0) == 0 && b.x0 <= x && x <= b.x1
                    && b.y0 <= y && y <= b.y1) {
                    doAction(b.action);
                    return;
                }
            openMenu = -1;
            return;
        }
        // Entry focus + caret placement first (they're also in buttons).
        for (auto& kv : entries) {
            EntryState& e = kv.second;
            if (e.enabled && e.x <= x && x <= e.x + e.w && e.y <= y
                && y <= e.y + e.h) {
                focusedEntry = kv.first;
                double cw = charW(10, false);
                long idx = (long)std::lround((x - e.x - 6) / std::max(1.0, cw));
                e.caret = (size_t)std::max(0L, std::min((long)e.text.size(),
                                                        idx));
                return;
            }
        }
        focusedEntry.clear();
        for (const Button& b : buttons)
            if (b.x0 <= x && x <= b.x1 && b.y0 <= y && y <= b.y1) {
                doAction(b.action);
                return;
            }
    }

    void onMouseMove(double x, double y) {
        mouseX = x;
        mouseY = y;
    }

    void onWheel(double delta, bool horizontal) {
        if (currentTab != "table") return;
        if (horizontal)
            tableHScroll = std::max(0, tableHScroll - (int)(delta * 30));
        else
            tableScroll = std::max(0, tableScroll - (int)(delta * 3));
    }

    void charInput(const std::string& utf8) {
        if (focusedEntry.empty()) return;
        auto it = entries.find(focusedEntry);
        if (it == entries.end() || !it->second.enabled) return;
        EntryState& e = it->second;
        e.text.insert(e.caret, utf8);
        e.caret += utf8.size();
    }

    void pasteText(const std::string& utf8) {
        std::string clean;
        for (char c : utf8)
            if ((unsigned char)c >= 0x20 || (unsigned char)c >= 0x80)
                clean += c;
        charInput(clean);
    }

    static size_t utf8Prev(const std::string& s, size_t pos) {
        if (pos == 0) return 0;
        pos--;
        while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) pos--;
        return pos;
    }
    static size_t utf8Next(const std::string& s, size_t pos) {
        if (pos >= s.size()) return s.size();
        pos++;
        while (pos < s.size() && ((unsigned char)s[pos] & 0xC0) == 0x80) pos++;
        return pos;
    }

    void handleKey(Key key, bool shift) {
        if (openMenu >= 0 && key == Key::Escape) {
            openMenu = -1;
            return;
        }
        if (!focusedEntry.empty()) {
            auto it = entries.find(focusedEntry);
            if (it != entries.end() && it->second.enabled) {
                EntryState& e = it->second;
                switch (key) {
                case Key::Backspace:
                    if (e.caret > 0) {
                        size_t prev = utf8Prev(e.text, e.caret);
                        e.text.erase(prev, e.caret - prev);
                        e.caret = prev;
                    }
                    return;
                case Key::Delete:
                    if (e.caret < e.text.size())
                        e.text.erase(e.caret,
                                     utf8Next(e.text, e.caret) - e.caret);
                    return;
                case Key::Left: e.caret = utf8Prev(e.text, e.caret); return;
                case Key::Right: e.caret = utf8Next(e.text, e.caret); return;
                case Key::Home: e.caret = 0; return;
                case Key::End: e.caret = e.text.size(); return;
                case Key::Tab: {
                    int idx = -1;
                    for (int i = 0; i < (int)entryOrder.size(); i++)
                        if (entryOrder[i] == focusedEntry) idx = i;
                    int n = (int)entryOrder.size();
                    for (int step = 1; step <= n; step++) {
                        int j = ((idx + (shift ? -step : step)) % n + n) % n;
                        if (entries[entryOrder[j]].enabled) {
                            focusedEntry = entryOrder[j];
                            EntryState& f = entries[focusedEntry];
                            f.caret = f.text.size();
                            break;
                        }
                    }
                    return;
                }
                case Key::Escape: focusedEntry.clear(); return;
                case Key::Enter: onCalculate(); return;
                default: break;
                }
            }
        }
        if (key == Key::Enter) { onCalculate(); return; }
        if (key == Key::Tab && !entryOrder.empty()) {
            focusedEntry = shift ? entryOrder.back() : entryOrder.front();
            EntryState& f = entries[focusedEntry];
            f.caret = f.text.size();
            return;
        }
        if (currentTab == "table") {
            if (key == Key::PgDn) tableScroll += 20;
            else if (key == Key::PgUp) tableScroll = std::max(0, tableScroll - 20);
            else if (key == Key::Home) tableScroll = 0;
        }
    }

    void tick() {
        frame++;
        if (gui) draw();
    }

    // ----------------------------------------------------- scene emission
    void emitLine(double x0, double y0, double x1, double y1, RGB color,
                  double width = 1, bool dash = false) {
        Cmd c;
        c.type = Cmd::Line;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
        c.outline = color; c.hasOutline = true;
        c.width = width;
        c.dash = dash;
        scene.push_back(c);
    }
    void emitRect(double x0, double y0, double x1, double y1, bool hasFill,
                  RGB fill, bool hasOutline = false, RGB outline = RGB{},
                  double width = 1) {
        Cmd c;
        c.type = Cmd::Rect;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
        c.hasFill = hasFill; c.fill = fill;
        c.hasOutline = hasOutline; c.outline = outline; c.width = width;
        scene.push_back(c);
    }
    void emitRoundRect(double x0, double y0, double x1, double y1,
                       double radius, bool hasFill, RGB fill, RGB outline,
                       double width = 1) {
        Cmd c;
        c.type = Cmd::RoundRect;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
        c.radius = std::min(radius, std::min((x1 - x0) / 2, (y1 - y0) / 2));
        c.hasFill = hasFill; c.fill = fill;
        c.hasOutline = true; c.outline = outline; c.width = width;
        scene.push_back(c);
    }
    void emitText(double x, double y, const std::string& s, RGB color,
                  int size, bool bold, Cmd::Anchor anchor = Cmd::Center) {
        Cmd c;
        c.type = Cmd::Text;
        c.x0 = x; c.y0 = y;
        c.text = s; c.fill = color; c.hasFill = true;
        c.size = size; c.bold = bold; c.anchor = anchor;
        scene.push_back(c);
    }
    void emitOval(double x0, double y0, double x1, double y1, bool hasFill,
                  RGB fill, bool hasOutline = false, RGB outline = RGB{},
                  double width = 1) {
        Cmd c;
        c.type = Cmd::Oval;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
        c.hasFill = hasFill; c.fill = fill;
        c.hasOutline = hasOutline; c.outline = outline; c.width = width;
        scene.push_back(c);
    }
    void emitPoly(const std::vector<std::pair<double, double>>& pts, RGB fill) {
        Cmd c;
        c.type = Cmd::Poly;
        c.pts = pts;
        c.fill = fill; c.hasFill = true;
        scene.push_back(c);
    }
    void emitPolyline(const std::vector<std::pair<double, double>>& pts,
                      RGB color, double width) {
        Cmd c;
        c.type = Cmd::Polyline;
        c.pts = pts;
        c.outline = color; c.hasOutline = true;
        c.width = width;
        scene.push_back(c);
    }
    void emitClip(double x0, double y0, double x1, double y1) {
        Cmd c;
        c.type = Cmd::ClipPush;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
        scene.push_back(c);
    }
    void emitUnclip() {
        Cmd c;
        c.type = Cmd::ClipPop;
        scene.push_back(c);
    }

    void roundButton(double x0, double y0, double x1, double y1,
                     const std::string& label, const std::string& action,
                     double radius, bool primary = false,
                     RGB* fillOver = nullptr, RGB* fgOver = nullptr,
                     int size = 10) {
        RGB fill = primary ? T.primary : T.btn;
        RGB fg = primary ? T.primary_fg : T.text;
        if (fillOver) fill = *fillOver;
        if (fgOver) fg = *fgOver;
        emitRoundRect(x0, y0, x1, y1, radius, true, fill,
                      primary ? T.primary : T.btn_edge, 1);
        emitText((x0 + x1) / 2, (y0 + y1) / 2, label, fg, size, true);
        buttons.push_back(Button{ x0, y0, x1, y1, action });
    }

    // ------------------------------------------------------------ the panel
    double menuRect[6][4] = {};                 // dropdown rects, per frame

    void drawEntryRow(double& y, const char* id, const std::string& label,
                      double boxW = 0) {
        double px = PANEL_X + 12;
        double labelW = 9 * charW(10, false);
        EntryState& e = entries[id];
        double x0 = px + labelW;
        double x1 = boxW > 0 ? x0 + boxW : PANEL_X + PANEL_W - 12;
        double h = 24;
        emitText(px, y + h / 2, label, e.enabled ? T.text : T.disabled, 10,
                 false, Cmd::W);
        RGB boxBg = e.enabled ? T.btn : T.panel;
        bool focused = focusedEntry == id && e.enabled;
        emitRect(x0, y, x1, y + h, true, boxBg, true,
                 focused ? T.accent : T.btn_edge, 1);
        e.x = x0; e.y = y; e.w = x1 - x0; e.h = h;
        emitClip(x0 + 2, y, x1 - 2, y + h);
        emitText(x0 + 6, y + h / 2, e.text,
                 e.enabled ? T.text : T.disabled, 10, false, Cmd::W);
        if (focused && (frame / 16) % 2 == 0) {
            size_t cps = 0;
            for (size_t i = 0; i < e.caret && i < e.text.size();
                 i = utf8Next(e.text, i))
                cps++;
            double cx = e.x + 6 + cps * charW(10, false);
            emitLine(cx, y + 4, cx, y + h - 4, T.text, 1);
        }
        emitUnclip();
        y += h + 4;
    }

    void drawSection(double& y, const std::string& text) {
        y += 8;
        emitText(PANEL_X + 12, y + 8, text, T.accent, 10, true, Cmd::W);
        y += 18;
    }

    void drawHint(double& y, const std::string& text) {
        emitText(PANEL_X + 12, y + 7, text, T.sub, 8, false, Cmd::W);
        y += 15;
    }

    void drawRadio(double& y, const std::string& label,
                   const std::string& action, bool selected, bool enabled) {
        double px = PANEL_X + 12;
        RGB fg = enabled ? T.text : T.disabled;
        emitOval(px, y + 4, px + 12, y + 16, true, T.btn, true, T.btn_edge, 1);
        if (selected)
            emitOval(px + 3, y + 7, px + 9, y + 13, true, fg);
        emitText(px + 18, y + 10, label, fg, 10, false, Cmd::W);
        buttons.push_back(Button{ px, y, PANEL_X + PANEL_W - 12, y + 20,
                                  action });
        y += 22;
    }

    void drawCheck(double& y, const std::string& label,
                   const std::string& action, bool checked, bool enabled) {
        double px = PANEL_X + 12;
        RGB fg = enabled ? T.text : T.disabled;
        emitRect(px, y + 4, px + 12, y + 16, true, T.btn, true, T.btn_edge, 1);
        if (checked)
            emitRect(px + 3, y + 7, px + 9, y + 13, true, fg);
        emitText(px + 18, y + 10, label, fg, 10, false, Cmd::W);
        buttons.push_back(Button{ px, y, PANEL_X + PANEL_W - 12, y + 20,
                                  action });
        y += 22;
    }

    void drawRuleRow(double& y, const std::string& label, int base,
                     bool enabled) {
        double px = PANEL_X + 12;
        emitText(px, y + 13, label, enabled ? T.text : T.disabled, 10, false,
                 Cmd::W);
        double x = px + 9 * charW(10, false);
        for (int i = 0; i < 3; i++) {
            int menuIdx = base + i;
            double w = 56;
            const char* value =
                i == 0 ? ORD_NAMES_LIST[ruleValue[menuIdx]]
                : i == 1 ? DAY_NAMES[ruleValue[menuIdx]]
                         : MONTH_NAMES[ruleValue[menuIdx]];
            RGB fill = enabled ? T.btn : T.panel;
            emitRoundRect(x, y, x + w, y + 26, 9, true, fill, T.btn_edge, 1);
            emitText(x + w / 2, y + 13, value,
                     enabled ? T.text : T.disabled, 9, false);
            menuRect[menuIdx][0] = x;
            menuRect[menuIdx][1] = y;
            menuRect[menuIdx][2] = x + w;
            menuRect[menuIdx][3] = y + 26;
            if (enabled)
                buttons.push_back(Button{ x, y, x + w, y + 26,
                                          "menu:" + std::to_string(menuIdx) });
            x += w + 4;
        }
        y += 30;
    }

    void drawPanel() {
        emitRect(PANEL_X, PANEL_Y, PANEL_X + PANEL_W, PANEL_Y + PANEL_H_PX,
                 true, T.panel, true, T.edge, 1);
        double y = PANEL_Y + 4;
        drawSection(y, "LOCATION");
        drawEntryRow(y, "location", "Name");
        drawEntryRow(y, "lat", "Latitude");
        drawEntryRow(y, "lon", "Longitude");
        drawHint(y, "degrees (+ = N/E); paste or DMS OK,");
        drawHint(y, "e.g. -34.2196 or 34\xC2\xB0" "13'10.5\" S");
        drawSection(y, "TIME ZONE");
        drawRadio(y, "System zone (DST aware)", "tz:system",
                  tz_mode == "system", true);
        drawRadio(y, "Fixed UTC offset (hours):", "tz:fixed",
                  tz_mode == "fixed", true);
        drawEntryRow(y, "offset", "Offset", 8 * charW(10, false) + 12);
        drawHint(y, "e.g. 10, -3.5 or 9:30");
        drawCheck(y, "with daylight saving:", "dst:toggle", dst_enabled,
                  tz_mode == "fixed");
        bool dstOn = tz_mode == "fixed" && dst_enabled;
        drawEntryRow(y, "dst_offset", "DST offs.", 8 * charW(10, false) + 12);
        drawRuleRow(y, "starts", 0, dstOn);
        drawRuleRow(y, "ends", 3, dstOn);
        drawSection(y, "HORIZON");
        drawEntryRow(y, "skyline", "Skyline");
        drawHint(y, "deg above true horizon: 0 = flat,");
        drawHint(y, "5 = hills, or az:alt 60:2, 90:6, 240:8");
        drawSection(y, "RANGE");
        drawEntryRow(y, "start", "Start");
        drawEntryRow(y, "days", "Days");
        y += 10;
        roundButton(PANEL_X + 12, y, PANEL_X + PANEL_W - 12, y + 40,
                    "CALCULATE", "calc", 12, true, nullptr, nullptr, 11);
        y += 46;
        double half = (PANEL_W - 24 - 6) / 2.0;
        roundButton(PANEL_X + 12, y, PANEL_X + 12 + half, y + 34,
                    "SAVE AS\xE2\x80\xA6", "save", 10);
        roundButton(PANEL_X + 12 + half + 6, y, PANEL_X + PANEL_W - 12,
                    y + 34, "LOAD\xE2\x80\xA6", "load", 10);
        y += 44;
        // Status box: a six-line reserve, character-wrapped (monospace).
        double cw9 = charW(9, false);
        int maxChars = (int)((PANEL_W - 28) / std::max(1.0, cw9));
        std::vector<std::string> lines;
        std::istringstream in(statusText);
        std::string raw;
        while (std::getline(in, raw)) {
            if (raw.empty()) { lines.push_back(""); continue; }
            while ((int)raw.size() > maxChars) {
                size_t cut = raw.rfind(' ', maxChars);
                if (cut == std::string::npos || cut == 0) cut = maxChars;
                lines.push_back(raw.substr(0, cut));
                raw = raw.substr(raw[cut] == ' ' ? cut + 1 : cut);
            }
            lines.push_back(raw);
        }
        RGB fg = statusKind == 1 ? T.ok : statusKind == 2 ? T.err : T.sub;
        for (int i = 0; i < (int)lines.size() && i < 6; i++)
            emitText(PANEL_X + 14, y + 7 + i * 15, lines[i], fg, 9, false,
                     Cmd::W);
    }

    // ---------------------------------------------------------- tabs + views
    void drawTabs() {
        double x = RIGHT_X;
        for (const char* key : { "graph", "table" }) {
            bool active = currentTab == key;
            RGB fill = active ? T.btn_hover : T.btn;
            RGB fg = active ? T.accent : T.sub;
            std::string label = key;
            for (char& c : label) c = (char)toupper((unsigned char)c);
            roundButton(x, PAD, x + 110, PAD + 32, label,
                        std::string("tab:") + key, 10, false, &fill, &fg);
            x += 116;
        }
        std::string themeLabel = "THEME: ";
        std::string tn = theme;
        for (char& c : tn) c = (char)toupper((unsigned char)c);
        themeLabel += tn;
        double tx1 = RIGHT_X + GRAPH_W + 2;
        roundButton(tx1 - 150, PAD, tx1, PAD + 32, themeLabel, "theme", 10);
        emitText(tx1 - 162, PAD + 16, "hover the graph for exact values",
                 T.sub, 9, false, Cmd::E);
    }

    // ------------------------------------------------------------ the graph
    struct PlotGeom { double L, T, pw, ph; int n; };
    PlotGeom plot{};
    bool plotValid = false;

    void drawGraph() {
        const Theme& th = T;
        emitRect(GRAPH_X - 1, GRAPH_Y - 1, GRAPH_X + GRAPH_W + 1,
                 GRAPH_Y + GRAPH_H + 1, true, th.bg, true, th.edge, 1);
        plotValid = false;
        if (rows.empty()) {
            emitText(GRAPH_X + GRAPH_W / 2.0, GRAPH_Y + GRAPH_H / 2.0,
                     "No data \xE2\x80\x94 press CALCULATE", th.sub, 12,
                     false);
            return;
        }
        const double L = 64, R = 16, TT = 64, B = 40;
        double pw = GRAPH_W - L - R, ph = GRAPH_H - TT - B;
        int n = (int)rows.size();
        auto X = [&](int i) {
            return GRAPH_X + L + pw * i / std::max(1, n - 1);
        };
        auto Y = [&](double hours) {
            return GRAPH_Y + TT + ph * (1.0 - hours / 24.0);
        };
        auto clockH = [](int sec) { return (sec % 86400) / 3600.0; };

        // Shaded daylight band: one polygon per contiguous non-polar run.
        std::vector<int> run;
        for (int i = 0; i <= n; i++) {
            bool has = i < n && rows[i].rise && rows[i].set;
            if (has) {
                run.push_back(i);
            } else if (!run.empty()) {
                if (run.size() >= 2) {
                    std::vector<std::pair<double, double>> pts;
                    for (int i2 : run)
                        pts.emplace_back(X(i2), Y(clockH(*rows[i2].rise)));
                    for (auto it = run.rbegin(); it != run.rend(); ++it)
                        pts.emplace_back(X(*it), Y(clockH(*rows[*it].set)));
                    emitPoly(pts, th.band);
                }
                run.clear();
            }
        }

        for (int h = 0; h <= 24; h += 2) {
            double y = Y(h);
            emitLine(GRAPH_X + L, y, GRAPH_X + GRAPH_W - R, y, th.grid);
            char lbl[8];
            snprintf(lbl, sizeof lbl, "%02d:00", h);
            emitText(GRAPH_X + L - 8, y, lbl, th.sub, 8, false, Cmd::E);
        }
        for (int i = 0; i < n; i++) {
            const Row& r = rows[i];
            if (r.date.d == 1 && n > 27) {
                double x = X(i);
                emitLine(x, GRAPH_Y + TT, x, GRAPH_Y + TT + ph, th.grid);
                emitText(x, GRAPH_Y + TT + ph + 6, MONTH_NAMES[r.date.m - 1],
                         th.sub, 8, false, Cmd::N);
                if (r.date.m == 1)
                    emitText(x, GRAPH_Y + TT + ph + 18,
                             std::to_string(r.date.y), th.sub, 8, false,
                             Cmd::N);
            }
        }
        emitRect(GRAPH_X + L, GRAPH_Y + TT, GRAPH_X + GRAPH_W - R,
                 GRAPH_Y + TT + ph, false, RGB{}, true, th.axis, 1);

        // Curves, split wherever a value is missing (polar days).
        auto curve = [&](std::function<std::optional<double>(const Row&)> get,
                         RGB color) {
            std::vector<std::pair<double, double>> seg;
            for (int i = 0; i <= n; i++) {
                std::optional<double> v =
                    i < n ? get(rows[i]) : std::nullopt;
                if (!v) {
                    if (seg.size() >= 2) emitPolyline(seg, color, 2);
                    else if (seg.size() == 1)
                        emitOval(seg[0].first - 1, seg[0].second - 1,
                                 seg[0].first + 1, seg[0].second + 1, true,
                                 color, true, color, 1);
                    seg.clear();
                } else {
                    seg.emplace_back(X(i), Y(*v));
                }
            }
        };
        curve([&](const Row& r) -> std::optional<double> {
            if (!r.rise) return std::nullopt;
            return clockH(*r.rise);
        }, th.rise);
        curve([&](const Row& r) -> std::optional<double> {
            if (!r.set) return std::nullopt;
            return clockH(*r.set);
        }, th.set);
        curve([&](const Row& r) -> std::optional<double> {
            return r.day / 3600.0;
        }, th.day);

        // Title + legend.
        std::string title = meta.location;
        if (meta.lat && meta.lon) {
            char buf[64];
            snprintf(buf, sizeof buf, "  (%+.4f\xC2\xB0, %+.4f\xC2\xB0)",
                     *meta.lat, *meta.lon);
            title += buf;
        }
        title = strip(title);
        emitText(GRAPH_X + L, GRAPH_Y + 14,
                 title.empty() ? "Sun almanac" : title, th.text, 13, true,
                 Cmd::W);
        std::string tzShort = meta.tz_desc.substr(0, meta.tz_desc.find(';'));
        emitText(GRAPH_X + L, GRAPH_Y + 34,
                 dateIso(rows[0].date) + " \xE2\x86\x92 "
                 + dateIso(rows[n - 1].date) + "   \xC2\xB7   " + tzShort,
                 th.sub, 8, false, Cmd::W);
        std::string hz = meta.horizon_desc;
        if (!hz.empty() && hz.rfind("flat", 0) != 0) {
            std::string shortHz = hz.substr(0, hz.find(" above"));
            shortHz = shortHz.substr(0, shortHz.find(" ("));
            if (shortHz.size() > 60) shortHz = shortHz.substr(0, 57) + "...";
            emitText(GRAPH_X + L, GRAPH_Y + 49, "skyline: " + shortHz, th.sub,
                     8, false, Cmd::W);
        }
        double lx = GRAPH_X + GRAPH_W - R;
        const struct { const char* label; RGB color; } LEGEND[3] = {
            { "day length", th.day }, { "sunset", th.set },
            { "sunrise", th.rise },
        };
        double cw9 = charW(9, false);
        for (auto& item : LEGEND) {
            emitText(lx, GRAPH_Y + 52, item.label, th.sub, 9, false, Cmd::E);
            double x0 = lx - strlen(item.label) * cw9;
            emitLine(x0 - 24, GRAPH_Y + 52, x0 - 6, GRAPH_Y + 52, item.color,
                     3);
            lx = x0 - 32;
        }

        plot = PlotGeom{ GRAPH_X + L, GRAPH_Y + TT, pw, ph, n };
        plotValid = true;
        drawHover();
    }

    void drawHover() {
        if (!plotValid || rows.empty()) return;
        const PlotGeom& p = plot;
        int n = p.n;
        if (mouseX < p.L - 6 || mouseX > p.L + p.pw + 6 || mouseY < GRAPH_Y
            || mouseY > GRAPH_Y + GRAPH_H)
            return;
        int i = n > 1 ? (int)std::lround((mouseX - p.L) / p.pw * (n - 1)) : 0;
        if (i < 0 || i >= n) return;
        const Row& r = rows[i];
        double x = p.L + p.pw * i / std::max(1, n - 1);
        emitLine(x, p.T, x, p.T + p.ph, T.hover, 1, true);
        auto event = [](const std::optional<int>& sec,
                        const std::optional<double>& az) {
            std::string clock = fmtClock(sec);
            if (!az) return clock;
            char buf[48];
            snprintf(buf, sizeof buf, "%s az %.1f\xC2\xB0", clock.c_str(),
                     *az);
            return std::string(buf);
        };
        std::vector<std::string> lines;
        lines.push_back(dateIso(r.date) + "   rise " + event(r.rise, r.rise_az)
                        + "   set " + event(r.set, r.set_az) + "   day "
                        + fmtSpan(r.day) + "   UTC" + fmtOffset(r.off));
        if (r.rise_dur || r.set_dur)
            lines.push_back("horizon crossing   rise " + fmtDur(r.rise_dur)
                            + "   set " + fmtDur(r.set_dur));
        if (r.rise_mag && r.set_mag) {
            char buf[80];
            snprintf(buf, sizeof buf,
                     "magnetic compass   rise az %.1f\xC2\xB0   set az "
                     "%.1f\xC2\xB0", *r.rise_mag, *r.set_mag);
            lines.push_back(buf);
        }
        double cw10 = charW(10, false);
        size_t maxLen = 0;
        for (auto& l : lines) {
            size_t cps = 0;                     // ° is 2 bytes, 1 column
            for (size_t j = 0; j < l.size(); j = utf8Next(l, j)) cps++;
            maxLen = std::max(maxLen, cps);
        }
        double bx0 = p.L + 10 - 6, by0 = p.T + 10 - 4;
        double bx1 = p.L + 10 + maxLen * cw10 + 6;
        double by1 = p.T + 10 + lines.size() * 15 + 4;
        emitRect(bx0, by0, bx1, by1, true, T.readout, true, T.edge, 1);
        for (size_t j = 0; j < lines.size(); j++)
            emitText(p.L + 10, p.T + 10 + j * 15 + 7, lines[j], T.text, 10,
                     false, Cmd::W);
    }

    // ------------------------------------------------------------- the table
    std::vector<std::string> tableLines;
    size_t tableLinesFor = (size_t)-1;

    void drawTable() {
        emitRect(GRAPH_X - 1, GRAPH_Y - 1, GRAPH_X + GRAPH_W + 1,
                 GRAPH_Y + GRAPH_H + 1, true, T.panel, true, T.edge, 1);
        if (tableLinesFor != table_text.size()) {
            tableLines.clear();
            std::istringstream in(table_text);
            std::string line;
            while (std::getline(in, line)) {
                while (!line.empty() && line.back() == '\r') line.pop_back();
                tableLines.push_back(line);
            }
            tableLinesFor = table_text.size();
        }
        const double lineH = 16;
        int visible = (int)(GRAPH_H / lineH) - 1;
        int maxScroll = std::max(0, (int)tableLines.size() - visible);
        tableScroll = std::max(0, std::min(tableScroll, maxScroll));
        emitClip(GRAPH_X, GRAPH_Y, GRAPH_X + GRAPH_W - 12, GRAPH_Y + GRAPH_H);
        for (int i = 0; i < visible; i++) {
            int idx = tableScroll + i;
            if (idx >= (int)tableLines.size()) break;
            emitText(GRAPH_X + 8 - tableHScroll, GRAPH_Y + 6 + i * lineH,
                     tableLines[idx], T.text, 10, false, Cmd::NW);
        }
        emitUnclip();
        if ((int)tableLines.size() > visible) {   // scrollbar thumb
            double track0 = GRAPH_Y + 2, track1 = GRAPH_Y + GRAPH_H - 2;
            double frac0 = (double)tableScroll / tableLines.size();
            double frac1 = (double)(tableScroll + visible) / tableLines.size();
            emitRect(GRAPH_X + GRAPH_W - 10, track0, GRAPH_X + GRAPH_W - 2,
                     track1, true, T.btn);
            emitRect(GRAPH_X + GRAPH_W - 10,
                     track0 + frac0 * (track1 - track0),
                     GRAPH_X + GRAPH_W - 2,
                     track0 + std::min(1.0, frac1) * (track1 - track0), true,
                     T.btn_edge);
        }
    }

    void drawMenuPopup() {
        if (openMenu < 0) return;
        const double* mr = menuRect[openMenu];
        int which = openMenu % 3;
        int count = which == 0 ? 5 : which == 1 ? 7 : 12;
        double w = mr[2] - mr[0];
        double itemH = 20;
        double y0 = mr[3] + 2;
        double y1 = y0 + count * itemH + 4;
        if (y1 > CLIENT_H - 4) {                 // open upward near the bottom
            y1 = mr[1] - 2;
            y0 = y1 - count * itemH - 4;
        }
        emitRect(mr[0], y0, mr[0] + w, y1, true, T.btn, true, T.btn_edge, 1);
        for (int i = 0; i < count; i++) {
            const char* label = which == 0 ? ORD_NAMES_LIST[i]
                                : which == 1 ? DAY_NAMES[i] : MONTH_NAMES[i];
            double iy = y0 + 2 + i * itemH;
            if (i == ruleValue[openMenu])
                emitRect(mr[0] + 1, iy, mr[0] + w - 1, iy + itemH, true,
                         T.btn_hover);
            emitText(mr[0] + w / 2, iy + itemH / 2, label, T.text, 9, false);
            buttons.push_back(Button{ mr[0], iy, mr[0] + w, iy + itemH,
                                      "item:" + std::to_string(i) });
        }
    }

    void draw() {
        scene.clear();
        buttons.clear();
        emitRect(0, 0, CLIENT_W, CLIENT_H, true, T.bg);
        drawPanel();
        drawTabs();
        if (currentTab == "graph") drawGraph();
        else drawTable();
        drawMenuPopup();
    }
};

// ----------------------------------------------------------------------------
// Self-test — the same reference anchors as Sun2Set.py --selftest.
// ----------------------------------------------------------------------------
static int gSelftestFailures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "SELFTEST FAIL line %d: %s (%s)\n", __LINE__, #cond, msg); \
    gSelftestFailures++; } } while (0)

static int hms(int h, int m, int s = 0) { return h * 3600 + m * 60 + s; }

static bool expectValueError(std::function<void()> fn) {
    try {
        fn();
        return false;
    } catch (const ValueError&) {
        return true;
    }
}

static int runSelftest() {
    printf("Sun2Set selftest...\n");

    // 1. Known sunrise/sunset times + azimuths (WolframAlpha-verified).
    struct Case {
        const char* label;
        double lat, lon, tzh;
        Date date;
        int wantRise, wantSet;
        double wantRaz, wantSaz;
    };
    const Case cases[3] = {
        { "London Jun-21 solstice", 51.5074, -0.1278, 1.0,
          Date{ 2026, 6, 21 }, hms(4, 43), hms(21, 21), 48.9, 311.0 },
        { "Sydney Dec-21 solstice", -33.8688, 151.2093, 11.0,
          Date{ 2026, 12, 21 }, hms(5, 40), hms(20, 5), 119.3, 240.8 },
        { "Reykjavik Dec-21", 64.1466, -21.9426, 0.0,
          Date{ 2026, 12, 21 }, hms(11, 22), hms(15, 29), 151.8, 208.0 },
    };
    for (const Case& c : cases) {
        SunEvents ev = sunEvents(c.date, c.lat, c.lon, c.tzh);
        CHECK(ev.rise && std::fabs(*ev.rise * 60.0 - c.wantRise) <= 120,
              c.label);
        CHECK(ev.set && std::fabs(*ev.set * 60.0 - c.wantSet) <= 120,
              c.label);
        CHECK(ev.rise_az && std::fabs(*ev.rise_az - c.wantRaz) <= 2.0,
              c.label);
        CHECK(ev.set_az && std::fabs(*ev.set_az - c.wantSaz) <= 2.0, c.label);
    }
    printf("  reference sunrise/sunset times + azimuths OK (3 locations)\n");

    // 1b. Horizon-crossing durations: slant formula + pinned values.
    for (const Case& c : cases) {
        SunEvents ev = sunEvents(c.date, c.lat, c.lon, c.tzh);
        const struct { std::optional<double> dur, az; } evs[2] = {
            { ev.rise_dur, ev.rise_az }, { ev.set_dur, ev.set_az }
        };
        for (auto& e : evs) {
            CHECK((bool)e.dur, c.label);
            if (!e.dur || !e.az) continue;
            double rate = 15.0 * std::cos(rad(c.lat))
                          * std::fabs(std::sin(rad(*e.az)));
            double approx = SUN_DIAMETER / rate * 60.0;
            CHECK(std::fabs(*e.dur - approx)
                  <= std::max(0.15, 0.08 * approx), c.label);
        }
    }
    const struct { double lat, lon, tzh; Date date; double wantDur; }
    durPins[3] = {
        { -33.8688, 151.2093, 11.0, Date{ 2026, 12, 21 }, 2.94 },
        { 51.5074, -0.1278, 1.0, Date{ 2026, 6, 21 }, 4.52 },
        { 64.1466, -21.9426, 0.0, Date{ 2026, 12, 21 }, 10.83 },
    };
    for (auto& pin : durPins) {
        SunEvents ev = sunEvents(pin.date, pin.lat, pin.lon, pin.tzh);
        CHECK(ev.rise_dur && std::fabs(*ev.rise_dur - pin.wantDur) < 0.2,
              "pinned rise dur");
        CHECK(ev.set_dur && std::fabs(*ev.set_dur - pin.wantDur) < 0.2,
              "pinned set dur");
    }
    printf("  horizon-crossing durations OK (Sydney solstice ~2m56s, "
           "Reykjavik midwinter ~10m50s)\n");

    // 2. Polar night / midnight sun (Longyearbyen) + grazing days.
    {
        const struct { Date date; const char* expect; } polar[2] = {
            { Date{ 2026, 12, 21 }, "night" }, { Date{ 2026, 6, 21 }, "day" }
        };
        for (auto& pc : polar) {
            SunEvents ev = sunEvents(pc.date, 78.2232, 15.6267, 1.0);
            CHECK(ev.polar == pc.expect && !ev.rise && !ev.set, pc.expect);
            CHECK(!ev.rise_az && !ev.set_az, pc.expect);
            CHECK(!ev.rise_dur && !ev.set_dur, pc.expect);
            CHECK(ev.daylight == (strcmp(pc.expect, "night") == 0 ? 0.0
                                                                  : 1440.0),
                  pc.expect);
        }
        std::vector<Row> rowsG = computeRows(78.2232, 15.6267,
                                             Date{ 2026, 2, 10 }, 12, "fixed",
                                             1.0);
        int grazing = 0;
        std::vector<const Row*> full;
        for (const Row& r : rowsG) {
            if (r.rise && !r.rise_dur) grazing++;
            if (r.rise_dur) full.push_back(&r);
        }
        CHECK(grazing > 0 && !full.empty() && *full[0]->rise_dur > 1200,
              "grazing days");
        printf("  polar night / midnight sun OK (+%d grazing days: sun up, "
               "disc never fully clear)\n", grazing);
    }

    // 3. Equinox at the equator.
    {
        SunEvents ev = sunEvents(Date{ 2027, 3, 20 }, 0.0, 0.0, 0.0);
        double d = (*ev.set - *ev.rise) * 60.0;
        CHECK(hms(12, 4) < d && d < hms(12, 10), "equator day length");
        CHECK(std::fabs(*ev.rise_az - 90.0) < 1.0, "equator rise az");
        CHECK(std::fabs(*ev.set_az - 270.0) < 1.0, "equator set az");
        CHECK(std::fabs(*ev.rise_dur - 2.13) < 0.05, "equator crossing");
        printf("  equator equinox day length OK (%s, rise az %.1f, "
               "crossing %s)\n", fmtSpan((int)std::lround(d)).c_str(),
               *ev.rise_az,
               fmtDur((int)std::lround(*ev.rise_dur * 60.0)).c_str());
    }

    // 4. Magnetic declination: the 16 official WMM2025 test vectors.
    {
        const struct { double dyear, alt, lat, lon, want; } vectors[16] = {
            { 2025.0,   0.0,  80.0,    0.0,   1.28 },
            { 2025.0,   0.0,   0.0,  120.0,  -0.16 },
            { 2025.0,   0.0, -80.0,  240.0,  68.78 },
            { 2025.0, 100.0,  80.0,    0.0,   0.85 },
            { 2025.0, 100.0,   0.0,  120.0,  -0.15 },
            { 2025.0, 100.0, -80.0,  240.0,  68.21 },
            { 2027.5,   0.0,  80.0,    0.0,   2.59 },
            { 2027.5,   0.0,   0.0,  120.0,  -0.24 },
            { 2027.5,   0.0, -80.0,  240.0,  68.49 },
            { 2027.5, 100.0,  80.0,    0.0,   2.16 },
            { 2027.5, 100.0,   0.0,  120.0,  -0.23 },
            { 2027.5, 100.0, -80.0,  240.0,  67.93 },
            { 2025.0,  28.0,  89.0, -121.0, -99.77 },
            { 2025.0,  48.0,  80.0,  -96.0, -29.91 },
            { 2025.0,  18.0,   0.0,   21.0,   1.29 },
            { 2025.5,   6.0, -36.0, -137.0,  20.28 },
        };
        for (auto& v : vectors) {
            double got = wmmDeclination(v.lat, v.lon, v.dyear, v.alt);
            CHECK(std::fabs(got - v.want) <= 0.01, "WMM vector");
        }
        const WmmModel& mdl = wmmModel();
        CHECK(mdl.name == "WMM2025" && mdl.epoch == 2025.0 && mdl.nmax == 12,
              "model header");
        CHECK(wmmCoeffCount() == 90, "coefficient count");
        Date day{ 2026, 7, 4 };
        CHECK(std::fabs(decimalYear(day) - 2026.50411) < 1e-5, "decimal year");
        const struct { double lat, lon, want; } noaa[4] = {
            { -33.8688, 151.2093, 12.82083 },
            { -34.2195833, 149.36625, 12.29126 },
            { 51.5074, -0.1278, 1.16280 },
            { 64.1466, -21.9426, -11.13349 },
        };
        for (auto& v : noaa) {
            double got = magneticDeclination(v.lat, v.lon, day);
            CHECK(std::fabs(got - v.want) < 0.01, "NOAA calculator pin");
        }
        auto angdiff = [](double x, double y) {
            return std::fabs(fmod(std::fabs(x - y + 180.0), 360.0) - 180.0);
        };
        for (double b : { 0.0, 5.0, 63.4, 180.0, 351.9 }) {
            double m = trueToMagnetic(b, -34.2195833, 149.36625, day);
            CHECK(0.0 <= m && m < 360.0, "magnetic range");
            double back = magneticToTrue(m, -34.2195833, 149.36625, day);
            CHECK(angdiff(back, b) < 1e-9, "inverse pair");
        }
        CHECK(trueToMagnetic(5.0, -34.2195833, 149.36625, day) > 350.0,
              "wrap north");
        CHECK(magneticToTrue(352.7, -34.2195833, 149.36625, day) < 10.0,
              "wrap north back");
        double dBinda = magneticDeclination(-34.2195833, 149.36625, day);
        printf("  magnetic declination OK (16 official WMM vectors; Binda "
               "2026-07-04: %+.2f deg)\n", dBinda);
    }

    // 5. A full 366-day Sydney batch (fixed offset): internal consistency.
    std::vector<Row> rowsSyd = computeRows(-33.8688, 151.2093,
                                           Date{ 2026, 7, 3 }, 366, "fixed",
                                           10.0);
    {
        CHECK(rowsSyd.size() == 366
              && rowsSyd.back().date == (Date{ 2027, 7, 3 }), "batch size");
        for (size_t i = 0; i + 1 < rowsSyd.size(); i++)
            CHECK(dateDiff(rowsSyd[i + 1].date, rowsSyd[i].date) == 1,
                  "consecutive dates");
        auto angdiff = [](double x, double y) {
            return std::fabs(fmod(std::fabs(x - y + 180.0), 360.0) - 180.0);
        };
        double minH = 1e9, maxH = -1e9;
        for (const Row& r : rowsSyd) {
            CHECK(r.rise && r.set && *r.set > *r.rise, "rise<set");
            CHECK(r.day == *r.set - *r.rise, "day = set - rise");
            CHECK(r.off == 600, "offset 600");
            CHECK(r.rise_dur && 145 <= *r.rise_dur && *r.rise_dur <= 185
                  && r.set_dur && 145 <= *r.set_dur && *r.set_dur <= 185,
                  "duration band");
            CHECK(std::abs(*r.rise_dur - *r.set_dur) <= 5, "duration sym");
            CHECK(0.0 < *r.rise_az && *r.rise_az < 180.0
                  && 180.0 < *r.set_az && *r.set_az < 360.0, "az halves");
            CHECK(std::fabs(*r.rise_az + *r.set_az - 360.0) < 1.5, "az sym");
            double d = magneticDeclination(-33.8688, 151.2093, r.date);
            double wantR = fmod(*r.rise_az - d + 360.0, 360.0);
            double wantS = fmod(*r.set_az - d + 360.0, 360.0);
            CHECK(angdiff(wantR, *r.rise_mag) < 0.11, "rise mag");
            CHECK(angdiff(wantS, *r.set_mag) < 0.11, "set mag");
            minH = std::min(minH, r.day / 3600.0);
            maxH = std::max(maxH, r.day / 3600.0);
        }
        CHECK(9.4 < minH && minH < 10.2, "shortest day");
        CHECK(14.0 < maxH && maxH < 14.9, "longest day");
        printf("  366-day batch consistency OK (day length %.2f..%.2f h)\n",
               minH, maxH);
    }

    // 6. System-zone mode runs and yields sane per-day offsets.
    {
        std::vector<Row> rows2 = computeRows(-33.8688, 151.2093,
                                             Date{ 2026, 7, 3 }, 30,
                                             "system", 0.0);
        for (const Row& r : rows2)
            CHECK(-14 * 60 <= r.off && r.off <= 14 * 60, "system offset");
        printf("  system time-zone mode OK (offset today: %s)\n",
               fmtOffset(rows2[0].off).c_str());
    }

    // 7. Manual daylight-saving rules.
    {
        CHECK(nthWeekdayDate(2026, 10, 1, 6) == (Date{ 2026, 10, 4 }), "nth");
        CHECK(nthWeekdayDate(2027, 4, 1, 6) == (Date{ 2027, 4, 4 }), "nth");
        CHECK(nthWeekdayDate(2027, 3, -1, 6) == (Date{ 2027, 3, 28 }),
              "last Sun");
        CHECK(nthWeekdayDate(2026, 11, 1, 6) == (Date{ 2026, 11, 1 }),
              "day 1");
        DstSpec syd;
        syd.hours = 11.0;
        syd.start = Rule{ 1, 6, 10 };
        syd.end = Rule{ 1, 6, 4 };
        std::vector<Row> rowsMan = computeRows(-33.8688, 151.2093,
                                               Date{ 2026, 7, 3 }, 366,
                                               "fixed", 10.0, &syd);
        std::map<long, int> offs;
        for (const Row& r : rowsMan) offs[dateOrdinal(r.date)] = r.off;
        auto offAt = [&](int y, int m, int d) {
            return offs[dateOrdinal(Date{ y, m, d })];
        };
        CHECK(offAt(2026, 10, 3) == 600 && offAt(2026, 10, 4) == 660,
              "Sydney spring");
        CHECK(offAt(2027, 4, 3) == 660 && offAt(2027, 4, 4) == 600,
              "Sydney autumn");
        CHECK(offAt(2026, 7, 3) == 600 && offAt(2026, 12, 21) == 660,
              "Sydney mid");
        DstSpec eu;
        eu.hours = 1.0;
        eu.start = Rule{ -1, 6, 3 };
        eu.end = Rule{ -1, 6, 10 };
        std::vector<Row> rowsEu = computeRows(51.5074, -0.1278,
                                              Date{ 2026, 1, 1 }, 365,
                                              "fixed", 0.0, &eu);
        std::map<long, int> offsEu;
        for (const Row& r : rowsEu) offsEu[dateOrdinal(r.date)] = r.off;
        auto offEu = [&](int y, int m, int d) {
            return offsEu[dateOrdinal(Date{ y, m, d })];
        };
        CHECK(offEu(2026, 3, 28) == 0 && offEu(2026, 3, 29) == 60,
              "EU spring");
        CHECK(offEu(2026, 10, 24) == 60 && offEu(2026, 10, 25) == 0,
              "EU autumn");
        CHECK(offEu(2026, 6, 21) == 60 && offEu(2026, 12, 21) == 0, "EU mid");
        std::vector<Row> rowsSys = computeRows(-33.8688, 151.2093,
                                               Date{ 2026, 7, 3 }, 366,
                                               "system", 0.0);
        bool sameOffs = rowsSys.size() == rowsMan.size();
        for (size_t i = 0; sameOffs && i < rowsSys.size(); i++)
            if (rowsSys[i].off != rowsMan[i].off) sameOffs = false;
        if (sameOffs) {
            CHECK(rowsSys == rowsMan, "system == manual");
            printf("  manual DST rules reproduce the system zone exactly\n");
        } else {
            printf("  (system zone here isn't Sydney-like; equivalence "
                   "check skipped)\n");
        }
    }

    // 8. Raised horizon (valley / hills skyline).
    {
        CHECK(std::fabs(zenithForHorizon(0.0) - ZENITH_OFFICIAL) < 1e-12,
              "flat zenith");
        CHECK(std::fabs(zenithForHorizon(10.0) - 80.357) < 0.01, "Bennett");
        CHECK(parseHorizon("").empty() && parseHorizon("0").empty(), "flat");
        HorizonProfile p5 = parseHorizon("5");
        CHECK(p5.size() == 1 && p5[0].first == 0.0 && p5[0].second == 5.0,
              "uniform 5");
        HorizonProfile prof = parseHorizon("90:4, 270:8");
        CHECK(horizonAlt(prof, 90) == 4.0 && horizonAlt(prof, 270) == 8.0,
              "profile points");
        CHECK(std::fabs(horizonAlt(prof, 0) - 6.0) < 1e-9, "wrap north");
        CHECK(std::fabs(horizonAlt(prof, 180) - 6.0) < 1e-9, "interp south");
        CHECK(horizonAlt({}, 123.4) == 0.0, "flat alt");
        for (const char* bad : { "abc", "90:4, x", "5, 6", "0:99", "400:5" })
            CHECK(expectValueError([&] { parseHorizon(bad); }), bad);
        SunEvents flat = sunEvents(Date{ 2026, 12, 21 }, -33.8688, 151.2093,
                                   11.0);
        HorizonProfile hills5 = parseHorizon("5");
        SunEvents hills = sunEvents(Date{ 2026, 12, 21 }, -33.8688, 151.2093,
                                    11.0, &hills5);
        double dRise = *hills.rise - *flat.rise;
        double dSet = *flat.set - *hills.set;
        CHECK(15.0 < dRise && dRise < 60.0 && 15.0 < dSet && dSet < 60.0,
              "hills delay");
        CHECK(2.0 < *flat.rise_az - *hills.rise_az
              && *flat.rise_az - *hills.rise_az < 20.0, "rise az shift");
        CHECK(2.0 < *hills.set_az - *flat.set_az
              && *hills.set_az - *flat.set_az < 20.0, "set az shift");
        CHECK(hills.rise_dur && 2.0 < *hills.rise_dur && *hills.rise_dur < 4.0,
              "hills rise dur");
        CHECK(hills.set_dur && 2.0 < *hills.set_dur && *hills.set_dur < 4.0,
              "hills set dur");
        HorizonProfile empty;
        CHECK(sunEvents(Date{ 2026, 12, 21 }, -33.8688, 151.2093, 11.0,
                        &empty) == flat, "explicit flat == default");
        HorizonProfile ridge = parseHorizon("12");
        SunEvents nordic = sunEvents(Date{ 2026, 12, 21 }, 60.0, 10.0, 1.0,
                                     &ridge);
        CHECK(nordic.polar == "night" && !nordic.rise, "nordic valley");
        std::vector<Row> rowsHz = computeRows(-33.8688, 151.2093,
                                              Date{ 2026, 7, 3 }, 30, "fixed",
                                              10.0, nullptr, &hills5);
        for (size_t i = 0; i < rowsHz.size(); i++)
            CHECK(rowsHz[i].day < rowsSyd[i].day, "hills shorten days");
        printf("  raised-horizon skyline OK (5 deg hills: Sydney solstice "
               "rise %s -> %s)\n",
               fmtClock((int)std::lround(*flat.rise * 60)).c_str(),
               fmtClock((int)std::lround(*hills.rise * 60)).c_str());
    }

    // 9. Text table round-trip, including polar rows and legacy files.
    {
        Meta meta;
        meta.generated = "2026-07-03 00:00:00";
        meta.location = "Testville";
        meta.lat = -33.8688;
        meta.lon = 151.2093;
        meta.tz_desc = "Fixed UTC offset +10:00";
        std::string text = buildTableText(rowsSyd, meta);
        Meta metaB;
        std::vector<Row> rowsB = parseTableText(text, metaB);
        CHECK(rowsB == rowsSyd, "round-trip rows");
        CHECK(metaB.lat && std::fabs(*metaB.lat - *meta.lat) < 1e-9, "lat");
        CHECK(metaB.lon && std::fabs(*metaB.lon - *meta.lon) < 1e-9, "lon");
        CHECK(metaB.location == "Testville", "location");
        std::vector<Row> rows3 = computeRows(78.2232, 15.6267,
                                             Date{ 2026, 10, 1 }, 150,
                                             "fixed", 1.0);
        bool anyPolar = false, magsNull = true;
        for (const Row& r : rows3) {
            if (!r.rise && r.day == 0) anyPolar = true;
            if (!r.rise && (r.rise_mag || r.set_mag)) magsNull = false;
        }
        CHECK(anyPolar && magsNull, "polar rows");
        std::string text3 = buildTableText(rows3, meta);
        Meta m3;
        CHECK(parseTableText(text3, m3) == rows3, "polar round-trip");
        bool sawDashes = false;
        std::istringstream in3(text3);
        std::string ln;
        while (std::getline(in3, ln))
            if (!ln.empty() && ln[0] != '#'
                && ln.find(" ---") != std::string::npos)
                sawDashes = true;
        CHECK(sawDashes, "polar '---' azimuths");
        const struct { std::optional<int> sec; const char* txt; } durPairs[6] = {
            { 174, "02:54" }, { 2421, "40:21" }, { 0, "00:00" },
            { 3599, "59:59" }, { 4530, "1:15:30" }, { std::nullopt, "--:--" },
        };
        for (auto& dp : durPairs) {
            CHECK(fmtDur(dp.sec) == dp.txt, dp.txt);
            CHECK(parseDur(dp.txt) == dp.sec, dp.txt);
        }
        CHECK(parseDur("00:02:54") == 174, "HH:MM:SS interlude");
        CHECK(!parseDur("--:--:--").has_value(), "dashes");
        Meta lm;
        std::string legacy11 = std::string(TABLE_HEADER) + "\n"
            "2026-07-03    07:00:57  00:02:54    62.6    49.8   16:57:53"
            "  00:02:54   297.4   284.6   09:56:56   +10:00\n";
        Row l11 = parseTableText(legacy11, lm)[0];
        CHECK(l11.rise_dur == 174 && l11.set_dur == 174, "legacy 11");
        std::string legacy9 = std::string(TABLE_HEADER) + "\n"
            "2026-07-03    07:00:57    62.6    49.8   16:57:53   297.4"
            "   284.6   09:56:56   +10:00\n";
        Row l9 = parseTableText(legacy9, lm)[0];
        CHECK(l9.rise == hms(7, 0, 57) && l9.rise_az == 62.6, "legacy 9");
        CHECK(l9.rise_mag == 49.8 && l9.set_mag == 284.6, "legacy 9 mag");
        CHECK(!l9.rise_dur && !l9.set_dur, "legacy 9 dur");
        std::string legacy7 = std::string(TABLE_HEADER) + "\n"
            "2026-07-03    07:00:57    63.4   16:57:53   296.6   09:56:56"
            "   +10:00\n";
        Row l7 = parseTableText(legacy7, lm)[0];
        CHECK(l7.rise_az == 63.4 && l7.set_az == 296.6, "legacy 7");
        CHECK(!l7.rise_mag && !l7.set_mag && !l7.rise_dur && !l7.set_dur,
              "legacy 7 null");
        std::string legacy5 = std::string(TABLE_HEADER) + "\n"
            "2026-07-03    07:00:57   16:57:53   09:56:56   +10:00\n";
        Row l5 = parseTableText(legacy5, lm)[0];
        CHECK(l5.rise == hms(7, 0, 57) && l5.off == 600, "legacy 5");
        CHECK(!l5.rise_az && !l5.set_az && !l5.rise_mag && !l5.set_mag
              && !l5.rise_dur && !l5.set_dur, "legacy 5 null");
        printf("  table text round-trip OK (incl. polar rows, legacy "
               "files)\n");
    }

    // 10. The entry parsers, including web-pasted coordinates.
    {
        const struct { const char* s; double want; } tzCases[8] = {
            { "10", 10.0 }, { "+10", 10.0 }, { "-3.5", -3.5 },
            { "9:30", 9.5 }, { "-3:30", -3.5 }, { "5.75", 5.75 },
            { "0", 0.0 }, { "\xE2\x88\x92" "3:30", -3.5 },
        };
        for (auto& c : tzCases)
            CHECK(parseTzHours(c.s) == c.want, c.s);
        for (const char* bad : { "abc", "", "15", "-15", "10:xx" })
            CHECK(expectValueError([&] { parseTzHours(bad); }), bad);
        double binda = 34.0 + 13.0 / 60.0 + 10.5 / 3600.0;
        struct AngleCase { const char* s; double want; };
        std::vector<AngleCase> angleCases = {
            { "-34.2195833", -34.2195833 },
            { "\xE2\x88\x92" "34.2195833", -34.2195833 },     // unicode minus
            { "-34.2195833\xC2\xB0", -34.2195833 },           // degree sign
            { "\xE2\x88\x92" "34.2195833\xC2\xA0", -34.2195833 },  // hard space
            { "34.2195833 S", -34.2195833 },
            { "-34.2195833 s", -34.2195833 },                 // S beats sign
            { "149.36625 E", 149.36625 },
            { "W 71.06", -71.06 },
            { "34\xC2\xB0" "13\xE2\x80\xB2" "10.5\xE2\x80\xB3" "S", -binda },
            { "34 13 10.5 S", -binda },
            { "34\xC2\xB0" "13'10.5\" S", -binda },
            { "0", 0.0 },
        };
        for (auto& c : angleCases) {
            double got = parseAngle(c.s);
            CHECK(std::fabs(got - c.want) < 1e-9, c.s);
        }
        for (const char* bad : { "", "abc", "34,22", "12 61", "1 2 3 4",
                                 "12 30 -5" })
            CHECK(expectValueError([&] { parseAngle(bad); }), bad);
        printf("  entry parsers OK (incl. web-pasted coordinates)\n");
    }

    // 11. Window-position parsing + the file-dialog default folder.
    {
        CHECK(wmPosition("1180x762+208+208") == "+208+208", "wm pos");
        CHECK(wmPosition("800x600-1200+30") == "-1200+30", "wm neg x");
        CHECK(wmPosition("800x600+30-1200") == "+30-1200", "wm neg y");
        CHECK(wmPosition("garbage") == "+100+100", "wm garbage");
        std::string dialogDir = defaultFileDir();
        CHECK(isDir(dialogDir), dialogDir.c_str());
        printf("  window-position parsing OK; file dialogs default to %s\n",
               dialogDir.c_str());
    }

    // 12. Headless app: compute + save/load round-trip; the file
    //     regenerates itself.
    {
        auto sys1 = parseTzDesc("System local zone (AEST) - DST aware; x");
        CHECK(sys1 && sys1->tz_mode == "system", "tz desc system");
        auto fx = parseTzDesc("Fixed UTC offset +09:30 (no daylight-saving "
                              "adjustments)");
        CHECK(fx && fx->tz_mode == "fixed" && fx->fixed_hours == 9.5
              && !fx->dst_enabled, "tz desc fixed");
        CHECK(!parseTzDesc("total nonsense"), "tz desc nonsense");
        auto hz1 = parseHorizonDesc("flat 0.0 deg (open astronomical "
                                    "horizon)");
        CHECK(hz1 && *hz1 == "0", "hz desc flat");
        auto hz2 = parseHorizonDesc("uniform 5.5 deg above the astronomical "
                                    "horizon");
        CHECK(hz2 && *hz2 == "5.5", "hz desc uniform");
        CHECK(!parseHorizonDesc("mystery"), "hz desc mystery");
        Sun2SetApp app(false, false);
        app.location = "Testville";
        app.lat = 40.0;
        app.lon = -75.0;
        app.tz_mode = "fixed";
        app.fixed_hours = -5.0;
        app.dst_enabled = true;
        app.dst_hours = -4.0;
        app.dst_start = Rule{ 2, 6, 3 };            // US-style rules
        app.dst_end = Rule{ 1, 6, 11 };
        app.horizon_str = "90:4, 270:8";
        app.start = Date{ 2026, 3, 1 };
        app.days = 30;
        app.compute();
        CHECK(app.autosave().empty(), "persist=false writes nothing");
        const char* tmpBase = getenv("TEMP");
#ifndef _WIN32
        if (!tmpBase) tmpBase = "/tmp";
#endif
        std::string path = std::string(tmpBase ? tmpBase : ".")
                           + "/sun2set_selftest_almanac.txt";
        app.saveTextFile(path);
        Sun2SetApp app2(false, false);
        int n = app2.loadTextFile(path);
        remove(path.c_str());
        CHECK(n == 30 && app2.rows == app.rows, "load round-trip");
        CHECK(app2.location == "Testville", "location back");
        CHECK(std::fabs(app2.lat - 40.0) < 1e-9
              && std::fabs(app2.lon - -75.0) < 1e-9, "coords back");
        CHECK(app2.tz_mode == "fixed" && app2.fixed_hours == -5.0,
              "tz back");
        CHECK(app2.dst_enabled && app2.dst_hours == -4.0, "dst back");
        CHECK(app2.dst_start == (Rule{ 2, 6, 3 })
              && app2.dst_end == (Rule{ 1, 6, 11 }), "rules back");
        CHECK(app2.horizon_str == "90:4, 270:8", "skyline back");
        CHECK(app2.start == (Date{ 2026, 3, 1 }) && app2.days == 30,
              "range back");
        app2.compute();
        CHECK(app2.rows == app.rows, "file regenerates itself");
        printf("  headless save/load round-trip OK (the file regenerates "
               "itself)\n");
    }

    if (gSelftestFailures == 0)
        printf("All Sun2Set self-tests passed.\n");
    else
        fprintf(stderr, "selftest FAILED: %d check(s)\n", gSelftestFailures);
    return gSelftestFailures == 0 ? 0 : 1;
}

// ============================================================================
// Windows backend: Win32 window + GDI rasterizer + common dialogs.
// ============================================================================
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#undef RGB

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0,
                                NULL, NULL);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL,
                        NULL);
    return s;
}

static void makeAppDir() {
    CreateDirectoryW(widen(appDataDir()).c_str(), NULL);
}

static bool isDir(const std::string& path) {
    DWORD attrs = GetFileAttributesW(widen(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES
           && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// The user's real Documents folder (often OneDrive-redirected) from the
// User Shell Folders registry key — never guess ~/Documents first.
static std::string defaultFileDir() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion"
                      L"\\Explorer\\User Shell Folders", 0, KEY_READ, &key)
        == ERROR_SUCCESS) {
        wchar_t raw[MAX_PATH] = {};
        DWORD size = sizeof raw;
        DWORD type = 0;
        LONG rc = RegQueryValueExW(key, L"Personal", NULL, &type, (BYTE*)raw,
                                   &size);
        RegCloseKey(key);
        if (rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
            wchar_t expanded[MAX_PATH] = {};
            ExpandEnvironmentStringsW(raw, expanded, MAX_PATH);
            std::string path = narrow(expanded);
            if (isDir(path)) return path;
        }
    }
    std::string docs = homeDir() + "\\Documents";
    return isDir(docs) ? docs : homeDir();
}

static int systemOffsetMinutes(const Date& date) {
    SYSTEMTIME local{};
    local.wYear = (WORD)date.y;
    local.wMonth = (WORD)date.m;
    local.wDay = (WORD)date.d;
    local.wHour = 12;
    SYSTEMTIME utc{};
    if (!TzSpecificLocalTimeToSystemTime(NULL, &local, &utc)) return 0;
    FILETIME fl{}, fu{};
    SystemTimeToFileTime(&local, &fl);
    SystemTimeToFileTime(&utc, &fu);
    ULARGE_INTEGER ul{}, uu{};
    ul.LowPart = fl.dwLowDateTime;
    ul.HighPart = fl.dwHighDateTime;
    uu.LowPart = fu.dwLowDateTime;
    uu.HighPart = fu.dwHighDateTime;
    long long diff = (long long)(ul.QuadPart - uu.QuadPart);   // 100 ns units
    return (int)(diff / 600000000LL);
}

static std::string systemZoneNames() {
    TIME_ZONE_INFORMATION tzi{};
    GetTimeZoneInformation(&tzi);
    std::string std_ = narrow(tzi.StandardName);
    std::string dst_ = narrow(tzi.DaylightName);
    if (std_.empty()) return dst_;
    if (dst_.empty() || dst_ == std_) return std_;
    return std_ + " / " + dst_;
}

static COLORREF cref(RGB c) {
    return (COLORREF)(c.r | (c.g << 8) | (c.b << 16));
}

class GdiRenderer {
public:
    HDC memdc = NULL;
    HBITMAP bmp = NULL;
    std::map<int, HFONT> fonts;
    int clipDepth = 0;

    void ensure(HDC winDC) {
        if (memdc) return;
        memdc = CreateCompatibleDC(winDC);
        bmp = CreateCompatibleBitmap(winDC, CLIENT_W, CLIENT_H);
        SelectObject(memdc, bmp);
        SetBkMode(memdc, TRANSPARENT);
    }

    HFONT font(int size, bool bold) {
        int key = size * 2 + (bold ? 1 : 0);
        auto it = fonts.find(key);
        if (it != fonts.end()) return it->second;
        HFONT f = CreateFontW(-MulDiv(size, 96, 72), 0, 0, 0,
                              bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH | FF_MODERN, L"Consolas");
        fonts[key] = f;
        return f;
    }

    double charWidth(int size, bool bold) {
        HDC dc = memdc ? memdc : GetDC(NULL);
        HGDIOBJ old = SelectObject(dc, font(size, bold));
        SIZE ext{};
        GetTextExtentPoint32W(dc, L"0", 1, &ext);
        SelectObject(dc, old);
        if (!memdc) ReleaseDC(NULL, dc);
        return (double)ext.cx;
    }

    void draw(const std::vector<Cmd>& scene) {
        clipDepth = 0;
        for (const Cmd& c : scene) {
            int x0 = (int)std::lround(c.x0), y0 = (int)std::lround(c.y0);
            int x1 = (int)std::lround(c.x1), y1 = (int)std::lround(c.y1);
            switch (c.type) {
            case Cmd::ClipPush:
                SaveDC(memdc);
                IntersectClipRect(memdc, x0, y0, x1, y1);
                clipDepth++;
                break;
            case Cmd::ClipPop:
                if (clipDepth > 0) {
                    RestoreDC(memdc, -1);
                    clipDepth--;
                }
                break;
            case Cmd::Line: {
                HPEN pen = CreatePen(c.dash ? PS_DOT : PS_SOLID,
                                     (int)std::lround(c.width),
                                     cref(c.outline));
                HGDIOBJ old = SelectObject(memdc, pen);
                MoveToEx(memdc, x0, y0, NULL);
                LineTo(memdc, x1, y1);
                SelectObject(memdc, old);
                DeleteObject(pen);
                break;
            }
            case Cmd::Rect: {
                if (c.hasFill) {
                    RECT r{ x0, y0, x1, y1 };
                    HBRUSH b = CreateSolidBrush(cref(c.fill));
                    FillRect(memdc, &r, b);
                    DeleteObject(b);
                }
                if (c.hasOutline) {
                    HPEN pen = CreatePen(PS_SOLID, (int)std::lround(c.width),
                                         cref(c.outline));
                    HGDIOBJ oldPen = SelectObject(memdc, pen);
                    HGDIOBJ oldBrush = SelectObject(memdc,
                                                    GetStockObject(NULL_BRUSH));
                    Rectangle(memdc, x0, y0, x1 + 1, y1 + 1);
                    SelectObject(memdc, oldBrush);
                    SelectObject(memdc, oldPen);
                    DeleteObject(pen);
                }
                break;
            }
            case Cmd::Oval: {
                HPEN pen = c.hasOutline
                    ? CreatePen(PS_SOLID, (int)std::lround(c.width),
                                cref(c.outline))
                    : (HPEN)GetStockObject(NULL_PEN);
                HBRUSH brush = c.hasFill ? CreateSolidBrush(cref(c.fill))
                                         : (HBRUSH)GetStockObject(NULL_BRUSH);
                HGDIOBJ oldPen = SelectObject(memdc, pen);
                HGDIOBJ oldBrush = SelectObject(memdc, brush);
                Ellipse(memdc, x0, y0, x1 + 1, y1 + 1);
                SelectObject(memdc, oldBrush);
                SelectObject(memdc, oldPen);
                if (c.hasFill) DeleteObject(brush);
                if (c.hasOutline) DeleteObject(pen);
                break;
            }
            case Cmd::RoundRect: {
                HPEN pen = c.hasOutline
                    ? CreatePen(PS_SOLID, (int)std::lround(c.width),
                                cref(c.outline))
                    : (HPEN)GetStockObject(NULL_PEN);
                HBRUSH brush = c.hasFill ? CreateSolidBrush(cref(c.fill))
                                         : (HBRUSH)GetStockObject(NULL_BRUSH);
                HGDIOBJ oldPen = SelectObject(memdc, pen);
                HGDIOBJ oldBrush = SelectObject(memdc, brush);
                int d = (int)std::lround(c.radius * 2);
                RoundRect(memdc, x0, y0, x1, y1, d, d);
                SelectObject(memdc, oldBrush);
                SelectObject(memdc, oldPen);
                if (c.hasFill) DeleteObject(brush);
                if (c.hasOutline) DeleteObject(pen);
                break;
            }
            case Cmd::Poly: {
                std::vector<POINT> pts;
                for (auto& p : c.pts)
                    pts.push_back(POINT{ (LONG)std::lround(p.first),
                                         (LONG)std::lround(p.second) });
                HBRUSH brush = CreateSolidBrush(cref(c.fill));
                HGDIOBJ oldPen = SelectObject(memdc, GetStockObject(NULL_PEN));
                HGDIOBJ oldBrush = SelectObject(memdc, brush);
                Polygon(memdc, pts.data(), (int)pts.size());
                SelectObject(memdc, oldBrush);
                SelectObject(memdc, oldPen);
                DeleteObject(brush);
                break;
            }
            case Cmd::Polyline: {
                std::vector<POINT> pts;
                for (auto& p : c.pts)
                    pts.push_back(POINT{ (LONG)std::lround(p.first),
                                         (LONG)std::lround(p.second) });
                HPEN pen = CreatePen(PS_SOLID, (int)std::lround(c.width),
                                     cref(c.outline));
                HGDIOBJ old = SelectObject(memdc, pen);
                ::Polyline(memdc, pts.data(), (int)pts.size());
                SelectObject(memdc, old);
                DeleteObject(pen);
                break;
            }
            case Cmd::Text: {
                std::wstring w = widen(c.text);
                HGDIOBJ oldFont = SelectObject(memdc, font(c.size, c.bold));
                SIZE ext{};
                GetTextExtentPoint32W(memdc, w.c_str(), (int)w.size(), &ext);
                int tx = x0, ty = y0 - ext.cy / 2;
                if (c.anchor == Cmd::Center) tx = x0 - ext.cx / 2;
                else if (c.anchor == Cmd::E) tx = x0 - ext.cx;
                else if (c.anchor == Cmd::N) { tx = x0 - ext.cx / 2; ty = y0; }
                else if (c.anchor == Cmd::NW) ty = y0;
                SetTextColor(memdc, cref(c.fill));
                TextOutW(memdc, tx, ty, w.c_str(), (int)w.size());
                SelectObject(memdc, oldFont);
                break;
            }
            }
        }
        while (clipDepth > 0) {
            RestoreDC(memdc, -1);
            clipDepth--;
        }
    }
};

struct App {
    Sun2SetApp* core = nullptr;
    GdiRenderer renderer;
} gApp;

static Key mapVk(WPARAM vk) {
    switch (vk) {
    case VK_LEFT: return Key::Left;
    case VK_RIGHT: return Key::Right;
    case VK_UP: return Key::Up;
    case VK_DOWN: return Key::Down;
    case VK_HOME: return Key::Home;
    case VK_END: return Key::End;
    case VK_PRIOR: return Key::PgUp;
    case VK_NEXT: return Key::PgDn;
    case VK_TAB: return Key::Tab;
    case VK_RETURN: return Key::Enter;
    case VK_ESCAPE: return Key::Escape;
    case VK_BACK: return Key::Backspace;
    case VK_DELETE: return Key::Delete;
    default: return Key::None;
    }
}

static std::string clipboardText(HWND hwnd) {
    if (!OpenClipboard(hwnd)) return "";
    std::string out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* w = (const wchar_t*)GlobalLock(h);
        if (w) {
            out = narrow(w);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

static std::string runFileDialog(HWND hwnd, bool save,
                                 const std::string& dir,
                                 const std::string& name) {
    wchar_t file[MAX_PATH] = {};
    std::wstring wname = widen(name);
    wcsncpy_s(file, wname.c_str(), _TRUNCATE);
    std::wstring wdir = widen(dir);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Text files\0*.txt\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = wdir.empty() ? NULL : wdir.c_str();
    ofn.lpstrDefExt = L"txt";
    ofn.lpstrTitle = save ? L"Save almanac as…"
                          : L"Load a saved almanac…";
    ofn.Flags = OFN_NOCHANGEDIR
                | (save ? OFN_OVERWRITEPROMPT
                        : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? narrow(file) : "";
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Sun2SetApp* core = gApp.core;
    switch (msg) {
    case WM_TIMER:
        if (core) {
            core->tick();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        gApp.renderer.ensure(dc);
        if (core) gApp.renderer.draw(core->scene);
        BitBlt(dc, 0, 0, CLIENT_W, CLIENT_H, gApp.renderer.memdc, 0, 0,
               SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        if (core) {
            Key k = mapVk(wp);
            if (k != Key::None) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                core->handleKey(k, shift);
            }
        }
        return 0;
    case WM_CHAR: {
        if (!core) return 0;
        wchar_t ch = (wchar_t)wp;
        if (ch == 0x16) {                       // Ctrl+V
            core->pasteText(clipboardText(hwnd));
            return 0;
        }
        if (ch >= 0x20 && ch != 0x7F) {
            std::wstring w(1, ch);
            core->charInput(narrow(w));
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (core) core->onMouseMove((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_MOUSEWHEEL:
        if (core) {
            double delta = GET_WHEEL_DELTA_WPARAM(wp) / 120.0;
            bool shift = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0;
            core->onWheel(delta, shift);
        }
        return 0;
    case WM_MOUSEHWHEEL:
        if (core)
            core->onWheel(-GET_WHEEL_DELTA_WPARAM(wp) / 120.0, true);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        if (core) core->onClick((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_CLOSE: {
        if (core) {
            RECT r;
            if (GetWindowRect(hwnd, &r)) core->onClose(r.left, r.top);
        }
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int runWindowsApp(HINSTANCE hInstance) {
    Sun2SetApp core(true, /*persist=*/true);
    gApp.core = &core;
    core.charW = [](int size, bool bold) {
        return gApp.renderer.charWidth(size, bold);
    };

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"Sun2SetWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r{ 0, 0, CLIENT_W, CLIENT_H };
    AdjustWindowRect(&r, style, FALSE);
    int winW = r.right - r.left, winH = r.bottom - r.top;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int sx, sy;
    if (core.savedWinPos(sx, sy)) { x = sx; y = sy; }

    HWND hwnd = CreateWindowW(L"Sun2SetWnd",
                              L"Sun2Set — sunrise & sunset almanac",
                              style, x, y, winW, winH, NULL, NULL, hInstance,
                              NULL);
    if (!hwnd) return 1;
    core.fileDialog = [hwnd](bool save, const std::string& dir,
                             const std::string& name) {
        return runFileDialog(hwnd, save, dir, name);
    };
    core.requestClose = [hwnd]() { PostMessageW(hwnd, WM_CLOSE, 0, 0); };

    core.onCalculate();                        // show results immediately
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetTimer(hwnd, 1, 33, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    for (int i = 1; i < __argc; i++) {
        if (strcmp(__argv[i], "--selftest") == 0) {
            HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
            if (out == NULL || out == INVALID_HANDLE_VALUE) {
                if (AttachConsole(ATTACH_PARENT_PROCESS)) {
                    FILE* f;
                    freopen_s(&f, "CONOUT$", "w", stdout);
                    freopen_s(&f, "CONOUT$", "w", stderr);
                }
            }
            int rc = runSelftest();
            fflush(stdout);
            fflush(stderr);
            return rc;
        }
    }
    return runWindowsApp(hInstance);
}

#endif // _WIN32

// ============================================================================
// macOS backend: Cocoa window + NSBezierPath rasterizer + NSSave/OpenPanel.
// This file is Objective-C++ here — build with `clang++ -x objective-c++`.
// ============================================================================
#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include <sys/stat.h>
#include <unistd.h>

static void makeAppDir() {
    mkdir(appDataDir().c_str(), 0755);
}

static bool isDir(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::string defaultFileDir() {
    std::string docs = homeDir() + "/Documents";
    return isDir(docs) ? docs : homeDir();
}

static int systemOffsetMinutes(const Date& date) {
    struct tm lt{};
    lt.tm_year = date.y - 1900;
    lt.tm_mon = date.m - 1;
    lt.tm_mday = date.d;
    lt.tm_hour = 12;
    lt.tm_isdst = -1;
    time_t t = mktime(&lt);
    if (t == (time_t)-1) return 0;
    struct tm resolved{};
    localtime_r(&t, &resolved);
    return (int)(resolved.tm_gmtoff / 60);
}

static std::string systemZoneNames() {
    int year = dateToday().y;
    std::string names;
    for (int m : { 1, 7 }) {
        struct tm lt{};
        lt.tm_year = year - 1900;
        lt.tm_mon = m - 1;
        lt.tm_mday = 1;
        lt.tm_hour = 12;
        lt.tm_isdst = -1;
        time_t t = mktime(&lt);
        struct tm resolved{};
        localtime_r(&t, &resolved);
        if (resolved.tm_zone) {
            std::string z = resolved.tm_zone;
            if (names.empty()) names = z;
            else if (names.find(z) == std::string::npos)
                names += " / " + z;
        }
    }
    return names.empty() ? "local" : names;
}

static NSColor* nsColor(RGB c) {
    return [NSColor colorWithSRGBRed:c.r / 255.0 green:c.g / 255.0
                                blue:c.b / 255.0 alpha:1.0];
}

static NSFont* sceneFont(int size, bool bold) {
    NSFont* f = [NSFont fontWithName:(bold ? @"Menlo-Bold" : @"Menlo-Regular")
                                size:size];
    if (!f) f = [NSFont userFixedPitchFontOfSize:size];
    return f;
}

@interface SunView : NSView {
@public
    Sun2SetApp* core;
}
@end

@implementation SunView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    for (NSTrackingArea* a in [self.trackingAreas copy])
        [self removeTrackingArea:a];
    NSTrackingArea* area = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect
             options:(NSTrackingMouseMoved | NSTrackingActiveInKeyWindow
                      | NSTrackingInVisibleRect)
               owner:self
            userInfo:nil];
    [self addTrackingArea:area];
}

- (void)drawRect:(NSRect)dirty {
    if (!core) return;
    int clipDepth = 0;
    for (const Cmd& c : core->scene) {
        NSRect r = NSMakeRect(c.x0, c.y0, c.x1 - c.x0, c.y1 - c.y0);
        switch (c.type) {
        case Cmd::ClipPush:
            [NSGraphicsContext saveGraphicsState];
            NSRectClip(r);
            clipDepth++;
            break;
        case Cmd::ClipPop:
            if (clipDepth > 0) {
                [NSGraphicsContext restoreGraphicsState];
                clipDepth--;
            }
            break;
        case Cmd::Line: {
            NSBezierPath* p = [NSBezierPath bezierPath];
            [p moveToPoint:NSMakePoint(c.x0, c.y0)];
            [p lineToPoint:NSMakePoint(c.x1, c.y1)];
            p.lineWidth = c.width;
            if (c.dash) {
                CGFloat pattern[2] = { 2, 3 };
                [p setLineDash:pattern count:2 phase:0];
            }
            [nsColor(c.outline) setStroke];
            [p stroke];
            break;
        }
        case Cmd::Rect: {
            if (c.hasFill) { [nsColor(c.fill) setFill]; NSRectFill(r); }
            if (c.hasOutline) {
                NSBezierPath* p = [NSBezierPath bezierPathWithRect:r];
                p.lineWidth = c.width;
                [nsColor(c.outline) setStroke];
                [p stroke];
            }
            break;
        }
        case Cmd::Oval: {
            NSBezierPath* p = [NSBezierPath bezierPathWithOvalInRect:r];
            if (c.hasFill) { [nsColor(c.fill) setFill]; [p fill]; }
            if (c.hasOutline) {
                p.lineWidth = c.width;
                [nsColor(c.outline) setStroke];
                [p stroke];
            }
            break;
        }
        case Cmd::RoundRect: {
            NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect:r
                                  xRadius:c.radius yRadius:c.radius];
            if (c.hasFill) { [nsColor(c.fill) setFill]; [p fill]; }
            if (c.hasOutline) {
                p.lineWidth = c.width;
                [nsColor(c.outline) setStroke];
                [p stroke];
            }
            break;
        }
        case Cmd::Poly: {
            if (c.pts.size() < 3) break;
            NSBezierPath* p = [NSBezierPath bezierPath];
            [p moveToPoint:NSMakePoint(c.pts[0].first, c.pts[0].second)];
            for (size_t i = 1; i < c.pts.size(); i++)
                [p lineToPoint:NSMakePoint(c.pts[i].first, c.pts[i].second)];
            [p closePath];
            [nsColor(c.fill) setFill];
            [p fill];
            break;
        }
        case Cmd::Polyline: {
            if (c.pts.size() < 2) break;
            NSBezierPath* p = [NSBezierPath bezierPath];
            [p moveToPoint:NSMakePoint(c.pts[0].first, c.pts[0].second)];
            for (size_t i = 1; i < c.pts.size(); i++)
                [p lineToPoint:NSMakePoint(c.pts[i].first, c.pts[i].second)];
            p.lineWidth = c.width;
            [nsColor(c.outline) setStroke];
            [p stroke];
            break;
        }
        case Cmd::Text: {
            NSString* s = [NSString stringWithUTF8String:c.text.c_str()];
            if (!s) break;
            NSDictionary* attrs = @{
                NSFontAttributeName: sceneFont(c.size, c.bold),
                NSForegroundColorAttributeName: nsColor(c.fill),
            };
            NSSize ext = [s sizeWithAttributes:attrs];
            double tx = c.x0, ty = c.y0 - ext.height / 2;
            if (c.anchor == Cmd::Center) tx = c.x0 - ext.width / 2;
            else if (c.anchor == Cmd::E) tx = c.x0 - ext.width;
            else if (c.anchor == Cmd::N) { tx = c.x0 - ext.width / 2; ty = c.y0; }
            else if (c.anchor == Cmd::NW) ty = c.y0;
            [s drawAtPoint:NSMakePoint(tx, ty) withAttributes:attrs];
            break;
        }
        }
    }
    while (clipDepth > 0) {
        [NSGraphicsContext restoreGraphicsState];
        clipDepth--;
    }
}

- (void)keyDown:(NSEvent*)e {
    if (!core) return;
    // Cmd+V pastes into the focused entry.
    if (([e modifierFlags] & NSEventModifierFlagCommand)
        && [[e charactersIgnoringModifiers] isEqualToString:@"v"]) {
        NSString* clip = [[NSPasteboard generalPasteboard]
            stringForType:NSPasteboardTypeString];
        if (clip) core->pasteText([clip UTF8String]);
        return;
    }
    NSString* raw = [e charactersIgnoringModifiers];
    unichar ch = [raw length] ? [raw characterAtIndex:0] : 0;
    bool shift = ([e modifierFlags] & NSEventModifierFlagShift) != 0;
    switch (ch) {
    case NSUpArrowFunctionKey: core->handleKey(Key::Up, shift); return;
    case NSDownArrowFunctionKey: core->handleKey(Key::Down, shift); return;
    case NSLeftArrowFunctionKey: core->handleKey(Key::Left, shift); return;
    case NSRightArrowFunctionKey: core->handleKey(Key::Right, shift); return;
    case NSHomeFunctionKey: core->handleKey(Key::Home, shift); return;
    case NSEndFunctionKey: core->handleKey(Key::End, shift); return;
    case NSPageUpFunctionKey: core->handleKey(Key::PgUp, shift); return;
    case NSPageDownFunctionKey: core->handleKey(Key::PgDn, shift); return;
    case '\t': case 0x19: core->handleKey(Key::Tab, shift || ch == 0x19); return;
    case '\r': case 0x03: core->handleKey(Key::Enter, shift); return;
    case 27: core->handleKey(Key::Escape, shift); return;
    case 0x7F: case '\b': core->handleKey(Key::Backspace, shift); return;
    case NSDeleteFunctionKey: core->handleKey(Key::Delete, shift); return;
    default: break;
    }
    NSString* chars = [e characters];
    if ([chars length]) {
        unichar c0 = [chars characterAtIndex:0];
        if (c0 >= 0x20 && (c0 < 0xF700 || c0 > 0xF8FF))
            core->charInput([chars UTF8String]);
    }
}

- (void)mouseDown:(NSEvent*)e {
    if (!core) return;
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    core->onClick(p.x, p.y);
}

- (void)mouseMoved:(NSEvent*)e {
    if (!core) return;
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    core->onMouseMove(p.x, p.y);
}

- (void)scrollWheel:(NSEvent*)e {
    if (!core) return;
    bool shift = ([e modifierFlags] & NSEventModifierFlagShift) != 0;
    if (shift || std::fabs([e deltaX]) > std::fabs([e deltaY]))
        core->onWheel([e deltaX] != 0 ? [e deltaX] : [e deltaY], true);
    else
        core->onWheel([e deltaY], false);
}
@end

@interface SunAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@public
    Sun2SetApp* core;
    NSWindow* window;
    NSTimer* timer;
}
@end

@implementation SunAppDelegate
- (void)windowWillClose:(NSNotification*)n {
    if (core && window) {
        NSRect f = [window frame];
        NSRect screen = [[NSScreen mainScreen] frame];
        core->onClose((int)f.origin.x,
                      (int)(screen.size.height
                            - (f.origin.y + f.size.height)));
    }
    [timer invalidate];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
    return YES;
}
- (void)tick:(NSTimer*)t {
    if (core) {
        core->tick();
        [[window contentView] setNeedsDisplay:YES];
    }
}
@end

static int runMacApp() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSMenu* bar = [[NSMenu alloc] init];
        NSMenuItem* appItem = [[NSMenuItem alloc] init];
        [bar addItem:appItem];
        NSMenu* appMenu = [[NSMenu alloc] init];
        [appMenu addItemWithTitle:@"Quit Sun2Set" action:@selector(terminate:)
                    keyEquivalent:@"q"];
        [appItem setSubmenu:appMenu];
        [NSApp setMainMenu:bar];

        Sun2SetApp core(true, /*persist=*/true);
        core.charW = [](int size, bool bold) {
            NSDictionary* attrs = @{
                NSFontAttributeName: sceneFont(size, bold)
            };
            return (double)[@"0" sizeWithAttributes:attrs].width;
        };
        core.fileDialog = [](bool save, const std::string& dir,
                             const std::string& name) -> std::string {
            NSURL* dirUrl = [NSURL fileURLWithPath:
                [NSString stringWithUTF8String:dir.c_str()]];
            if (save) {
                NSSavePanel* panel = [NSSavePanel savePanel];
                panel.title = @"Save almanac as…";
                panel.directoryURL = dirUrl;
                panel.nameFieldStringValue =
                    [NSString stringWithUTF8String:name.c_str()];
                if ([panel runModal] == NSModalResponseOK && panel.URL)
                    return [[panel.URL path] UTF8String];
                return "";
            }
            NSOpenPanel* panel = [NSOpenPanel openPanel];
            panel.title = @"Load a saved almanac…";
            panel.directoryURL = dirUrl;
            panel.canChooseFiles = YES;
            panel.canChooseDirectories = NO;
            if ([panel runModal] == NSModalResponseOK && panel.URLs.count)
                return [[panel.URLs[0] path] UTF8String];
            return "";
        };

        NSRect rect = NSMakeRect(0, 0, CLIENT_W, CLIENT_H);
        NSWindow* win = [[NSWindow alloc]
            initWithContentRect:rect
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [win setTitle:@"Sun2Set — sunrise & sunset almanac"];
        SunView* view = [[SunView alloc] initWithFrame:rect];
        view->core = &core;
        [win setContentView:view];
        [win makeFirstResponder:view];
        [win setAcceptsMouseMovedEvents:YES];

        SunAppDelegate* delegate = [[SunAppDelegate alloc] init];
        delegate->core = &core;
        delegate->window = win;
        [win setDelegate:delegate];
        [NSApp setDelegate:delegate];
        core.requestClose = [win]() { [win close]; };

        int sx, sy;
        if (core.savedWinPos(sx, sy)) {
            NSRect screen = [[NSScreen mainScreen] frame];
            [win setFrameTopLeftPoint:NSMakePoint(sx,
                                                  screen.size.height - sy)];
        } else {
            [win center];
        }

        core.onCalculate();                    // show results immediately
        delegate->timer =
            [NSTimer scheduledTimerWithTimeInterval:0.033
                                             target:delegate
                                           selector:@selector(tick:)
                                           userInfo:nil
                                            repeats:YES];
        [[NSRunLoop currentRunLoop] addTimer:delegate->timer
                                     forMode:NSRunLoopCommonModes];

        [win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--selftest") == 0) return runSelftest();
    return runMacApp();
}

#endif // __APPLE__

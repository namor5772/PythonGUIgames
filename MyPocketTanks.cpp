// MyPocketTanks.cpp — a C++ port of MyPocketTanks.py with identical
// functionality.
//
// Two tanks trade shots across destructible per-column terrain: the same
// 20 weapons, draft and one-weapon matches, wind, fuel-limited driving,
// trajectory-simulating AI, synthesized sounds, keyboard-focus model and
// %APPDATA%\MyPocketTanks\config.json persistence as the Python version
// (the .py and the native build read each other's config).
//
// No third-party libraries — same pattern as MyTetris.cpp: a platform-free
// game core (PocketTanks) that renders into a draw-command scene plus a
// terrain pixel buffer, followed by a Win32 + GDI backend and a Cocoa
// backend (this one file compiles as Objective-C++ on macOS).
//
// Build (Windows):  .\build_mytetris.ps1 -App MyPocketTanks
// Build (macOS):    ./build_mytetris.command MyPocketTanks
//   (or by hand:    clang++ -x objective-c++ -std=c++17 -O2 MyPocketTanks.cpp
//                           -framework Cocoa -o MyPocketTanks)
//
// Self-test (headless logic, same checks as MyPocketTanks.py --selftest):
//   MyPocketTanks.exe --selftest

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// Field / rendering constants (same values as MyPocketTanks.py)
// ----------------------------------------------------------------------------
static const int FIELD_W = 1000;   // battlefield width (1 terrain column each)
static const int FIELD_H = 560;    // battlefield height
static const int PANEL_H = 190;    // control panel below the field
static const int WIN_W = FIELD_W, WIN_H = FIELD_H + PANEL_H;
static const int FRAME_MS = 16;    // ~60 FPS fixed timestep
static const double DT = FRAME_MS / 1000.0;

static const double GRAVITY = 600.0;
static const double WIND_ACCEL = 14.0;
static const int WIND_MAX = 10;
static const double POWER_SPEED = 7.5;
static const int SUBSTEPS = 4;

static const int ROUNDS = 10;              // draft mode: shots per player
static const int POOL_SIZE = 2 * ROUNDS;
static const int ROUNDS_MIN = 1, ROUNDS_MAX = 20;
static const double FUEL_MAX = 100.0;
static const double FUEL_PER_PX = 0.5;
static const double MAX_CLIMB = 2.5;

static const int TANK_W = 26, TANK_H = 12;
static const double BARREL_LEN = 22;

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

static const RGB BG        = hexColor("#0b0b12");
static const RGB TEXT_C    = hexColor("#e6e6ec");
static const RGB SUBTEXT   = hexColor("#9a9ab0");
static const RGB GOLD      = hexColor("#ffd91a");
static const RGB PANEL_BG  = hexColor("#12121c");
static const RGB PANEL_EDGE= hexColor("#2a2a3a");
static const RGB BTN_BG    = hexColor("#1d1d2c");
static const RGB BTN_EDGE  = hexColor("#3a3a52");
static const RGB FOCUS_C   = hexColor("#7ec8ff");
static const RGB P1_COLOR  = hexColor("#ef4444");   // player 1 = red tank
static const RGB P2_COLOR  = hexColor("#3f7fe0");   // player 2 = blue tank
static const RGB BTN_DIS_BG= hexColor("#15151d");
static const RGB BTN_DIS_FG= hexColor("#55556a");
static const RGB HINT_C    = hexColor("#55556a");
static const RGB CARD_BG   = hexColor("#191926");
static const RGB SEL_BG    = hexColor("#26263a");
static const RGB START_BG  = hexColor("#183822");
static const RGB START_FG  = hexColor("#7ee08a");
static const RGB FIRE_BG   = hexColor("#38182a");
static const RGB FIRE_FG   = hexColor("#ff6a6a");
static const RGB WIND_C    = hexColor("#8fd0ff");
static const RGB WHITE     = hexColor("#ffffff");
static const RGB FUEL_C    = hexColor("#2ecc55");

static RGB shade(RGB c, double factor) {   // lighten (>1) / darken (<1)
    auto f = [factor](uint8_t v) {
        int x = (int)(v * factor);
        return (uint8_t)std::max(0, std::min(255, x));
    };
    return RGB{ f(c.r), f(c.g), f(c.b) };
}

// ----------------------------------------------------------------------------
// AI difficulty presets
// ----------------------------------------------------------------------------
struct AiLevel {
    const char* name;
    double aim_err;    // stddev of angle/power noise
    int sims;          // candidate shots evaluated
    double move_err;   // miss distance (px) that sends the AI driving
    const char* blurb;
};
static const AiLevel AI_LEVELS[3] = {
    { "Easy",   9.0, 25,  200, "Wobbly aim" },
    { "Normal", 4.0, 80,  110, "Decent gunner" },
    { "Hard",   1.2, 220, 50,  "Deadly accurate" },
};
static const int N_AI = 3;

// ----------------------------------------------------------------------------
// The arsenal: every weapon is data + a `kind` the projectile engine
// dispatches on (identical table to the Python WEAPONS list).
// ----------------------------------------------------------------------------
enum class Kind {
    Shell, Triple, Scatter, Dirt, Dig, Drill, Bouncer, Roller, Mirv,
    Napalm, Beam, Hopper, Sniper, Quake, DirtArc, Magnet, Cluster, Skipper,
};

struct Weapon {
    const char* key;
    const char* name;
    Kind kind;
    int r;
    int dmg;
    int weight;
    RGB color;
    const char* blurb;
};

static const Weapon WEAPONS[] = {
    { "single",  "Single Shot",  Kind::Shell,   30,  24, 10, hexColor("#d7d7e0"), "The dependable classic" },
    { "bigone",  "Big One",      Kind::Shell,   70,  50, 3,  hexColor("#ff6a3d"), "One very large boom" },
    { "triple",  "Triple Shot",  Kind::Triple,  24,  15, 6,  hexColor("#ffd91a"), "Three shells, slight spread" },
    { "bucks",   "Buckshot",     Kind::Scatter, 12,  7,  6,  hexColor("#ffb46a"), "Eight-pellet spray" },
    { "dirt",    "Dirt Ball",    Kind::Dirt,    48,  0,  5,  hexColor("#a8743d"), "Buries the target in soil" },
    { "excav",   "Excavator",    Kind::Dig,     62,  8,  5,  hexColor("#8a6a4a"), "Scoops a giant crater" },
    { "drill",   "Drill Bit",    Kind::Drill,   10,  22, 4,  hexColor("#b0b0c0"), "Bores straight down" },
    { "bounce",  "Bouncy Bomb",  Kind::Bouncer, 28,  17, 5,  hexColor("#2ecc55"), "Detonates on each of 3 bounces" },
    { "roller",  "Steamroller",  Kind::Roller,  36,  32, 5,  hexColor("#9a5ce0"), "Rolls downhill to find you" },
    { "mirv",    "Pentabomb",    Kind::Mirv,    22,  14, 4,  hexColor("#19d3da"), "Splits into 5 at the apex" },
    { "napalm",  "Firestorm",    Kind::Napalm,  26,  34, 4,  hexColor("#ff5522"), "Flames flow downhill" },
    { "laser",   "Sky Laser",    Kind::Beam,    16,  45, 3,  hexColor("#66eaff"), "Orbital beam at impact point" },
    { "hopper",  "Jack Hopper",  Kind::Hopper,  26,  16, 4,  hexColor("#7ee08a"), "Explodes, hops on, twice more" },
    { "sniper",  "Sniper Round", Kind::Sniper,  13,  55, 4,  hexColor("#e6e6ec"), "Fast, tiny, devastating" },
    { "nuke",    "Kiloton",      Kind::Shell,   115, 75, 1,  hexColor("#ffef7a"), "You will feel this one" },
    { "quake",   "Tremor",       Kind::Quake,   85,  28, 4,  hexColor("#c08a5a"), "Collapses the ground nearby" },
    { "slinger", "Dirt Slinger", Kind::DirtArc, 30,  6,  4,  hexColor("#caa05a"), "Flings a ramp of dirt onward" },
    { "magnet",  "Magno Shot",   Kind::Magnet,  28,  32, 4,  hexColor("#e05ac8"), "Steers toward the enemy" },
    { "cluster", "Cluster Pod",  Kind::Cluster, 16,  9,  5,  hexColor("#ff9f1a"), "Pops into 6 bomblets" },
    { "skimmer", "Skimmer",      Kind::Skipper, 22,  15, 5,  hexColor("#5ad0ff"), "Skips along the ground" },
};
static const int N_WEAPONS = (int)(sizeof(WEAPONS) / sizeof(WEAPONS[0]));

static int weaponByKey(const std::string& key) {
    for (int i = 0; i < N_WEAPONS; i++)
        if (key == WEAPONS[i].key) return i;
    return -1;
}

// ----------------------------------------------------------------------------
// A seedable RNG with the handful of draws the game uses (the streams don't
// match CPython's Mersenne usage, only the distributions — nothing depends
// on the exact sequence, just on determinism for a given seed).
// ----------------------------------------------------------------------------
struct Rng {
    std::mt19937 g;
    explicit Rng(uint32_t seed) : g(seed) {}
    double uniform(double a, double b) {
        return std::uniform_real_distribution<double>(a, b)(g);
    }
    int randint(int a, int b) {                       // inclusive, like Python
        return std::uniform_int_distribution<int>(a, b)(g);
    }
    double random01() { return uniform(0.0, 1.0); }
    double gauss(double mu, double sigma) {
        return std::normal_distribution<double>(mu, sigma)(g);
    }
    int choiceIndex(int n) { return randint(0, n - 1); }
    int weightedIndex(const std::vector<int>& weights) {
        int total = 0;
        for (int w : weights) total += w;
        int pick = randint(1, total);
        for (int i = 0; i < (int)weights.size(); i++) {
            pick -= weights[i];
            if (pick <= 0) return i;
        }
        return (int)weights.size() - 1;
    }
    template <typename T> void shuffle(std::vector<T>& v) {
        std::shuffle(v.begin(), v.end(), g);
    }
};

static uint32_t fnv1a(const char* s) {                // string -> stable seed
    uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
    return h;
}

// POOL_SIZE weighted-random weapon indices for the pick phase (dupes OK,
// but at most 2 of a kind so the pool stays varied).
static std::vector<int> draftPool(Rng& rng) {
    std::vector<int> weights;
    for (int i = 0; i < N_WEAPONS; i++) weights.push_back(WEAPONS[i].weight);
    std::vector<int> pool;
    std::map<int, int> counts;
    while ((int)pool.size() < POOL_SIZE) {
        int k = rng.weightedIndex(weights);
        if (counts[k] < 2) {
            counts[k]++;
            pool.push_back(k);
        }
    }
    rng.shuffle(pool);
    return pool;
}

// ----------------------------------------------------------------------------
// Terrain: a per-column heightmap. terrain[x] = y of the surface (smaller =
// higher ground). No overhangs by design — that keeps physics simple.
// ----------------------------------------------------------------------------
static const int TERRAIN_MIN = 90;
static const int TERRAIN_MAX = FIELD_H - 30;

static int clampTerrain(double y) {
    int v = (int)std::lround(y);
    return std::max(TERRAIN_MIN, std::min(TERRAIN_MAX, v));
}

static std::vector<int> generateTerrain(Rng& rng) {
    struct Wave { double amp, freq, phase; };
    std::vector<Wave> waves;
    for (int i = 0; i < 5; i++)
        waves.push_back(Wave{ rng.uniform(20, 90), rng.uniform(1.0, 4.0),
                              rng.uniform(0, 2 * 3.14159265358979323846) });
    double base = rng.uniform(FIELD_H * 0.55, FIELD_H * 0.72);
    std::vector<int> terrain(FIELD_W);
    for (int x = 0; x < FIELD_W; x++) {
        double t = (double)x / FIELD_W;
        double y = base;
        for (const Wave& w : waves)
            y += w.amp * std::sin(2 * 3.14159265358979323846 * w.freq * t
                                  + w.phase) / waves.size() * 2;
        terrain[x] = clampTerrain(y);
    }
    return terrain;
}

// ----------------------------------------------------------------------------
// Sound synthesis — identical WAV buffers to the Python _tone/_noise/_wav.
// ----------------------------------------------------------------------------
static const int SAMPLE_RATE = 22050;

static std::vector<int16_t> tone(double freq, double ms, double vol = 0.4,
                                 bool square = true) {
    int n = (int)(SAMPLE_RATE * ms / 1000);
    int attack = std::max(1, (int)(0.004 * SAMPLE_RATE));
    std::vector<int16_t> out;
    out.reserve(n);
    for (int i = 0; i < n; i++) {
        double t = (double)i / SAMPLE_RATE;
        double s = std::sin(2 * 3.14159265358979323846 * freq * t);
        double base = square ? (s >= 0 ? 1.0 : -1.0) : s;
        double env = std::min(1.0, (double)i / attack) * (1.0 - (double)i / n);
        double v = std::max(-1.0, std::min(1.0, base * env * vol));
        out.push_back((int16_t)(v * 32767));
    }
    return out;
}

// Filtered white noise — the basis of every explosion sound.
static std::vector<int16_t> noise(double ms, double vol = 0.5,
                                  double lowpass = 0.25) {
    Rng rng(1234);
    int n = (int)(SAMPLE_RATE * ms / 1000);
    std::vector<int16_t> out;
    out.reserve(n);
    double prev = 0.0;
    for (int i = 0; i < n; i++) {
        double raw = rng.uniform(-1.0, 1.0);
        prev += lowpass * (raw - prev);           // crude one-pole low-pass
        double env = (1.0 - (double)i / n);
        env *= env;
        double v = std::max(-1.0, std::min(1.0, prev * env * vol * 3));
        out.push_back((int16_t)(v * 32767));
    }
    return out;
}

static std::vector<int16_t> seq(std::initializer_list<std::vector<int16_t>> parts) {
    std::vector<int16_t> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

static std::vector<uint8_t> wavBytes(const std::vector<int16_t>& samples) {
    std::vector<uint8_t> b;
    uint32_t dataLen = (uint32_t)(samples.size() * 2);
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xff); };
    auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; i++) b.push_back((v >> (8 * i)) & 0xff); };
    auto tag = [&](const char* s) { for (int i = 0; i < 4; i++) b.push_back(s[i]); };
    tag("RIFF"); u32(36 + dataLen); tag("WAVE");
    tag("fmt "); u32(16); u16(1); u16(1); u32(SAMPLE_RATE);
    u32(SAMPLE_RATE * 2); u16(2); u16(16);
    tag("data"); u32(dataLen);
    for (int16_t s : samples) { b.push_back(s & 0xff); b.push_back((s >> 8) & 0xff); }
    return b;
}

// name -> WAV bytes for every effect (shared by both playback backends).
static std::map<std::string, std::vector<uint8_t>> soundSpecs() {
    std::map<std::string, std::vector<uint8_t>> m;
    m["blip"]    = wavBytes(tone(440, 30, 0.25));
    m["pick"]    = wavBytes(seq({ tone(523, 40, 0.3), tone(659, 60, 0.3) }));
    m["fire"]    = wavBytes(seq({ tone(160, 40, 0.5, false), noise(90, 0.35, 0.5) }));
    m["boom"]    = wavBytes(noise(300, 0.55, 0.12));
    m["bigboom"] = wavBytes(noise(650, 0.7, 0.07));
    m["dirt"]    = wavBytes(noise(220, 0.3, 0.5));
    m["bounce"]  = wavBytes(tone(300, 45, 0.35, false));
    m["laser"]   = wavBytes(seq({ tone(1600, 60, 0.35), tone(1200, 60, 0.35),
                                  tone(800, 90, 0.35) }));
    m["move"]    = wavBytes(tone(90, 35, 0.3, false));
    m["win"]     = wavBytes(seq({ tone(523, 70, 0.45), tone(659, 70, 0.45),
                                  tone(784, 70, 0.45), tone(1047, 160, 0.45) }));
    m["lose"]    = wavBytes(seq({ tone(440, 130, 0.4, false), tone(330, 130, 0.4, false),
                                  tone(220, 220, 0.4, false) }));
    return m;
}

class SoundIface {
public:
    bool muted = false;
    virtual ~SoundIface() {}
    virtual bool isEnabled() const { return false; }
    virtual void play(const std::string&) {}
    virtual void toggleMute() { muted = !muted; }
};

// ----------------------------------------------------------------------------
// Minimal JSON (parse + dump) — same defensive shape as the Python loaders.
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
            snprintf(buf, sizeof buf, "%g", v.num);
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
// Config persistence — the same %APPDATA%\MyPocketTanks\config.json as the
// Python version (macOS: ~/MyPocketTanks/), read/written interchangeably.
// ----------------------------------------------------------------------------
static std::string appDataDir() {
    const char* base = getenv("APPDATA");
    if (!base || !*base) {
#ifdef _WIN32
        base = getenv("USERPROFILE");
#else
        base = getenv("HOME");
#endif
    }
    std::string dir = base ? base : ".";
#ifdef _WIN32
    return dir + "\\MyPocketTanks";
#else
    return dir + "/MyPocketTanks";
#endif
}

static std::string configPath() {
#ifdef _WIN32
    return appDataDir() + "\\config.json";
#else
    return appDataDir() + "/config.json";
#endif
}

static void makeAppDir();                     // per-platform (defined below)

static JVal loadConfig() {
    std::ifstream f(configPath(), std::ios::binary);
    if (!f) return JVal::object();
    std::ostringstream ss;
    ss << f.rdbuf();
    // The text must outlive the parser — JParser keeps pointers into it
    // (passing ss.str() directly would parse a destroyed temporary).
    std::string text = ss.str();
    JParser parser(text);
    JVal data = parser.parse();
    if (!parser.ok || data.type != JVal::Obj) return JVal::object();
    return data;
}

static void saveConfig(const JVal& config) {
    makeAppDir();
    std::ofstream f(configPath(), std::ios::binary | std::ios::trunc);
    if (!f) return;
    std::string out;
    jsonDump(config, out, 0);
    f << out;
}

// ----------------------------------------------------------------------------
// The scene: draw() emits draw commands (the Tk canvas calls, reified) plus
// button hit boxes; the platform backends rasterize them. The terrain itself
// is a pixel buffer the core repaints per touched column range (the C++ twin
// of the Tk PhotoImage) — the `Terrain` command tells the backend to blit it.
// ----------------------------------------------------------------------------
struct Cmd {
    enum Type { Line, Rect, RoundRect, Text, Oval, Poly, Polyline, Terrain } type;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    RGB fill{}; bool hasFill = false;
    RGB outline{}; bool hasOutline = false;
    double width = 1;
    double radius = 0;                        // RoundRect
    bool roundCap = false;                    // Line
    std::vector<std::pair<double, double>> pts;   // Poly / Polyline
    std::string text;                         // Text (UTF-8)
    int size = 11; bool bold = false, italic = false;
    enum Anchor { Center, W, E } anchor = Center;
};

struct Button { double x0, y0, x1, y1; std::string action; };

enum class Key {
    None, Left, Right, Up, Down, Home, End, PgUp, PgDn, Tab, Enter, Space,
    Escape, A, D, Y, N, One, Two, R, M, BracketLeft, BracketRight,
};

// ----------------------------------------------------------------------------
// Tanks
// ----------------------------------------------------------------------------
struct Tank {
    int pid;                        // 0 or 1
    double x;                       // center of the hull
    double y = 0;                   // hull top y; settled onto the terrain
    RGB color;
    std::string name;
    bool is_ai;
    double angle;                   // 0=right, 90=up, 180=left
    double power = 60.0;
    int score = 0;
    double fuel = FUEL_MAX;
    std::vector<int> arsenal;       // weapon indices, consumed on fire
    int weapon_i = 0;
    double burn_acc = 0;            // fractional napalm damage accumulator

    Tank(int pid_, double x_, RGB color_, std::string name_, bool ai_ = false)
        : pid(pid_), x(x_), color(color_), name(std::move(name_)), is_ai(ai_),
          angle(pid_ == 0 ? 60.0 : 120.0) {}

    void settle(const std::vector<int>& terrain) {
        int x0 = std::max(0, (int)x - TANK_W / 2);
        int x1 = std::min(FIELD_W - 1, (int)x + TANK_W / 2);
        int top = TERRAIN_MAX;
        for (int xx = x0; xx <= x1; xx++) top = std::min(top, terrain[xx]);
        y = top - TANK_H;
    }
    void center(double& cx, double& cy) const { cx = x; cy = y + TANK_H * 0.5; }
    void muzzle(double& mx, double& my) const {
        double a = angle * 3.14159265358979323846 / 180.0;
        mx = x + std::cos(a) * BARREL_LEN;
        my = y - 2 - std::sin(a) * BARREL_LEN;
    }
    int currentWeapon() const {
        if (arsenal.empty()) return -1;
        return arsenal[weapon_i];
    }
    void cycleWeapon(int step) {
        if (!arsenal.empty()) {
            int n = (int)arsenal.size();
            weapon_i = ((weapon_i + step) % n + n) % n;
        }
    }
};

struct Projectile {
    double x, y, vx, vy;
    int weapon;                     // index into WEAPONS (r/dmg/color)
    Kind kind;                      // usually WEAPONS[weapon].kind; cluster
                                    // bomblets carry Kind::Shell instead
    int owner;
    bool rolling = false;
    std::vector<std::pair<float, float>> trail;
    int bounces = 0, hops = 0, skips = 0;
    bool split = false;
    double roll_time = 0;
};
using PProj = std::shared_ptr<Projectile>;

struct Effect {
    enum Type { Boom, BeamFx, QuakeFx, TextFx } type;
    double x, y, r = 0;
    int frames, age = 0;
    std::string text;
    RGB color{};
};

struct Flame {
    double x;
    int owner;
    std::shared_ptr<double> budget;   // shared by one volley's 14 flames
    int life;
    double dmg;
};

struct AiPlan {
    double angle, power;
    int weapon;
    bool hasMove = false;
    int move_to = 0;
};

enum class GState { Menu, Pick, Playing, GameOver };

// ----------------------------------------------------------------------------
// The game core — a line-for-line port of the Python PocketTanks class minus
// Tk. `gui=false` (the selftest) skips the terrain pixel work like root=None.
// ----------------------------------------------------------------------------
class PocketTanks {
public:
    bool persist;
    Rng rng;
    SoundIface* sound;
    JVal config;

    GState state = GState::Menu;
    bool confirm_menu = false;
    std::string mode;               // "1P" | "2P"
    std::string ai_level;           // "Easy" | "Normal" | "Hard"
    std::string match_type;         // "draft" | "single"
    int single_rounds = ROUNDS;
    int single_weapon;              // weapon index (persisted by key string)
    bool move_enabled = true;
    int rounds = ROUNDS;

    std::vector<int> terrain;
    std::vector<Tank> tanks;
    int turn = 0;
    int wind = 0;
    bool aimPhase = true;           // phase: aim | flight
    std::vector<PProj> projectiles;
    std::vector<Effect> effects;
    std::vector<Flame> flames;
    int shots_fired[2] = { 0, 0 };
    std::vector<int> pool;
    int picker = 0;
    int ai_wait = 0;
    std::unique_ptr<AiPlan> ai_plan;
    Tank* winner = nullptr;         // points into tanks
    bool has_toast = false;
    std::string toast_text;
    int toast_frames = 0;
    long frame = 0;

    // UI / scene
    bool gui;
    std::vector<Cmd> scene;
    std::vector<Button> buttons;
    std::string focus_action;       // "" = nothing focused
    std::function<void()> requestClose;

    // Terrain pixel layer (only when gui): 0x00RRGGBB rows, top-down.
    std::vector<uint32_t> terrainPix;
    std::vector<uint32_t> skyRow, dirtRow;
    uint32_t grassRow[3] = { 0, 0, 0 };
    std::map<long, uint32_t> stars;           // (y*FIELD_W + x) -> color

    PocketTanks(bool gui_, SoundIface* snd, bool persistFlag, long seed = -1)
        : persist(persistFlag),
          rng(seed >= 0 ? (uint32_t)seed : std::random_device{}()),
          sound(snd), gui(gui_) {
        config = persist ? loadConfig() : JVal::object();
        mode = strConfig("mode", "1P");
        if (mode != "1P" && mode != "2P") mode = "1P";
        ai_level = strConfig("ai_level", "Normal");
        bool knownAi = false;
        for (auto& l : AI_LEVELS) if (ai_level == l.name) knownAi = true;
        if (!knownAi) ai_level = "Normal";
        match_type = strConfig("match_type", "draft");
        if (match_type != "draft" && match_type != "single") match_type = "draft";
        const JVal* sr = config.get("single_rounds");
        single_rounds = (sr && sr->type == JVal::Num
                         && sr->num == (int)sr->num
                         && ROUNDS_MIN <= (int)sr->num
                         && (int)sr->num <= ROUNDS_MAX) ? (int)sr->num : ROUNDS;
        single_weapon = weaponByKey(strConfig("single_weapon", "single"));
        if (single_weapon < 0) single_weapon = weaponByKey("single");
        const JVal* me = config.get("move_enabled");
        move_enabled = (me && me->type == JVal::Bool) ? me->b : true;

        terrain = generateTerrain(rng);
        if (gui) {
            buildTerrainPalette();
            terrainPix.assign((size_t)FIELD_W * FIELD_H, 0);
            repaintTerrain();
        }
    }

    std::string strConfig(const char* key, const char* fallback) const {
        const JVal* v = config.get(key);
        return (v && v->type == JVal::Str) ? v->str : fallback;
    }

    const AiLevel& aiLevel() const {
        for (auto& l : AI_LEVELS) if (ai_level == l.name) return l;
        return AI_LEVELS[1];
    }

    // ------------------------------------------------------------------ match
    void startMatch() {
        terrain = generateTerrain(rng);
        int margin = 120;
        int x1 = rng.randint(margin, margin + 120);
        int x2 = rng.randint(FIELD_W - margin - 120, FIELD_W - margin);
        tanks.clear();
        tanks.emplace_back(0, x1, P1_COLOR, "PLAYER 1");
        tanks.emplace_back(1, x2, P2_COLOR,
                           mode == "1P" ? "COMPUTER" : "PLAYER 2",
                           mode == "1P");
        for (Tank& t : tanks) t.settle(terrain);
        turn = rng.randint(0, 1);
        rounds = (match_type == "draft") ? ROUNDS : single_rounds;
        if (match_type == "single") {
            pool.clear();
            for (int i = 0; i < N_WEAPONS; i++) pool.push_back(i);
            picker = 0;
        } else {
            pool = draftPool(rng);
            picker = turn;
        }
        shots_fired[0] = shots_fired[1] = 0;
        projectiles.clear();
        effects.clear();
        flames.clear();
        winner = nullptr;
        aimPhase = true;
        confirm_menu = false;
        focus_action.clear();
        if (match_type == "single") {
            // Enter the one-weapon pick screen focused on the last-picked
            // (gold-outlined) card: Enter alone replays the previous choice.
            for (int i = 0; i < (int)pool.size(); i++)
                if (pool[i] == single_weapon) {
                    focus_action = "pick:" + std::to_string(i);
                    break;
                }
        }
        ai_plan.reset();
        ai_wait = 30;
        state = GState::Pick;
        newWind();
        repaintTerrain();
    }

    void newWind() { wind = rng.randint(-WIND_MAX, WIND_MAX); }
    Tank& currentTank() { return tanks[turn]; }
    Tank& enemyTank() { return tanks[1 - turn]; }

    // ------------------------------------------------------------------ pick
    void pickWeapon(int poolIndex) {
        if (state != GState::Pick || poolIndex < 0
            || poolIndex >= (int)pool.size())
            return;
        if (match_type == "single") {
            int key = pool[poolIndex];
            single_weapon = key;               // remembered (and persisted)
            for (Tank& t : tanks) {
                t.arsenal.assign(rounds, key);
                t.weapon_i = 0;
            }
            sound->play("pick");
            beginCombat();
            return;
        }
        int key = pool[poolIndex];
        pool.erase(pool.begin() + poolIndex);
        tanks[picker].arsenal.push_back(key);
        sound->play("pick");
        bool allFull = true;
        for (Tank& t : tanks)
            if ((int)t.arsenal.size() < rounds) allFull = false;
        if (allFull) {
            beginCombat();
            return;
        }
        picker = 1 - picker;
        if ((int)tanks[picker].arsenal.size() >= rounds)
            picker = 1 - picker;               // other side still drafting
        ai_wait = 20;
    }

    void beginCombat() {
        state = GState::Playing;
        aimPhase = true;
        focus_action.clear();
        setToast(currentTank().name + " SHOOTS FIRST");
        ai_wait = 45;
    }

    void aiPick() {
        // AI drafts greedily by damage potential, with some randomness.
        auto value = [&](int key) {
            const Weapon& w = WEAPONS[key];
            double v = w.dmg * (1 + w.r / 60.0);
            switch (w.kind) {
            case Kind::Mirv: case Kind::Cluster: case Kind::Scatter:
            case Kind::Triple: case Kind::Bouncer: case Kind::Hopper:
            case Kind::Skipper:
                v *= 2.2;                       // multi-hit weapons
                break;
            default: break;
            }
            return v * rng.uniform(0.7, 1.3);
        };
        int best = 0;
        double bestV = -1;
        for (int i = 0; i < (int)pool.size(); i++) {
            double v = value(pool[i]);
            if (v > bestV) { bestV = v; best = i; }
        }
        pickWeapon(best);
    }

    // ------------------------------------------------------------------ aim
    void adjustAngle(double d) {
        Tank& t = currentTank();
        t.angle = std::max(0.0, std::min(180.0, t.angle + d));
    }

    void adjustPower(double d) {
        Tank& t = currentTank();
        t.power = std::max(5.0, std::min(100.0, t.power + d));
    }

    bool moveTank(int direction) {
        // Drive 1px left/right if fuel remains and the slope is climbable.
        Tank& t = currentTank();
        if (!move_enabled || !aimPhase || t.fuel < FUEL_PER_PX) return false;
        double nx = t.x + direction;
        int half = TANK_W / 2;
        if (!(half <= nx && nx <= FIELD_W - 1 - half)) return false;
        int here = terrain[(int)t.x];
        int there = terrain[(int)nx];
        if (here - there > MAX_CLIMB) return false;   // too steep uphill
        Tank& other = enemyTank();
        if (std::fabs(nx - other.x) < TANK_W) return false;  // enemy block
        t.x = nx;
        t.fuel -= FUEL_PER_PX;
        t.settle(terrain);
        if (frame % 6 == 0) sound->play("move");
        return true;
    }

    void driveRange(const Tank& t, int& outLo, int& outHi) {
        // The x-interval tank t could reach right now, under moveTank's rules.
        int half = TANK_W / 2;
        const Tank& other = tanks[1 - t.pid];
        int budget = (int)(t.fuel / FUEL_PER_PX);
        int span[2] = { (int)t.x, (int)t.x };
        const int dirs[2] = { -1, 1 };
        for (int i = 0; i < 2; i++) {
            int x = (int)t.x, steps = 0;
            while (steps < budget) {
                int nx = x + dirs[i];
                if (!(half <= nx && nx <= FIELD_W - 1 - half)) break;
                if (terrain[x] - terrain[nx] > MAX_CLIMB) break;
                if (std::fabs((double)nx - other.x) < TANK_W) break;
                x = nx;
                steps++;
            }
            span[i] = x;
        }
        outLo = span[0];
        outHi = span[1];
    }

    // ------------------------------------------------------------------ fire
    void fire() {
        Tank& t = currentTank();
        int wi = t.currentWeapon();
        if (state != GState::Playing || !aimPhase || wi < 0) return;
        const Weapon& w = WEAPONS[wi];
        double mx, my;
        t.muzzle(mx, my);
        double a = t.angle * 3.14159265358979323846 / 180.0;
        double speed = t.power * POWER_SPEED;
        double vx = std::cos(a) * speed, vy = -std::sin(a) * speed;
        if (w.kind == Kind::Triple) {
            for (int da = -4; da <= 4; da += 4) {
                double aa = (t.angle + da) * 3.14159265358979323846 / 180.0;
                spawn(mx, my, std::cos(aa) * speed, -std::sin(aa) * speed,
                      wi, w.kind, t.pid);
            }
        } else if (w.kind == Kind::Scatter) {
            for (int i = 0; i < 8; i++) {
                double aa = (t.angle + rng.uniform(-7, 7))
                            * 3.14159265358979323846 / 180.0;
                double sp = speed * rng.uniform(0.85, 1.05);
                spawn(mx, my, std::cos(aa) * sp, -std::sin(aa) * sp,
                      wi, w.kind, t.pid);
            }
        } else if (w.kind == Kind::Sniper) {
            spawn(mx, my, vx * 1.6, vy * 1.6, wi, w.kind, t.pid);
        } else {
            PProj p = spawn(mx, my, vx, vy, wi, w.kind, t.pid);
            if (w.kind == Kind::Bouncer) p->bounces = 3;
            else if (w.kind == Kind::Hopper) p->hops = 3;
            else if (w.kind == Kind::Skipper) p->skips = 4;
        }
        // The shot is spent whether it lands well or not.
        t.arsenal.erase(t.arsenal.begin() + t.weapon_i);
        if (!t.arsenal.empty()) t.weapon_i %= (int)t.arsenal.size();
        shots_fired[t.pid]++;
        aimPhase = false;
        sound->play("fire");
    }

    PProj spawn(double x, double y, double vx, double vy, int weapon,
                Kind kind, int owner) {
        PProj p = std::make_shared<Projectile>();
        p->x = x; p->y = y; p->vx = vx; p->vy = vy;
        p->weapon = weapon;
        p->kind = kind;
        p->owner = owner;
        projectiles.push_back(p);
        return p;
    }

    // ------------------------------------------------------------- projectile
    bool projLive(const PProj& p) const {
        return std::find(projectiles.begin(), projectiles.end(), p)
               != projectiles.end();
    }
    void projRemove(const PProj& p) {
        auto it = std::find(projectiles.begin(), projectiles.end(), p);
        if (it != projectiles.end()) projectiles.erase(it);
    }

    void updateProjectiles() {
        double dt = DT / SUBSTEPS;
        for (int sub = 0; sub < SUBSTEPS; sub++) {
            std::vector<PProj> snapshot = projectiles;
            for (const PProj& p : snapshot) {
                if (!projLive(p)) continue;
                if (p->rolling) {
                    rollStep(p, dt);
                    continue;
                }
                p->vy += GRAVITY * dt;
                p->vx += wind * WIND_ACCEL * dt;
                if (p->kind == Kind::Magnet) {
                    double ex, ey;
                    tanks[1 - p->owner].center(ex, ey);
                    double d = std::hypot(ex - p->x, ey - p->y);
                    if (d == 0) d = 1.0;
                    const double pull = 260.0;
                    p->vx += (ex - p->x) / d * pull * dt;
                    p->vy += (ey - p->y) / d * pull * dt;
                }
                // MIRV splits when it tips over the top of its arc.
                if (p->kind == Kind::Mirv && !p->split && p->vy >= 0) {
                    p->split = true;
                    projRemove(p);
                    for (int i = 0; i < 5; i++) {
                        PProj q = std::make_shared<Projectile>(*p);
                        q->vx = p->vx + (i - 2) * 42.0;
                        q->split = true;
                        projectiles.push_back(q);
                    }
                    continue;
                }
                p->x += p->vx * dt;
                p->y += p->vy * dt;
                if (p->x < -60 || p->x > FIELD_W + 60 || p->y > FIELD_H + 60) {
                    projRemove(p);                     // flew off the world
                    continue;
                }
                if (frame % 2 == 0) {
                    p->trail.emplace_back((float)p->x, (float)p->y);
                    if (p->trail.size() > 40) p->trail.erase(p->trail.begin());
                }
                Tank* hit = tankAt(p->x, p->y);
                if (hit != nullptr || inGround(p->x, p->y))
                    impact(p, hit);
            }
        }
    }

    Tank* tankAt(double x, double y) {
        for (Tank& t : tanks)
            if (std::fabs(x - t.x) <= TANK_W / 2.0 + 2
                && t.y - 8 <= y && y <= t.y + TANK_H + 2)
                return &t;
        return nullptr;
    }

    bool inGround(double x, double y) const {
        int xi = (int)x;
        return 0 <= xi && xi < FIELD_W && y >= terrain[xi];
    }

    void rollStep(const PProj& p, double dt) {
        // Steamroller ground travel: follow the surface, speed up downhill,
        // explode on the enemy, on stalling, or at the field edge.
        int xi = (int)p->x;
        if (!(2 <= xi && xi <= FIELD_W - 3)) {
            detonate(p, p->x, p->y);
            return;
        }
        double slope = (terrain[xi + 2] - terrain[xi - 2]) / 4.0;
        p->vx += slope * 900.0 * dt;               // gravity along the slope
        p->vx *= (1 - 0.4 * dt);                   // rolling friction
        p->x += p->vx * dt;
        xi = (int)std::max(0.0, std::min((double)FIELD_W - 1, p->x));
        p->y = terrain[xi] - 3;
        p->roll_time += dt;
        if (frame % 2 == 0) {
            p->trail.emplace_back((float)p->x, (float)p->y);
            if (p->trail.size() > 40) p->trail.erase(p->trail.begin());
        }
        Tank* hit = tankAt(p->x, p->y + 2);
        bool stalled = std::fabs(p->vx) < 12 && p->roll_time > 0.4;
        if (hit != nullptr || stalled || p->roll_time > 6.0)
            detonate(p, p->x, p->y, hit);
    }

    // ---------------------------------------------------------------- impact
    void impact(const PProj& p, Tank* direct = nullptr) {
        const Weapon& w = WEAPONS[p->weapon];
        double x = p->x, y = p->y;
        if (p->kind == Kind::Bouncer && p->bounces > 1 && direct == nullptr) {
            p->bounces--;
            explode(p->owner, x, y, w.r, w.dmg);
            bounce(p, 0.65);
            sound->play("bounce");
            return;
        }
        if (p->kind == Kind::Skipper && p->skips > 1 && direct == nullptr
            && std::fabs(p->vy) < std::fabs(p->vx) * 0.9) {
            p->skips--;
            explode(p->owner, x, y, w.r * 0.7, w.dmg * 0.6);
            bounce(p, 0.75);
            sound->play("bounce");
            return;
        }
        detonate(p, x, y, direct);
    }

    void bounce(const PProj& p, double damping) {
        // Reflect off the local terrain surface and lift out of the ground.
        int xi = (int)std::max(2.0, std::min((double)FIELD_W - 3, p->x));
        double slope = (terrain[xi + 2] - terrain[xi - 2]) / 4.0;
        double nx = -slope, ny = -1.0;
        double nl = std::hypot(nx, ny);
        nx /= nl; ny /= nl;
        double dot = p->vx * nx + p->vy * ny;
        p->vx = (p->vx - 2 * dot * nx) * damping;
        p->vy = (p->vy - 2 * dot * ny) * damping;
        p->y = terrain[xi] - 4;
    }

    void detonate(const PProj& p, double x, double y, Tank* direct = nullptr) {
        // The projectile is done; apply its weapon's terminal effect.
        projRemove(p);
        const Weapon& w = WEAPONS[p->weapon];
        int owner = p->owner;
        Kind kind = p->kind;
        if (kind == Kind::Roller && !p->rolling && direct == nullptr
            && 0 <= (int)x && (int)x < FIELD_W) {
            // First ground contact: start rolling instead of exploding.
            p->rolling = true;
            p->x = std::max(2.0, std::min((double)FIELD_W - 3, x));
            p->y = terrain[(int)p->x] - 3;
            if (std::fabs(p->vx) < 30)
                p->vx = p->vx >= 0 ? 30.0 : -30.0;
            projectiles.push_back(p);
            return;
        }
        if (kind == Kind::Dirt) {
            addDirt(x, w.r);
            sound->play("dirt");
        } else if (kind == Kind::DirtArc) {
            int direction = p->vx >= 0 ? 1 : -1;
            for (int i = 0; i < 5; i++)
                addDirt(x + direction * i * 22, 18 + i * 3);
            explode(owner, x, y, w.r, w.dmg, true, direct);
        } else if (kind == Kind::Dig) {
            carve(x, y, w.r);
            explode(owner, x, y, w.r, w.dmg, false, direct);
            sound->play("dirt");
        } else if (kind == Kind::Drill) {
            int xi = (int)std::max(0.0, std::min((double)FIELD_W - 1, x));
            for (int step = 0; step < 6; step++) {  // staged charges downward
                int yy = terrain[xi] + 1;
                carve(xi, yy + step * 12, 14);
                explode(owner, xi, yy + step * 12, w.r + 6, w.dmg / 3.0,
                        false, step == 0 ? direct : nullptr);
            }
        } else if (kind == Kind::Beam) {
            skyLaser(owner, x, w);
        } else if (kind == Kind::Napalm) {
            explode(owner, x, y, w.r, w.dmg * 0.4, true, direct);
            spawnFlames(owner, x, w);
        } else if (kind == Kind::Quake) {
            tremor(owner, x, w);
        } else if (kind == Kind::Cluster) {
            explode(owner, x, y, w.r, w.dmg, true, direct);
            for (int i = 0; i < 6; i++)
                spawn(x, y - 4, rng.uniform(-140, 140),
                      rng.uniform(-320, -160), p->weapon, Kind::Shell, owner);
        } else if (kind == Kind::Hopper) {
            explode(owner, x, y, w.r, w.dmg, true, direct);
            if (p->hops > 1) {
                double direction = p->vx >= 0 ? 1 : -1;
                PProj q = spawn(x, y - 6, direction * 190.0, -330.0,
                                p->weapon, p->kind, owner);
                q->hops = p->hops - 1;
            }
        } else {                                    // shell / triple / scatter /
            bool big = w.r >= 60;                   // sniper / magnet / roller...
            explode(owner, x, y, w.r, w.dmg, true, direct);
            if (big) sound->play("bigboom");
        }
    }

    // ------------------------------------------------------ terrain & damage
    void carve(double cx, double cy, double r) {
        // Remove a circle of ground centered at (cx, cy) from the heightmap.
        int x0 = std::max(0, (int)(cx - r));
        int x1 = std::min(FIELD_W - 1, (int)(cx + r));
        for (int x = x0; x <= x1; x++) {
            double dx = x - cx;
            double half = std::sqrt(std::max(0.0, r * r - dx * dx));
            double top = cy - half, bottom = cy + half;
            int h = terrain[x];
            if (bottom <= h) continue;              // circle entirely in the air
            terrain[x] = clampTerrain(h + (bottom - std::max((double)h, top)));
        }
        repaintTerrain(x0, x1);
        settleTanks();
    }

    void addDirt(double cx, double r) {
        // Pile a dome of dirt onto the surface centered at column cx.
        int x0 = std::max(0, (int)(cx - r));
        int x1 = std::min(FIELD_W - 1, (int)(cx + r));
        for (int x = x0; x <= x1; x++) {
            double dx = x - cx;
            double lift = std::sqrt(std::max(0.0, r * r - dx * dx));
            terrain[x] = clampTerrain(terrain[x] - lift);
        }
        repaintTerrain(x0, x1);
        settleTanks();
    }

    void settleTanks() {
        for (Tank& t : tanks) t.settle(terrain);
    }

    void explode(int owner, double x, double y, double r, double dmg,
                 bool carveGround = true, Tank* direct = nullptr) {
        // Crater + blast damage + score. Direct hits earn a 25% bonus.
        if (carveGround) carve(x, y, r);
        for (Tank& t : tanks) {
            double cx, cy;
            t.center(cx, cy);
            double d = std::hypot(cx - x, cy - y);
            double reach = r + TANK_W / 2.0;
            if (d >= reach || dmg <= 0) continue;
            double amountF = dmg * (1.0 - d / reach);
            if (direct == &t) amountF = dmg * 1.25;
            int amount = (int)std::lround(amountF);
            if (amount <= 0) continue;
            // Points go to the shooter for enemy damage; gifting yourself
            // damage scores for your opponent instead.
            int scorer = (t.pid != owner) ? owner : 1 - owner;
            tanks[scorer].score += amount;
            addTextEffect(cx, t.y - 16, "+" + std::to_string(amount),
                          tanks[scorer].color);
        }
        Effect e;
        e.type = Effect::Boom;
        e.x = x; e.y = y; e.r = r; e.frames = 18;
        effects.push_back(e);
        if (dmg > 0 || r >= 40) sound->play("boom");
    }

    void skyLaser(int owner, double x, const Weapon& w) {
        // Vertical orbital beam: vaporize a trench at column x, hurt below it.
        int xi = (int)std::max(0.0, std::min((double)FIELD_W - 1, x));
        int ground = terrain[xi];
        for (Tank& t : tanks) {
            if (std::fabs(t.x - xi) <= w.r + TANK_W / 2.0) {
                int amount = (int)std::lround(
                    w.dmg * (1 - std::fabs(t.x - xi) / (w.r + TANK_W)));
                if (amount > 0) {
                    int scorer = (t.pid != owner) ? owner : 1 - owner;
                    tanks[scorer].score += amount;
                    addTextEffect(t.x, t.y - 16, "+" + std::to_string(amount),
                                  tanks[scorer].color);
                }
            }
        }
        carve(xi, ground + 18, w.r + 8);
        Effect e;
        e.type = Effect::BeamFx;
        e.x = xi; e.y = ground; e.frames = 16;
        effects.push_back(e);
        sound->play("laser");
    }

    void tremor(int owner, double x, const Weapon& w) {
        // Slump the terrain toward its local average around the epicenter.
        int x0 = std::max(0, (int)(x - w.r));
        int x1 = std::min(FIELD_W - 1, (int)(x + w.r));
        double avg = 0;
        for (int xx = x0; xx <= x1; xx++) avg += terrain[xx];
        avg /= (x1 - x0 + 1);
        for (int xx = x0; xx <= x1; xx++) {
            double f = 1 - std::fabs(xx - x) / w.r;  // strongest at the center
            double jitter = rng.uniform(-4, 4);
            terrain[xx] = clampTerrain(
                terrain[xx] + (avg - terrain[xx]) * 0.6 * f + 10 * f + jitter);
        }
        repaintTerrain(x0, x1);
        settleTanks();
        for (Tank& t : tanks) {
            double d = std::fabs(t.x - x);
            if (d < w.r) {
                int amount = (int)std::lround(w.dmg * (1 - d / w.r));
                if (amount > 0) {
                    int scorer = (t.pid != owner) ? owner : 1 - owner;
                    tanks[scorer].score += amount;
                    addTextEffect(t.x, t.y - 16, "+" + std::to_string(amount),
                                  tanks[scorer].color);
                }
            }
        }
        Effect e;
        e.type = Effect::QuakeFx;
        int xi = (int)std::max(0.0, std::min((double)FIELD_W - 1, x));
        e.x = x; e.y = terrain[xi]; e.r = w.r; e.frames = 24;
        effects.push_back(e);
        sound->play("bigboom");
    }

    void spawnFlames(int owner, double x, const Weapon& w) {
        // Napalm: flames slide downhill and burn where they sit, sharing one
        // damage budget so a bathing tank takes at most ~1.5x rated damage.
        auto budget = std::make_shared<double>(w.dmg * 1.5);
        for (int i = 0; i < 14; i++) {
            Flame f;
            f.x = x + rng.uniform(-6, 6);
            f.owner = owner;
            f.budget = budget;
            f.life = rng.randint(50, 110);
            f.dmg = w.dmg / 40.0;
            flames.push_back(f);
        }
        sound->play("boom");
    }

    void updateFlames() {
        for (size_t i = 0; i < flames.size();) {
            Flame& f = flames[i];
            int xi = (int)std::max(1.0, std::min((double)FIELD_W - 2, f.x));
            // Slide toward the lower neighboring column.
            if (terrain[xi - 1] > terrain[xi + 1]) f.x -= 25 * DT;
            else if (terrain[xi + 1] > terrain[xi - 1]) f.x += 25 * DT;
            f.life--;
            if (f.life <= 0) {
                flames.erase(flames.begin() + i);
                continue;
            }
            for (Tank& t : tanks) {
                if (std::fabs(t.x - f.x) < TANK_W * 0.8
                    && std::fabs(t.y + TANK_H - terrain[xi]) < 30) {
                    double amount = std::min(f.dmg, *f.budget);
                    if (amount <= 0) continue;
                    *f.budget -= amount;
                    // Accumulate fractional burn into whole scoreboard points.
                    t.burn_acc += amount;
                    int whole = (int)t.burn_acc;
                    t.burn_acc -= whole;
                    if (whole > 0) {
                        int scorer = (t.pid != f.owner) ? f.owner : 1 - f.owner;
                        tanks[scorer].score += whole;
                    }
                }
            }
            i++;
        }
    }

    void addTextEffect(double x, double y, const std::string& text, RGB color) {
        Effect e;
        e.type = Effect::TextFx;
        e.x = x; e.y = y; e.frames = 45;
        e.text = text;
        e.color = color;
        effects.push_back(e);
    }

    void setToast(const std::string& text) {
        has_toast = true;
        toast_text = text;
        toast_frames = 100;
    }

    // ------------------------------------------------------------- turn flow
    bool flightDone() const {
        if (!projectiles.empty() || !flames.empty()) return false;
        for (const Effect& e : effects)
            if (e.age < e.frames) return false;
        return true;
    }

    void endTurn() {
        // Shot fully resolved: next player, or game over after all volleys.
        if (shots_fired[0] >= rounds && shots_fired[1] >= rounds) {
            finishGame();
            return;
        }
        turn = 1 - turn;
        if (shots_fired[turn] >= rounds)      // ran dry (odd start)
            turn = 1 - turn;
        newWind();
        aimPhase = true;
        ai_plan.reset();
        ai_wait = 40;
        int shot = shots_fired[turn] + 1;
        setToast(currentTank().name + " \xE2\x80\x94 SHOT "
                 + std::to_string(shot) + "/" + std::to_string(rounds));
    }

    void finishGame() {
        state = GState::GameOver;
        Tank& t0 = tanks[0];
        Tank& t1 = tanks[1];
        if (t0.score == t1.score) {
            winner = nullptr;
            setToast("DEAD HEAT!");
        } else {
            winner = t0.score > t1.score ? &t0 : &t1;
            bool humanWon = !winner->is_ai;
            sound->play(humanWon ? "win" : "lose");
        }
    }

    // ---------------------------------------------------------------- AI turn
    bool simulateShot(double angle, double power, double& hx, double& hy) {
        // Cheap ballistic preview (wind + gravity, no weapon behavior).
        Tank& t = currentTank();
        Tank& e = enemyTank();
        double a = angle * 3.14159265358979323846 / 180.0;
        double x = t.x + std::cos(a) * BARREL_LEN;
        double y = t.y - 2 - std::sin(a) * BARREL_LEN;
        double speed = power * POWER_SPEED;
        double vx = std::cos(a) * speed, vy = -std::sin(a) * speed;
        double dt = 0.02;
        for (int i = 0; i < 700; i++) {
            vy += GRAVITY * dt;
            vx += wind * WIND_ACCEL * dt;
            x += vx * dt;
            y += vy * dt;
            if (x < -40 || x > FIELD_W + 40 || y > FIELD_H + 40) return false;
            if (std::fabs(x - e.x) <= TANK_W / 2.0 + 2
                && e.y - 8 <= y && y <= e.y + TANK_H + 2) {
                hx = x; hy = y;
                return true;
            }
            int xi = (int)x;
            if (0 <= xi && xi < FIELD_W && y >= terrain[xi]) {
                hx = x; hy = y;
                return true;
            }
        }
        return false;
    }

    AiPlan aiPlanShot() {
        // Pick a weapon and search angle/power for the closest impact to the
        // enemy, then blur the answer by the difficulty's aim error (see the
        // .py for the move-planning commentary).
        const AiLevel& lvl = aiLevel();
        Tank& t = currentTank();
        double ex, ey;
        enemyTank().center(ex, ey);

        auto wvalue = [](int key) {
            const Weapon& w = WEAPONS[key];
            double v = w.dmg * (1 + w.r / 80.0);
            if (w.kind == Kind::Dirt || w.kind == Kind::DirtArc)
                v = 12;                              // burying is situational
            return v;
        };
        int weapon;
        if (ai_level == "Hard") {
            weapon = t.arsenal[0];
            for (int k : t.arsenal)
                if (wvalue(k) > wvalue(weapon)) weapon = k;
        } else {
            weapon = t.arsenal[rng.choiceIndex((int)t.arsenal.size())];
        }

        auto error = [&](double angle, double power) {
            double hx, hy;
            if (!simulateShot(angle, power, hx, hy)) return 1e9;
            return std::hypot(hx - ex, hy - ey);
        };

        auto search = [&](int n, double& outAngle, double& outPower) {
            bool towardRight = ex > t.x;
            double bestA = towardRight ? 60.0 : 120.0, bestP = 60.0;
            double bestErr = 1e9;
            for (int i = 0; i < n; i++) {
                double angle = towardRight ? rng.uniform(15, 88)
                                           : rng.uniform(92, 165);
                double power = rng.uniform(25, 100);
                double err = error(angle, power);
                if (err < bestErr) { bestA = angle; bestP = power; bestErr = err; }
            }
            for (int i = 0; i < n / 2; i++) {
                double angle = bestA + rng.uniform(-4, 4);
                double power = bestP + rng.uniform(-6, 6);
                angle = std::max(1.0, std::min(179.0, angle));
                power = std::max(5.0, std::min(100.0, power));
                double err = error(angle, power);
                if (err < bestErr) { bestA = angle; bestP = power; bestErr = err; }
            }
            outAngle = bestA;
            outPower = bestP;
            return bestErr;
        };

        double bestA, bestP;
        double bestErr = search(lvl.sims, bestA, bestP);

        bool hasMove = false;
        int moveTo = 0;
        if (move_enabled && t.fuel >= 8 * FUEL_PER_PX) {
            double homeX = t.x, homeY = t.y;
            int lo, hi;
            driveRange(t, lo, hi);
            double ration = t.fuel / FUEL_PER_PX / 2;
            lo = (int)std::max((double)lo, homeX - ration);
            hi = (int)std::min((double)hi, homeX + ration);

            auto tryAt = [&](int x, double& outA, double& outP) {
                // Best shot if the tank stood at x (then put it back).
                t.x = (double)x;
                t.settle(terrain);
                double err = search(std::max(15, lvl.sims / 2), outA, outP);
                t.x = homeX;
                t.y = homeY;
                return err;
            };

            if (bestErr > lvl.move_err) {
                const int offs[6] = { -90, -60, -30, 30, 60, 90 };
                for (int off : offs) {
                    int cand = (int)std::lround(homeX + off);
                    if (!(lo <= cand && cand <= hi)
                        || std::fabs(cand - homeX) < 8)
                        continue;
                    double a2, p2;
                    double err = tryAt(cand, a2, p2);
                    if (err + 30 < bestErr) {        // materially better only
                        bestA = a2; bestP = p2; bestErr = err;
                        hasMove = true; moveTo = cand;
                    }
                }
            } else if (t.fuel > FUEL_MAX * 0.25 && rng.random01() < 0.3) {
                double off = rng.uniform(10, 26)
                             * (rng.randint(0, 1) == 0 ? -1 : 1);
                int cand = (int)std::lround(
                    std::max((double)lo, std::min((double)hi, homeX + off)));
                if (std::fabs(cand - homeX) >= 8) {
                    double a2, p2;
                    double err = tryAt(cand, a2, p2);
                    // A scoot must not cost real accuracy (tightest for Hard).
                    if (err <= bestErr + 8 + lvl.aim_err) {
                        bestA = a2; bestP = p2; bestErr = err;
                        hasMove = true; moveTo = cand;
                    }
                }
            }
        }

        AiPlan plan;
        plan.angle = std::max(1.0, std::min(179.0,
                                            bestA + rng.gauss(0, lvl.aim_err)));
        plan.power = std::max(5.0, std::min(100.0,
                                            bestP + rng.gauss(0, lvl.aim_err * 0.8)));
        plan.weapon = weapon;
        plan.hasMove = hasMove;
        plan.move_to = moveTo;
        return plan;
    }

    void aiAct() {
        // One frame of AI behavior during its aim phase: plan once, then
        // visibly swing the turret onto the solution before firing.
        if (ai_wait > 0) {
            ai_wait--;
            return;
        }
        Tank& t = currentTank();
        if (t.arsenal.empty()) return;
        if (!ai_plan) ai_plan = std::make_unique<AiPlan>(aiPlanShot());
        AiPlan& plan = *ai_plan;
        // Drive to the planned firing spot first, one px per frame.
        if (plan.hasMove) {
            if (std::fabs(plan.move_to - t.x) >= 1
                && moveTank(plan.move_to > t.x ? 1 : -1))
                return;
            plan.hasMove = false;
        }
        if (t.arsenal[t.weapon_i] != plan.weapon) {
            for (int i = 0; i < (int)t.arsenal.size(); i++)
                if (t.arsenal[i] == plan.weapon) { t.weapon_i = i; break; }
        }
        double da = plan.angle - t.angle;
        double dp = plan.power - t.power;
        if (std::fabs(da) > 0.6) {
            t.angle += std::max(-2.0, std::min(2.0, da));
        } else if (std::fabs(dp) > 0.6) {
            t.power += std::max(-1.6, std::min(1.6, dp));
        } else {
            t.angle = plan.angle;
            t.power = plan.power;
            fire();
        }
    }

    // -------------------------------------------------------------- main loop
    void tick() {
        step();
        if (gui) draw();
    }

    void step() {
        // Pure logic for one frame — everything --selftest exercises.
        frame++;
        if (has_toast) {
            toast_frames--;
            if (toast_frames <= 0) has_toast = false;
        }
        for (size_t i = 0; i < effects.size();) {
            effects[i].age++;
            if (effects[i].age >= effects[i].frames)
                effects.erase(effects.begin() + i);
            else
                i++;
        }
        if (state == GState::Pick) {
            if (mode == "1P" && picker == 1 && !confirm_menu) {
                if (ai_wait > 0) ai_wait--;
                else {
                    aiPick();
                    ai_wait = 14;
                }
            }
        } else if (state == GState::Playing && !confirm_menu) {
            if (aimPhase) {
                if (currentTank().is_ai) aiAct();
            } else {
                updateProjectiles();
                updateFlames();
                if (flightDone()) endTurn();
            }
        }
    }

    // ---------------------------------------------------------- state changes
    void requestMenu() {
        if (state == GState::Playing || state == GState::Pick) {
            confirm_menu = true;
            focus_action.clear();
        } else if (state == GState::GameOver) {
            toMenu();
        }
    }

    void toMenu() {
        confirm_menu = false;
        focus_action.clear();
        state = GState::Menu;
        projectiles.clear();
        effects.clear();
        flames.clear();
        saveSettings();
    }

    void saveSettings() {
        if (!persist) return;
        config.set("mode", JVal::string(mode));
        config.set("ai_level", JVal::string(ai_level));
        config.set("match_type", JVal::string(match_type));
        config.set("single_rounds", JVal::number(single_rounds));
        config.set("single_weapon", JVal::string(WEAPONS[single_weapon].key));
        config.set("move_enabled", JVal::boolean(move_enabled));
        saveConfig(config);
    }

    // Window position: same "+x+y" geometry-tail string as the Python config.
    bool savedWinPos(int& x, int& y) const {
        const JVal* v = config.get("win_pos");
        if (!v || v->type != JVal::Str || v->str.size() >= 20) return false;
        const char* s = v->str.c_str();
        char sign1, sign2;
        int a, b;
        if (sscanf(s, "%c%d%c%d", &sign1, &a, &sign2, &b) == 4
            && (sign1 == '+' || sign1 == '-')
            && (sign2 == '+' || sign2 == '-')) {
            x = (sign1 == '-') ? -a : a;
            y = (sign2 == '-') ? -b : b;
            return true;
        }
        return false;
    }

    void saveWinPos(int x, int y) {
        if (!persist) return;
        config.set("win_pos", JVal::string(
            "+" + std::to_string(x) + "+" + std::to_string(y)));
        saveSettings();
    }

    // ------------------------------------------------------- terrain painting
    static uint32_t pack(int r, int g, int b) {
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }

    void buildTerrainPalette() {
        // One color per row for the sky gradient and the dirt strata, so
        // painting a terrain column is just an indexed lookup.
        skyRow.resize(FIELD_H);
        dirtRow.resize(FIELD_H);
        auto lerp = [](int a, int b, double t) { return (int)(a + (b - a) * t); };
        for (int y = 0; y < FIELD_H; y++) {
            double t = (double)y / FIELD_H;
            skyRow[y] = pack(lerp(11, 52, t), lerp(13, 64, t), lerp(26, 100, t));
            int br = lerp(124, 74, t), bg = lerp(86, 50, t), bb = lerp(48, 30, t);
            double band = 1.0 + 0.06 * std::sin(y * 0.22);  // subtle strata
            auto cl = [band](int c) {
                return std::max(0, std::min(255, (int)(c * band)));
            };
            dirtRow[y] = pack(cl(br), cl(bg), cl(bb));
        }
        RGB g0 = hexColor("#3fae4f"), g1 = hexColor("#379a46"),
            g2 = hexColor("#2f853c");
        grassRow[0] = pack(g0.r, g0.g, g0.b);
        grassRow[1] = pack(g1.r, g1.g, g1.b);
        grassRow[2] = pack(g2.r, g2.g, g2.b);
        Rng starRng(99);
        const uint32_t shades[3] = { pack(0x88, 0x88, 0xaa),
                                     pack(0xbb, 0xbb, 0xdd),
                                     pack(0xee, 0xee, 0xff) };
        for (int i = 0; i < 90; i++) {
            int sx = starRng.randint(0, FIELD_W - 1);
            int sy = starRng.randint(0, (int)(FIELD_H * 0.7) - 1);
            stars[(long)sy * FIELD_W + sx] = shades[starRng.randint(0, 2)];
        }
    }

    void repaintTerrain(int x0 = 0, int x1 = FIELD_W - 1) {
        // Redraw terrain columns x0..x1 into the pixel buffer.
        if (terrainPix.empty()) return;
        x0 = std::max(0, x0);
        x1 = std::min(FIELD_W - 1, x1);
        if (x1 < x0) return;
        for (int y = 0; y < FIELD_H; y++) {
            uint32_t* row = &terrainPix[(size_t)y * FIELD_W];
            for (int x = x0; x <= x1; x++) {
                int h = terrain[x];
                if (y < h) {
                    auto it = stars.find((long)y * FIELD_W + x);
                    row[x] = (it != stars.end()) ? it->second : skyRow[y];
                } else if (y - h < 3) {
                    row[x] = grassRow[y - h];
                } else {
                    row[x] = dirtRow[y];
                }
            }
        }
    }

    // ------------------------------------------------------- scene emission
    void emitLine(double x0, double y0, double x1, double y1, RGB color,
                  double width = 1, bool roundCap = false) {
        Cmd c;
        c.type = Cmd::Line;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
        c.outline = color; c.hasOutline = true;
        c.width = width;
        c.roundCap = roundCap;
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

    void emitRoundRect(double x0, double y0, double x1, double y1,
                       double radius, bool hasFill, RGB fill, RGB outline,
                       double width) {
        Cmd c;
        c.type = Cmd::RoundRect;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
        c.radius = std::min(radius, std::min((x1 - x0) / 2, (y1 - y0) / 2));
        c.hasFill = hasFill; c.fill = fill;
        c.hasOutline = true; c.outline = outline; c.width = width;
        scene.push_back(c);
    }

    void emitText(double x, double y, const std::string& s, RGB color,
                  int size, bool bold, Cmd::Anchor anchor = Cmd::Center,
                  bool italic = false) {
        Cmd c;
        c.type = Cmd::Text;
        c.x0 = x; c.y0 = y;
        c.text = s; c.fill = color; c.hasFill = true;
        c.size = size; c.bold = bold; c.italic = italic;
        c.anchor = anchor;
        scene.push_back(c);
    }

    // Center-justified multi-line text (Tk create_text with '\n').
    void emitTextLines(double x, double y, const std::string& s, RGB color,
                       int size) {
        std::vector<std::string> lines;
        std::string cur;
        for (char ch : s) {
            if (ch == '\n') { lines.push_back(cur); cur.clear(); }
            else cur += ch;
        }
        lines.push_back(cur);
        double lh = size * 1.5;
        double top = y - lh * (lines.size() - 1) / 2.0;
        for (size_t i = 0; i < lines.size(); i++)
            emitText(x, top + i * lh, lines[i], color, size, false);
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

    void button(double x0, double y0, double x1, double y1,
                const std::string& label, const std::string& action,
                bool enabled = true, RGB fill = BTN_BG, RGB fg = TEXT_C,
                int size = 11) {
        emitRoundRect(x0, y0, x1, y1, 10, true,
                      enabled ? fill : BTN_DIS_BG, BTN_EDGE, 1);
        emitText((x0 + x1) / 2, (y0 + y1) / 2, label,
                 enabled ? fg : BTN_DIS_FG, size, true);
        if (enabled) {
            buttons.push_back(Button{ x0, y0, x1, y1, action });
            if (action == focus_action) focusRing(x0, y0, x1, y1);
        }
    }

    void focusRing(double x0, double y0, double x1, double y1,
                   double radius = 12) {
        emitRoundRect(x0 - 3, y0 - 3, x1 + 3, y1 + 3, radius, false, RGB{},
                      FOCUS_C, 2);
    }

    // ------------------------------------------------------------------ draw
    void draw() {
        scene.clear();
        buttons.clear();
        if (state == GState::Menu) {
            emitRect(0, 0, WIN_W, WIN_H, true, BG);
            drawMenu();
            return;
        }
        if (state == GState::Pick) {
            emitRect(0, 0, WIN_W, WIN_H, true, BG);
            drawPick();
            if (confirm_menu) drawConfirm();
            return;
        }
        emitRect(0, 0, WIN_W, WIN_H, true, BG);
        Cmd t;
        t.type = Cmd::Terrain;
        scene.push_back(t);
        drawField();
        drawPanel();
        if (state == GState::GameOver) drawGameover();
        if (confirm_menu) drawConfirm();
    }

    void drawField() {
        for (Tank& t : tanks) {
            double x = t.x, y = t.y;
            double hw = TANK_W / 2.0;
            double mx, my;
            t.muzzle(mx, my);
            emitLine(x, y - 2, mx, my, shade(t.color, 0.8), 4, true);
            emitOval(x - hw * 0.55, y - 9, x + hw * 0.55, y + 4,
                     true, shade(t.color, 1.15));
            emitRect(x - hw, y, x + hw, y + TANK_H - 4, true, t.color);
            emitOval(x - hw, y + TANK_H - 9, x + hw, y + TANK_H + 1,
                     true, shade(t.color, 0.6));
            if (state == GState::Playing && aimPhase && &t == &currentTank())
                emitPoly({ { x - 6, y - 26 }, { x + 6, y - 26 },
                           { x, y - 17 } }, GOLD);
        }
        for (const PProj& p : projectiles) {
            RGB pc = WEAPONS[p->weapon].color;
            int n = (int)p->trail.size();
            int start = std::max(0, n - 24);
            for (int i = start; i < n; i++) {
                double tx = p->trail[i].first, ty = p->trail[i].second;
                emitOval(tx - 1, ty - 1, tx + 1, ty + 1, true,
                         shade(pc, 0.35 + 0.5 * (i - start) / 24.0));
            }
            emitOval(p->x - 3, p->y - 3, p->x + 3, p->y + 3, true, pc,
                     true, WHITE, 1);
        }
        // Napalm flames flicker between two sizes/colors by frame parity.
        for (const Flame& f : flames) {
            int xi = (int)std::max(0.0, std::min((double)FIELD_W - 1, f.x));
            int gy = terrain[xi];
            bool big = (frame + xi) % 4 < 2;
            double h = big ? 10 : 7;
            RGB col = big ? hexColor("#ff7a22") : hexColor("#ffd91a");
            emitOval(f.x - 3, gy - h, f.x + 3, gy + 2, true, col);
        }
        for (const Effect& e : effects) {
            double frac = (double)e.age / e.frames;
            if (e.type == Effect::Boom) {
                double r = e.r * (0.35 + 0.65 * frac);
                const RGB cols[3] = { hexColor("#ff5522"),
                                      hexColor("#ff9f1a"),
                                      hexColor("#ffef7a") };
                const double rs[3] = { r, r * 0.7, r * 0.4 };
                for (int i = 0; i < 3; i++)
                    emitOval(e.x - rs[i], e.y - rs[i], e.x + rs[i], e.y + rs[i],
                             false, RGB{}, true, cols[i], 3);
            } else if (e.type == Effect::BeamFx) {
                double wdt = 10 * (1 - frac) + 2;
                emitRect(e.x - wdt, 0, e.x + wdt, e.y + 14, true,
                         hexColor("#66eaff"));
                emitRect(e.x - wdt / 3, 0, e.x + wdt / 3, e.y + 14, true,
                         hexColor("#e8ffff"));
            } else if (e.type == Effect::QuakeFx) {
                double r = e.r * frac;
                if (r > 1) {
                    std::vector<std::pair<double, double>> pts;
                    for (int i = 0; i <= 24; i++) {
                        double th = 3.14159265358979323846 * i / 24;
                        pts.emplace_back(e.x + r * std::cos(th),
                                         e.y - (r / 2) * std::sin(th));
                    }
                    emitPolyline(pts, hexColor("#c08a5a"), 3);
                }
            } else if (e.type == Effect::TextFx) {
                emitText(e.x, e.y - 22 * frac, e.text, e.color, 13, true);
            }
        }
        // HUD: scores, wind, toast.
        Tank& t0 = tanks[0];
        Tank& t1 = tanks[1];
        char buf[80];
        snprintf(buf, sizeof buf, "%s  %4d", t0.name.c_str(), t0.score);
        emitText(14, 12, buf, t0.color, 14, true, Cmd::W);
        snprintf(buf, sizeof buf, "%-4d  %s", t1.score, t1.name.c_str());
        emitText(FIELD_W - 14, 12, buf, t1.color, 14, true, Cmd::E);
        drawWind(FIELD_W / 2, 16);
        if (has_toast)
            emitText(FIELD_W / 2, 52, toast_text, GOLD, 15, true);
    }

    void drawWind(double cx, double cy) {
        emitText(cx, cy - 6, "WIND", SUBTEXT, 8, false);
        if (wind == 0) {
            emitText(cx, cy + 8, "CALM", TEXT_C, 10, false);
            return;
        }
        double ln = std::abs(wind) * 4;
        int d = wind > 0 ? 1 : -1;
        double x0 = cx - d * ln / 2, x1 = cx + d * ln / 2, y = cy + 8;
        emitLine(x0, y, x1, y, WIND_C, 3);
        emitPoly({ { x1 + d * 8, y }, { x1 - d * 2, y - 4 },
                   { x1 - d * 2, y + 4 } }, WIND_C);
        emitText(cx, cy + 20, std::to_string(std::abs(wind)), WIND_C, 9, true);
    }

    // ----------------------------------------------------------------- panel
    void drawPanel() {
        double top = FIELD_H;
        emitRect(0, top, WIN_W, WIN_H, true, PANEL_BG, true, PANEL_EDGE, 2);
        Tank& t = currentTank();
        bool live = (state == GState::Playing && aimPhase && !t.is_ai
                     && !confirm_menu);
        char buf[120];

        // --- left: angle / power / move -----------------------------------
        double lx = 20;
        emitText(lx, top + 24, "ANGLE", SUBTEXT, 10, false, Cmd::W);
        snprintf(buf, sizeof buf, "%5.1f\xC2\xB0", t.angle);
        emitText(lx + 74, top + 24, buf, TEXT_C, 13, true, Cmd::W);
        button(lx + 160, top + 12, lx + 196, top + 36, "-", "angle-", live);
        button(lx + 202, top + 12, lx + 238, top + 36, "+", "angle+", live);
        emitText(lx, top + 60, "POWER", SUBTEXT, 10, false, Cmd::W);
        snprintf(buf, sizeof buf, "%5.1f ", t.power);
        emitText(lx + 74, top + 60, buf, TEXT_C, 13, true, Cmd::W);
        button(lx + 160, top + 48, lx + 196, top + 72, "-", "power-", live);
        button(lx + 202, top + 48, lx + 238, top + 72, "+", "power+", live);
        emitRect(lx, top + 76, lx + 238, top + 84, false, RGB{}, true,
                 BTN_EDGE, 1);
        emitRect(lx + 1, top + 77, lx + 1 + 236 * t.power / 100, top + 83,
                 true, t.color);
        if (move_enabled) {
            emitText(lx, top + 104, "MOVE", SUBTEXT, 10, false, Cmd::W);
            bool canMove = live && t.fuel >= FUEL_PER_PX;
            // U+25C4/U+25BA, not the Python source's U+25C0/U+25B6: Tk
            // font-substitutes missing glyphs, GDI draws boxes — and
            // Consolas only carries the DOS-era pointer pair.
            button(lx + 74, top + 92, lx + 116, top + 116,
                   "\xE2\x97\x84", "moveL", canMove);
            button(lx + 122, top + 92, lx + 164, top + 116,
                   "\xE2\x96\xBA", "moveR", canMove);
            snprintf(buf, sizeof buf, "FUEL %5.1f", t.fuel);
            emitText(lx, top + 134, buf, SUBTEXT, 9, false, Cmd::W);
            emitRect(lx + 90, top + 128, lx + 238, top + 138, false, RGB{},
                     true, BTN_EDGE, 1);
            emitRect(lx + 91, top + 129, lx + 91 + 146 * t.fuel / FUEL_MAX,
                     top + 137, true, FUEL_C);
        }
        std::string moveKeys = move_enabled ? "A/D move  " : "";
        emitText(lx, top + 162,
                 "keys: \xE2\x86\x90\xE2\x86\x92 or End/Home angle  "
                 "\xE2\x86\x91\xE2\x86\x93 power  " + moveKeys
                 + "[ ] weapon  Space fire",
                 HINT_C, 9, false, Cmd::W);

        // --- middle: weapon selector ---------------------------------------
        double mx = 320;
        int wi = t.currentWeapon();
        snprintf(buf, sizeof buf, "%s \xE2\x80\x94 SHOT %d/%d",
                 t.name.c_str(),
                 std::min(rounds, shots_fired[t.pid] + 1), rounds);
        emitText(mx + 170, top + 18, buf, t.color, 11, true);
        button(mx, top + 34, mx + 40, top + 86, "\xE2\x97\x84", "wprev", live);
        button(mx + 300, top + 34, mx + 340, top + 86,
               "\xE2\x96\xBA", "wnext", live);
        emitRect(mx + 48, top + 34, mx + 292, top + 86, true, CARD_BG,
                 true, BTN_EDGE, 1);
        if (wi >= 0) {
            const Weapon& w = WEAPONS[wi];
            emitRect(mx + 58, top + 44, mx + 78, top + 76, true, w.color);
            int count = 0;
            for (int k : t.arsenal) if (k == wi) count++;
            std::string extra = count > 1 ? " x" + std::to_string(count) : "";
            emitText(mx + 88, top + 52, std::string(w.name) + extra, TEXT_C,
                     13, true, Cmd::W);
            emitText(mx + 88, top + 71, w.blurb, SUBTEXT, 9, false, Cmd::W);
        }
        // remaining arsenal dots (pitch shrinks so long one-weapon
        // arsenals still fit between the selector and the FIRE button)
        int n = (int)t.arsenal.size();
        double pitch = n ? std::min(26, 356 / n) : 26;
        double dw = std::max(4.0, std::min(18.0, pitch - 8));
        for (int i = 0; i < n; i++) {
            RGB col = WEAPONS[t.arsenal[i]].color;
            double x0 = mx + 48 + i * pitch;
            emitRect(x0, top + 96, x0 + dw, top + 114, true, col,
                     i == t.weapon_i, WHITE, 2);
        }
        std::string noun = (match_type == "single") ? "shots" : "weapons";
        emitText(mx + 170, top + 136,
                 noun + " left: " + std::to_string(n), SUBTEXT, 9, false);

        // --- right: FIRE ----------------------------------------------------
        double fx = 740;
        button(fx, top + 30, fx + 230, top + 100, "F I R E", "fire", live,
               live ? FIRE_BG : BTN_BG, live ? FIRE_FG : TEXT_C, 22);
        std::string status;
        if (state == GState::Playing && t.is_ai && aimPhase) {
            bool driving = ai_plan && ai_plan->hasMove;
            status = driving ? "computer is driving..."
                             : "computer is aiming...";
        } else if (!aimPhase) {
            status = "shot in flight";
        }
        emitText(fx + 115, top + 120, status, SUBTEXT, 10, false,
                 Cmd::Center, true);
        emitText(fx + 115, top + 156, "Tab+Enter buttons   M mute   Esc menu",
                 HINT_C, 9, false);
    }

    // ------------------------------------------------------------------ menu
    void drawMenu() {
        double cx = WIN_W / 2.0;
        emitText(cx, 100, "MY POCKET TANKS", GOLD, 44, true);
        emitText(cx, 146, "artillery duel on destructible ground", SUBTEXT,
                 14, false);
        double y = 204;
        emitText(cx - 260, y, "MODE", SUBTEXT, 13, false, Cmd::W);
        {
            const char* modes[2] = { "1P", "2P" };
            const char* labels[2] = { "1 PLAYER vs COMPUTER",
                                      "2 PLAYER HOTSEAT" };
            for (int i = 0; i < 2; i++) {
                bool sel = mode == modes[i];
                double x0 = cx - 150 + i * 260;
                button(x0, y - 18, x0 + 245, y + 18, labels[i],
                       std::string("mode:") + modes[i], true,
                       sel ? SEL_BG : BTN_BG, sel ? GOLD : TEXT_C, 11);
            }
        }
        y = 258;
        if (mode == "1P") {
            emitText(cx - 260, y, "AI", SUBTEXT, 13, false, Cmd::W);
            for (int i = 0; i < N_AI; i++) {
                bool sel = ai_level == AI_LEVELS[i].name;
                double x0 = cx - 150 + i * 175;
                std::string upper = AI_LEVELS[i].name;
                for (char& ch : upper) ch = (char)toupper((unsigned char)ch);
                button(x0, y - 18, x0 + 160, y + 18, upper,
                       std::string("ai:") + AI_LEVELS[i].name, true,
                       sel ? SEL_BG : BTN_BG, sel ? GOLD : TEXT_C, 11);
            }
            emitText(cx - 150, y + 27, aiLevel().blurb, SUBTEXT, 10, false,
                     Cmd::W);
        }
        y = 312;
        emitText(cx - 260, y, "MATCH", SUBTEXT, 13, false, Cmd::W);
        {
            const char* types[2] = { "draft", "single" };
            const char* labels[2] = { "DRAFT 10 FROM POOL", "ONE WEAPON ONLY" };
            for (int i = 0; i < 2; i++) {
                bool sel = match_type == types[i];
                double x0 = cx - 150 + i * 260;
                button(x0, y - 18, x0 + 245, y + 18, labels[i],
                       std::string("match:") + types[i], true,
                       sel ? SEL_BG : BTN_BG, sel ? GOLD : TEXT_C, 11);
            }
        }
        y = 366;
        emitText(cx - 260, y, "MOVING", SUBTEXT, 13, false, Cmd::W);
        {
            char drive[32];
            snprintf(drive, sizeof drive, "DRIVE (FUEL %.0f)", FUEL_MAX);
            const char* vals[2] = { "off", "on" };
            const char* labels[2] = { "TANKS PARKED", drive };
            for (int i = 0; i < 2; i++) {
                bool sel = move_enabled == (i == 1);
                double x0 = cx - 150 + i * 260;
                button(x0, y - 18, x0 + 245, y + 18, labels[i],
                       std::string("move:") + vals[i], true,
                       sel ? SEL_BG : BTN_BG, sel ? GOLD : TEXT_C, 11);
            }
        }
        if (match_type == "single") {
            y = 420;
            emitText(cx - 260, y, "ROUNDS", SUBTEXT, 13, false, Cmd::W);
            button(cx - 150, y - 18, cx - 114, y + 18, "-", "rounds-",
                   single_rounds > ROUNDS_MIN);
            emitText(cx - 80, y, std::to_string(single_rounds), GOLD, 16, true);
            button(cx - 46, y - 18, cx - 10, y + 18, "+", "rounds+",
                   single_rounds < ROUNDS_MAX);
            emitText(cx + 20, y, "shots per tank \xE2\x80\x94 pick the weapon "
                     "next", SUBTEXT, 10, false, Cmd::W);
        }
        button(cx - 130, 452, cx + 130, 510, "S T A R T", "start", true,
               START_BG, START_FG, 20);
        std::string what;
        if (match_type == "single") {
            int nr = single_rounds;
            what = "Pick ONE weapon; both tanks fire it every round: "
                   + std::to_string(nr) + " shot" + (nr != 1 ? "s" : "")
                   + " per side.";
        } else {
            what = "Draft 10 weapons each, then take turns: "
                   + std::to_string(ROUNDS) + " shots per side.";
        }
        emitTextLines(cx, 548, what + "\nDamage dealt = points scored. "
                      "Self-damage scores for your opponent. "
                      "Most points wins.", SUBTEXT, 10);
        std::string moveHint = move_enabled ? "   A/D move" : "";
        emitTextLines(cx, WIN_H - 60,
                      "\xE2\x86\x90\xE2\x86\x92 or End/Home angle   "
                      "\xE2\x86\x91\xE2\x86\x93 power" + moveHint
                      + "   [ ] weapon   Space fire   M mute   Esc menu\n"
                      "Tab/Shift-Tab or the arrow keys walk the buttons, "
                      "Enter presses \xE2\x80\x94 or Enter/click START to play",
                      HINT_C, 10);
    }

    // ------------------------------------------------------------------ pick
    void drawPick() {
        double cx = WIN_W / 2.0;
        bool single = match_type == "single";
        if (single) {
            emitText(cx, 34, "CHOOSE THE MATCH WEAPON", GOLD, 24, true);
            emitText(cx, 66, "both tanks fire it for all "
                     + std::to_string(rounds) + " round"
                     + (rounds != 1 ? "s" : ""), TEXT_C, 13, true);
        } else {
            Tank& picking = tanks[picker];
            emitText(cx, 34, "WEAPON DRAFT", GOLD, 24, true);
            std::string who = (mode == "1P" && picker == 1)
                ? "COMPUTER IS PICKING..."
                : picking.name + " PICKS";
            emitText(cx, 66, who, picking.color, 15, true);
            for (int pid = 0; pid < 2; pid++) {
                Tank& t = tanks[pid];
                double x = pid == 0 ? 130 : WIN_W - 130;
                emitText(x, 34, t.name + ": "
                         + std::to_string(t.arsenal.size()) + "/"
                         + std::to_string(rounds), t.color, 11, true);
            }
        }
        // cards, 5 x 4 grid
        bool humanTurn = (single || !(mode == "1P" && picker == 1))
                         && !confirm_menu;
        const int cols = 5;
        const double cw = 182, ch = 96, gap = 8;
        double gx = (WIN_W - cols * cw - (cols - 1) * gap) / 2;
        double gy = 96;
        for (int i = 0; i < (int)pool.size(); i++) {
            const Weapon& w = WEAPONS[pool[i]];
            int col = i % cols, row = i / cols;
            double x0 = gx + col * (cw + gap);
            double y0 = gy + row * (ch + gap);
            bool last = single && pool[i] == single_weapon;
            emitRoundRect(x0, y0, x0 + cw, y0 + ch, 12, true, CARD_BG,
                          last ? GOLD : BTN_EDGE, last ? 2 : 1);
            emitRect(x0 + 10, y0 + 12, x0 + 26, y0 + 40, true, w.color);
            emitText(x0 + 34, y0 + 20, w.name, TEXT_C, 11, true, Cmd::W);
            emitText(x0 + 34, y0 + 38, "dmg " + std::to_string(w.dmg)
                     + "  r " + std::to_string(w.r), SUBTEXT, 9, false, Cmd::W);
            emitText(x0 + 10, y0 + 62, w.blurb, SUBTEXT, 9, false, Cmd::W);
            if (humanTurn) {
                std::string action = "pick:" + std::to_string(i);
                buttons.push_back(Button{ x0, y0, x0 + cw, y0 + ch, action });
                if (focus_action == action)
                    focusRing(x0, y0, x0 + cw, y0 + ch, 14);
            }
        }
        emitText(cx, WIN_H - 40,
                 single ? "click or Tab+Enter the weapon both tanks will use "
                          "for the whole match"
                        : "click or Tab+Enter a card to draft it \xE2\x80\x94 "
                          "you alternate picks with your opponent",
                 HINT_C, 10, false);
    }

    // ------------------------------------------------------------- overlays
    void drawGameover() {
        double cx = WIN_W / 2.0;
        emitRect(cx - 320, 150, cx + 320, 420, true, PANEL_BG, true, GOLD, 2);
        if (winner == nullptr) {
            emitText(cx, 210, "DEAD HEAT!", GOLD, 30, true);
        } else {
            emitText(cx, 210, winner->name + " WINS!", winner->color, 30,
                     true);
        }
        Tank& t0 = tanks[0];
        Tank& t1 = tanks[1];
        emitText(cx, 270, t0.name + " " + std::to_string(t0.score)
                 + "   \xE2\x80\x94   " + std::to_string(t1.score) + " "
                 + t1.name, TEXT_C, 16, true);
        button(cx - 220, 320, cx - 20, 370, "REMATCH (R)", "rematch");
        button(cx + 20, 320, cx + 220, 370, "MENU (Esc)", "menu");
    }

    void drawConfirm() {
        double cx = WIN_W / 2.0;
        emitRect(cx - 240, 220, cx + 240, 340, true, PANEL_BG, true, GOLD, 2);
        emitText(cx, 258, "Return to menu? Match is lost.", TEXT_C, 13, true);
        button(cx - 160, 285, cx - 20, 320, "YES (Y)", "confirmY");
        button(cx + 20, 285, cx + 160, 320, "NO (N)", "confirmN");
    }

    // ----------------------------------------------------------------- input
    void focusMove(int step) {
        if (buttons.empty()) { focus_action.clear(); return; }
        int idx = -1;
        for (int i = 0; i < (int)buttons.size(); i++)
            if (buttons[i].action == focus_action) { idx = i; break; }
        int n = (int)buttons.size();
        int i = (idx >= 0) ? ((idx + step) % n + n) % n
                           : (step >= 0 ? 0 : n - 1);
        focus_action = buttons[i].action;
    }

    void focusSpatial(int direction) {
        const Button* cur = nullptr;
        for (const Button& b : buttons)
            if (b.action == focus_action) { cur = &b; break; }
        if (!cur) { focusMove(direction > 0 ? 1 : -1); return; }
        double cx = (cur->x0 + cur->x1) / 2, cy = (cur->y0 + cur->y1) / 2;
        struct Cand { double d, dx; const std::string* action; };
        std::vector<Cand> ahead, behind;
        for (const Button& b : buttons) {
            if (b.action == focus_action) continue;
            double dy = ((b.y0 + b.y1) / 2 - cy) * direction;
            double dx = std::fabs((b.x0 + b.x1) / 2 - cx);
            if (dy > 0.5) ahead.push_back(Cand{ dy, dx, &b.action });
            else if (dy < -0.5) behind.push_back(Cand{ -dy, dx, &b.action });
        }
        auto lessCand = [](const Cand& a, const Cand& b) {
            if (a.d != b.d) return a.d < b.d;
            return a.dx < b.dx;
        };
        if (!ahead.empty()) {
            focus_action = *std::min_element(ahead.begin(), ahead.end(),
                                             lessCand)->action;
        } else if (!behind.empty()) {         // screen edge: wrap to farthest
            double farthest = 0;
            for (const Cand& c : behind) farthest = std::max(farthest, c.d);
            const Cand* pick = nullptr;
            for (const Cand& c : behind)
                if (c.d > farthest - 0.5 && (!pick || c.dx < pick->dx))
                    pick = &c;
            if (pick) focus_action = *pick->action;
        }
    }

    bool focusActivate() {
        if (focus_action.empty()) return false;
        for (const Button& b : buttons)
            if (b.action == focus_action) { doAction(b.action); return true; }
        return false;
    }

    void onClick(double x, double y) {
        for (const Button& b : buttons)
            if (b.x0 <= x && x <= b.x1 && b.y0 <= y && y <= b.y1) {
                doAction(b.action);
                return;
            }
    }

    void doAction(const std::string& action) {
        auto starts = [&](const char* prefix) {
            return action.rfind(prefix, 0) == 0;
        };
        if (action == "start") {
            startMatch();
        } else if (starts("mode:")) {
            mode = action.substr(5);
            sound->play("blip");
        } else if (starts("ai:")) {
            ai_level = action.substr(3);
            sound->play("blip");
        } else if (starts("match:")) {
            match_type = action.substr(6);
            sound->play("blip");
        } else if (starts("move:")) {
            move_enabled = action.substr(5) == "on";
            sound->play("blip");
        } else if (action == "rounds-") {
            single_rounds = std::max(ROUNDS_MIN, single_rounds - 1);
            sound->play("blip");
        } else if (action == "rounds+") {
            single_rounds = std::min(ROUNDS_MAX, single_rounds + 1);
            sound->play("blip");
        } else if (starts("pick:")) {
            pickWeapon(atoi(action.c_str() + 5));
        } else if (action == "angle-") {
            adjustAngle(-1);
        } else if (action == "angle+") {
            adjustAngle(1);
        } else if (action == "power-") {
            adjustPower(-1);
        } else if (action == "power+") {
            adjustPower(1);
        } else if (action == "moveL") {
            for (int i = 0; i < 4; i++) moveTank(-1);
        } else if (action == "moveR") {
            for (int i = 0; i < 4; i++) moveTank(1);
        } else if (action == "wprev") {
            currentTank().cycleWeapon(-1);
            sound->play("blip");
        } else if (action == "wnext") {
            currentTank().cycleWeapon(1);
            sound->play("blip");
        } else if (action == "fire") {
            fire();
        } else if (action == "rematch") {
            startMatch();
        } else if (action == "menu") {
            toMenu();
        } else if (action == "confirmY") {
            toMenu();
        } else if (action == "confirmN") {
            confirm_menu = false;
        }
    }

    void handleKey(Key key, bool shift) {
        if (key == Key::M) { sound->toggleMute(); return; }
        // Keyboard focus, ahead of every state branch (see the .py comments).
        if (key == Key::Tab) {
            focusMove(shift ? -1 : 1);
            return;
        }
        if (key == Key::Enter && focusActivate()) return;
        // The arrow cluster also navigates — everywhere those keys are not
        // game controls (a human aim turn keeps them for angle/power).
        bool aiming = (state == GState::Playing && aimPhase && !confirm_menu
                       && !currentTank().is_ai);
        if (!aiming) {
            if (key == Key::Left || key == Key::Home) { focusMove(-1); return; }
            if (key == Key::Right || key == Key::End) { focusMove(1); return; }
            if (key == Key::Up || key == Key::PgUp) { focusSpatial(-1); return; }
            if (key == Key::Down || key == Key::PgDn) { focusSpatial(1); return; }
        }
        if (confirm_menu) {
            if (key == Key::Y) toMenu();
            else if (key == Key::N || key == Key::Escape) confirm_menu = false;
            return;
        }
        if (state == GState::Menu) {
            if (key == Key::Enter || key == Key::Space) startMatch();
            else if (key == Key::One) mode = "1P";
            else if (key == Key::Two) mode = "2P";
            return;
        }
        if (state == GState::GameOver) {
            if (key == Key::R || key == Key::Enter || key == Key::Space)
                startMatch();
            else if (key == Key::Escape) toMenu();
            return;
        }
        if (key == Key::Escape) {
            requestMenu();
            return;
        }
        if (state != GState::Playing || !aimPhase || currentTank().is_ai)
            return;
        double step = shift ? 5 : 1;
        if (key == Key::Left) adjustAngle(step);   // left = raise to the left
        else if (key == Key::Right) adjustAngle(-step);
        else if (key == Key::End) adjustAngle(step);
        else if (key == Key::Home) adjustAngle(-step);
        else if (key == Key::Up) adjustPower(step);
        else if (key == Key::Down) adjustPower(-step);
        else if (key == Key::A) moveTank(-1);
        else if (key == Key::D) moveTank(1);
        else if (key == Key::BracketRight) {
            currentTank().cycleWeapon(1);
            sound->play("blip");
        } else if (key == Key::BracketLeft) {
            currentTank().cycleWeapon(-1);
            sound->play("blip");
        } else if (key == Key::Space || key == Key::Enter) {
            fire();
        }
    }
};

// ----------------------------------------------------------------------------
// Self-test — the same headless checks as MyPocketTanks.py --selftest.
// ----------------------------------------------------------------------------
static int gSelftestFailures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "SELFTEST FAIL line %d: %s (%s)\n", __LINE__, #cond, msg); \
    gSelftestFailures++; } } while (0)

static void assertTerrainOk(PocketTanks& g, const char* ctx) {
    CHECK((int)g.terrain.size() == FIELD_W, ctx);
    for (int x = 0; x < FIELD_W; x += 7) {
        int y = g.terrain[x];
        CHECK(TERRAIN_MIN <= y && y <= TERRAIN_MAX, ctx);
        if (!(TERRAIN_MIN <= y && y <= TERRAIN_MAX)) return;
    }
}

static void runUntilResolved(PocketTanks& g, const char* ctx,
                             int maxFrames = 4000) {
    int frames = 0;
    while (!g.aimPhase && g.state == GState::Playing && frames < maxFrames) {
        g.step();
        frames++;
    }
    CHECK(g.aimPhase || g.state == GState::GameOver, ctx);
}

static int runSelftest() {
    printf("MyPocketTanks selftest...\n");
    SoundIface silent;

    // 1. Every weapon fires and resolves, at several angles/powers.
    const int ANGLES[4][2] = { {45, 75}, {80, 40}, {135, 90}, {20, 100} };
    for (int wi = 0; wi < N_WEAPONS; wi++) {
        const Weapon& w = WEAPONS[wi];
        for (auto& ap : ANGLES) {
            PocketTanks g(false, &silent, false,
                          (long)((fnv1a(w.key) * 31 + ap[0]) & 0xFFFF));
            g.mode = "2P";
            g.startMatch();
            g.state = GState::Playing;
            g.aimPhase = true;
            for (Tank& t : g.tanks) {
                t.arsenal.assign(3, wi);
                t.weapon_i = 0;
            }
            g.turn = 0;
            Tank& t = g.currentTank();
            t.angle = ap[0];
            t.power = ap[1];
            int before[2] = { g.tanks[0].score, g.tanks[1].score };
            g.fire();
            CHECK(!g.aimPhase, w.key);
            runUntilResolved(g, w.key);
            assertTerrainOk(g, w.key);
            CHECK(g.tanks[0].score >= before[0]
                  && g.tanks[1].score >= before[1], w.key);
        }
        printf("  weapon ok: %s\n", w.name);
    }

    // 2. Tank movement respects fuel, bounds, and slopes.
    {
        PocketTanks g(false, &silent, false, 7);
        g.mode = "2P";
        g.startMatch();
        g.state = GState::Playing;
        for (Tank& t : g.tanks) t.arsenal.assign(ROUNDS, 0);
        Tank& t = g.currentTank();
        int moved = 0;
        for (int i = 0; i < 1000; i++)
            if (g.moveTank(1)) moved++;
        CHECK(t.fuel >= 0, "fuel went negative");
        CHECK(moved <= FUEL_MAX / FUEL_PER_PX, "moved farther than fuel");
        printf("  movement ok (%d px on a full tank)\n", moved);
    }

    // 2b. The MOVING toggle parks everything.
    {
        PocketTanks g(false, &silent, false, 7);
        g.mode = "2P";
        g.move_enabled = false;
        g.startMatch();
        g.state = GState::Playing;
        for (Tank& t : g.tanks) t.arsenal.assign(ROUNDS, 0);
        Tank& t = g.currentTank();
        double x0 = t.x;
        bool movedOff = g.moveTank(1);
        CHECK(!movedOff && t.x == x0 && t.fuel == FUEL_MAX,
              "moving-off game still drove");
        AiPlan plan = g.aiPlanShot();
        CHECK(!plan.hasMove, "moving-off plan wants to drive");
        printf("  moving toggle ok (parked tanks stay parked)\n");
    }

    // 2c. driveRange mirrors moveTank's rules: fuel, cliffs, the enemy.
    {
        PocketTanks g(false, &silent, false, 7);
        g.mode = "2P";
        g.startMatch();
        g.state = GState::Playing;
        g.terrain.assign(FIELD_W, 400);       // flat proving ground
        Tank& t = g.currentTank();
        Tank& e = g.enemyTank();
        t.x = 300.0;
        e.x = 700.0;
        for (Tank& tk : g.tanks) tk.settle(g.terrain);
        t.fuel = 20.0;                        // = 40 px of driving
        int lo, hi;
        g.driveRange(t, lo, hi);
        CHECK(lo == 260 && hi == 340, "flat range");
        int wall = (int)t.x + 15;
        for (int x = wall; x < FIELD_W; x++)
            g.terrain[x] = 340;               // 60 px cliff: too steep
        g.driveRange(t, lo, hi);
        CHECK(hi == wall - 1, "cliff not respected");
        g.terrain.assign(FIELD_W, 400);
        e.x = 350.0;
        e.settle(g.terrain);
        g.driveRange(t, lo, hi);
        CHECK(hi == 350 - TANK_W, "drove through the enemy");
        printf("  drive range ok (fuel, cliffs, enemy block)\n");
    }

    // 2d. The AI executes a move plan: drives, spends fuel, then fires.
    {
        PocketTanks g(false, &silent, false, 13);
        g.mode = "1P";
        g.startMatch();
        g.state = GState::Playing;
        g.aimPhase = true;
        g.terrain.assign(FIELD_W, 400);
        for (Tank& t : g.tanks) {
            t.arsenal.assign(3, 0);
            t.weapon_i = 0;
            t.settle(g.terrain);
        }
        g.turn = 1;                           // tanks[1] is the AI in 1P mode
        Tank& ai = g.currentTank();
        CHECK(ai.is_ai, "tanks[1] should be AI");
        int target = (int)ai.x - 40;
        auto plan = std::make_unique<AiPlan>();
        plan->angle = 135.0;
        plan->power = 60.0;
        plan->weapon = 0;
        plan->hasMove = true;
        plan->move_to = target;
        g.ai_plan = std::move(plan);
        g.ai_wait = 0;
        double fuel0 = ai.fuel;
        for (int i = 0; i < 600 && g.aimPhase; i++) g.step();
        CHECK(!g.aimPhase, "AI with a move plan never fired");
        CHECK(std::fabs(ai.x - target) < 1.5, "AI stopped short");
        CHECK(fuel0 - ai.fuel >= 39 * FUEL_PER_PX, "drive didn't spend fuel");
        printf("  AI drive ok (drove to %d, fuel %.0f -> %.0f)\n", target,
               fuel0, ai.fuel);
    }

    // 3. Full AI-vs-AI matches at every difficulty level.
    for (int li = 0; li < N_AI; li++) {
        const char* level = AI_LEVELS[li].name;
        PocketTanks g(false, &silent, false, (long)fnv1a(level));
        g.mode = "1P";
        g.ai_level = level;
        g.startMatch();
        g.tanks[0].is_ai = true;              // both sides play themselves
        while (g.state == GState::Pick) g.aiPick();
        for (Tank& t : g.tanks)
            CHECK((int)t.arsenal.size() == ROUNDS, level);
        long frames = 0;
        while (g.state != GState::GameOver && frames < 400000) {
            g.step();
            frames++;
        }
        CHECK(g.state == GState::GameOver, level);
        CHECK(g.shots_fired[0] == ROUNDS && g.shots_fired[1] == ROUNDS, level);
        for (Tank& t : g.tanks) {
            CHECK(t.score >= 0, level);
            CHECK(0 <= t.fuel && t.fuel <= FUEL_MAX, level);
        }
        assertTerrainOk(g, level);
        double drove = ((FUEL_MAX - g.tanks[0].fuel)
                        + (FUEL_MAX - g.tanks[1].fuel)) / FUEL_PER_PX;
        printf("  match ok: AI %-6s \xE2\x80\x94 final %d : %d "
               "(%ld frames, %.0f px driven)\n", level, g.tanks[0].score,
               g.tanks[1].score, frames, drove);
    }

    // 4. One-weapon matches: both tanks share one weapon for N rounds.
    {
        const struct { int rounds; const char* wkey; } cases[2] = {
            { 1, "bigone" }, { 4, "triple" }
        };
        for (auto& cse : cases) {
            PocketTanks g(false, &silent, false, cse.rounds);
            g.mode = "1P";
            g.ai_level = "Easy";
            g.match_type = "single";
            g.single_rounds = cse.rounds;
            g.startMatch();
            CHECK(g.state == GState::Pick
                  && (int)g.pool.size() == N_WEAPONS,
                  "single mode should offer every weapon");
            int prevIdx = -1;
            for (int i = 0; i < (int)g.pool.size(); i++)
                if (g.pool[i] == g.single_weapon) prevIdx = i;
            CHECK(g.focus_action == "pick:" + std::to_string(prevIdx),
                  "one-weapon pick should focus the previous choice");
            int wi = weaponByKey(cse.wkey);
            int poolIdx = -1;
            for (int i = 0; i < (int)g.pool.size(); i++)
                if (g.pool[i] == wi) poolIdx = i;
            g.pickWeapon(poolIdx);
            CHECK(g.state == GState::Playing, "single pick didn't start");
            for (Tank& t : g.tanks) {
                CHECK((int)t.arsenal.size() == cse.rounds, cse.wkey);
                for (int k : t.arsenal) CHECK(k == wi, cse.wkey);
            }
            g.tanks[0].is_ai = true;          // both sides play themselves
            long frames = 0;
            while (g.state != GState::GameOver && frames < 200000) {
                g.step();
                frames++;
            }
            CHECK(g.state == GState::GameOver, "one-weapon never finished");
            CHECK(g.shots_fired[0] == cse.rounds
                  && g.shots_fired[1] == cse.rounds, cse.wkey);
            assertTerrainOk(g, cse.wkey);
            int s0 = g.tanks[0].score, s1 = g.tanks[1].score;
            g.startMatch();                   // rematch: pick screen returns
            int wIdx = -1;
            for (int i = 0; i < (int)g.pool.size(); i++)
                if (g.pool[i] == wi) wIdx = i;
            CHECK(g.focus_action == "pick:" + std::to_string(wIdx),
                  "rematch should focus the weapon picked last time");
            printf("  one-weapon ok: %s x%d \xE2\x80\x94 %d:%d\n", cse.wkey,
                   cse.rounds, s0, s1);
        }
    }

    // 5. Keyboard focus: pure logic over the per-frame buttons list.
    {
        PocketTanks g(false, &silent, false, 11);
        CHECK(g.focus_action.empty(), "starts unfocused");
        g.focusMove(1);                       // no buttons: stays unfocused
        CHECK(g.focus_action.empty() && !g.focusActivate(), "no buttons");
        g.buttons = { {0,0,1,1,"a"}, {0,0,1,1,"b"}, {0,0,1,1,"c"} };
        g.focusMove(1);
        CHECK(g.focus_action == "a", "Tab enters at the first button");
        g.focusMove(1);
        g.focusMove(1);
        CHECK(g.focus_action == "c", "walks forward");
        g.focusMove(1);
        CHECK(g.focus_action == "a", "wraps forward");
        g.focusMove(-1);
        CHECK(g.focus_action == "c", "wraps backward");
        g.focus_action.clear();
        g.focusMove(-1);
        CHECK(g.focus_action == "c", "Shift-Tab enters at the last");
        g.focus_action = "gone";              // stale focus (screen changed)
        CHECK(!g.focusActivate(), "Enter on stale focus: no-op");
        g.focusMove(1);
        CHECK(g.focus_action == "a", "Tab recovers cleanly");
        g.confirm_menu = true;
        g.buttons = { {0,0,1,1,"confirmY"}, {0,0,1,1,"confirmN"} };
        g.focus_action = "confirmN";
        CHECK(g.focusActivate() && g.confirm_menu == false, "confirmN");
        g.buttons.clear();
        g.focusMove(1);
        CHECK(g.focus_action.empty(), "empty screens drop the focus");
        g.focus_action = "x";
        g.startMatch();
        CHECK(g.focus_action.empty()
              || g.focus_action.rfind("pick:", 0) == 0,
              "screen changes drop stale focus");
        // Spatial moves hop rows column-wise and wrap.
        g.buttons = { {0,0,10,10,"tl"}, {20,0,30,10,"tr"},
                      {0,20,10,30,"bl"}, {20,20,30,30,"br"} };
        g.focus_action = "tl";
        g.focusSpatial(1);
        CHECK(g.focus_action == "bl", "down stays in the column");
        g.focusSpatial(1);
        CHECK(g.focus_action == "tl", "wraps bottom -> top");
        g.focusSpatial(-1);
        CHECK(g.focus_action == "bl", "up wraps top -> bottom");
        g.focus_action = "br";
        g.focusSpatial(-1);
        CHECK(g.focus_action == "tr", "right column stays right");
        g.focus_action.clear();
        g.focusSpatial(1);
        CHECK(g.focus_action == "tl", "unfocused: enters like Tab");
        g.buttons = { {0,0,10,10,"a"}, {20,0,30,10,"b"} };
        g.focus_action = "a";
        g.focusSpatial(1);
        CHECK(g.focus_action == "a", "single row: vertical is a no-op");
        printf("  keyboard focus ok (Tab order, wrap, spatial rows, "
               "stale-focus safety)\n");
    }

    if (gSelftestFailures == 0)
        printf("All selftests passed.\n");
    else
        fprintf(stderr, "selftest FAILED: %d check(s)\n", gSelftestFailures);
    return gSelftestFailures == 0 ? 0 : 1;
}

// ============================================================================
// Windows backend: Win32 window + GDI rasterizer + PlaySound audio.
// ============================================================================
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#undef RGB                    // windows.h macro; our RGB struct predates it

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#endif

static void makeAppDir() {
    CreateDirectoryA(appDataDir().c_str(), NULL);
}

// PlaySound from memory, asynchronously: safe here (unlike winsound) because
// every WAV buffer lives for the program's lifetime. One sound at a time,
// like the Python behavior.
class WinSound : public SoundIface {
public:
    std::map<std::string, std::vector<uint8_t>> cache;
    WinSound() { cache = soundSpecs(); }
    bool isEnabled() const override { return true; }
    void play(const std::string& name) override {
        if (muted) return;
        auto it = cache.find(name);
        if (it == cache.end()) return;
        PlaySoundA((LPCSTR)it->second.data(), NULL,
                   SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
    void toggleMute() override {
        muted = !muted;
        if (muted) PlaySoundA(NULL, NULL, SND_PURGE);
    }
};

static COLORREF cref(RGB c) {
    return (COLORREF)(c.r | (c.g << 8) | (c.b << 16));
}

static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

class GdiRenderer {
public:
    HDC memdc = NULL;
    HBITMAP bmp = NULL;
    std::map<int, HFONT> fonts;    // size*4 + bold*2 + italic -> font

    void ensure(HDC winDC) {
        if (memdc) return;
        memdc = CreateCompatibleDC(winDC);
        bmp = CreateCompatibleBitmap(winDC, WIN_W, WIN_H);
        SelectObject(memdc, bmp);
        SetBkMode(memdc, TRANSPARENT);
    }

    HFONT font(int size, bool bold, bool italic) {
        int key = size * 4 + (bold ? 2 : 0) + (italic ? 1 : 0);
        auto it = fonts.find(key);
        if (it != fonts.end()) return it->second;
        HFONT f = CreateFontW(-MulDiv(size, 96, 72), 0, 0, 0,
                              bold ? FW_BOLD : FW_NORMAL, italic, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH | FF_MODERN, L"Consolas");
        fonts[key] = f;
        return f;
    }

    void blitTerrain(const std::vector<uint32_t>& pix) {
        if (pix.empty()) return;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = FIELD_W;
        bi.bmiHeader.biHeight = -FIELD_H;     // negative = top-down rows
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(memdc, 0, 0, FIELD_W, FIELD_H, 0, 0, 0, FIELD_H,
                          pix.data(), &bi, DIB_RGB_COLORS);
    }

    void draw(const std::vector<Cmd>& scene,
              const std::vector<uint32_t>& terrainPix) {
        for (const Cmd& c : scene) {
            int x0 = (int)std::lround(c.x0), y0 = (int)std::lround(c.y0);
            int x1 = (int)std::lround(c.x1), y1 = (int)std::lround(c.y1);
            switch (c.type) {
            case Cmd::Terrain:
                blitTerrain(terrainPix);
                break;
            case Cmd::Line: {
                HPEN pen;
                if (c.roundCap || c.width > 1) {
                    LOGBRUSH lb{ BS_SOLID, cref(c.outline), 0 };
                    pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID
                                       | (c.roundCap ? PS_ENDCAP_ROUND
                                                     : PS_ENDCAP_FLAT),
                                       (DWORD)std::lround(c.width), &lb, 0, NULL);
                } else {
                    pen = CreatePen(PS_SOLID, 1, cref(c.outline));
                }
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
                HGDIOBJ oldFont = SelectObject(memdc,
                                               font(c.size, c.bold, c.italic));
                SIZE ext{};
                GetTextExtentPoint32W(memdc, w.c_str(), (int)w.size(), &ext);
                int tx = x0, ty = y0 - ext.cy / 2;
                if (c.anchor == Cmd::Center) tx = x0 - ext.cx / 2;
                else if (c.anchor == Cmd::E) tx = x0 - ext.cx;
                SetTextColor(memdc, cref(c.fill));
                TextOutW(memdc, tx, ty, w.c_str(), (int)w.size());
                SelectObject(memdc, oldFont);
                break;
            }
            }
        }
    }
};

struct App {
    PocketTanks* core = nullptr;
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
    case VK_SPACE: return Key::Space;
    case VK_ESCAPE: return Key::Escape;
    case 'A': return Key::A;
    case 'D': return Key::D;
    case 'Y': return Key::Y;
    case 'N': return Key::N;
    case '1': return Key::One;
    case '2': return Key::Two;
    case 'R': return Key::R;
    case 'M': return Key::M;
    case VK_OEM_4: return Key::BracketLeft;    // [
    case VK_OEM_6: return Key::BracketRight;   // ]
    default: return Key::None;
    }
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PocketTanks* core = gApp.core;
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
        if (core) gApp.renderer.draw(core->scene, core->terrainPix);
        BitBlt(dc, 0, 0, WIN_W, WIN_H, gApp.renderer.memdc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN: {
        // No repeat filtering: Tk delivered OS auto-repeat too (that's what
        // makes held angle/power keys keep adjusting).
        if (!core) return 0;
        Key k = mapVk(wp);
        if (k != Key::None) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            core->handleKey(k, shift);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (core) core->onClick((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_CLOSE: {
        if (core) {
            RECT r;
            if (GetWindowRect(hwnd, &r)) core->saveWinPos(r.left, r.top);
            else core->saveSettings();
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
    WinSound sound;
    PocketTanks core(true, &sound, /*persist=*/true);
    gApp.core = &core;

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MyPocketTanksWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));   // embedded .ico
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r{ 0, 0, WIN_W, WIN_H };
    AdjustWindowRect(&r, style, FALSE);
    int winW = r.right - r.left, winH = r.bottom - r.top;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int sx, sy;
    if (core.savedWinPos(sx, sy)) { x = sx; y = sy; }  // verbatim, like Tk

    HWND hwnd = CreateWindowW(L"MyPocketTanksWnd", L"MyPocketTanks", style,
                              x, y, winW, winH, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 1;
    core.requestClose = [hwnd]() { PostMessageW(hwnd, WM_CLOSE, 0, 0); };

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    timeBeginPeriod(1);
    SetTimer(hwnd, 1, FRAME_MS, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    timeEndPeriod(1);
    return (int)msg.wParam;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    for (int i = 1; i < __argc; i++) {
        if (strcmp(__argv[i], "--dumpconfig") == 0) {   // diagnostic aid
            HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
            if (out == NULL || out == INVALID_HANDLE_VALUE) {
                if (AttachConsole(ATTACH_PARENT_PROCESS)) {
                    FILE* f;
                    freopen_s(&f, "CONOUT$", "w", stdout);
                }
            }
            printf("configPath: %s\n", configPath().c_str());
            JVal cfg = loadConfig();
            std::string dump;
            jsonDump(cfg, dump, 0);
            printf("loaded: %s\n", dump.c_str());
            int x, y;
            PocketTanks probe(false, nullptr, false);
            probe.config = cfg;
            printf("savedWinPos: %s\n",
                   probe.savedWinPos(x, y)
                       ? (std::to_string(x) + "," + std::to_string(y)).c_str()
                       : "none");
            fflush(stdout);
            return 0;
        }
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
// macOS backend: Cocoa window + NSBezierPath rasterizer + afplay audio.
// This file is Objective-C++ here — build with `clang++ -x objective-c++`.
// ============================================================================
#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

extern char** environ;

static void makeAppDir() {
    mkdir(appDataDir().c_str(), 0755);
}

static std::string gSndDir;
static std::vector<std::string> gSndPaths;
static void cleanupSounds() {
    for (const std::string& p : gSndPaths) unlink(p.c_str());
    if (!gSndDir.empty()) rmdir(gSndDir.c_str());
}

class MacSound : public SoundIface {
public:
    std::map<std::string, std::string> files;
    bool ok = false;
    MacSound() {
        if (access("/usr/bin/afplay", X_OK) != 0) return;
        signal(SIGCHLD, SIG_IGN);
        char tmpl[] = "/tmp/mypockettanks-snd-XXXXXX";
        char* dir = mkdtemp(tmpl);
        if (!dir) return;
        gSndDir = dir;
        for (auto& kv : soundSpecs()) {
            std::string path = gSndDir + "/" + kv.first + ".wav";
            FILE* f = fopen(path.c_str(), "wb");
            if (!f) continue;
            fwrite(kv.second.data(), 1, kv.second.size(), f);
            fclose(f);
            files[kv.first] = path;
            gSndPaths.push_back(path);
        }
        atexit(cleanupSounds);
        ok = true;
        std::thread([]() {
            FILE* p = popen("/usr/bin/osascript -e "
                            "'output muted of (get volume settings)' "
                            "2>/dev/null", "r");
            if (!p) return;
            char buf[32] = { 0 };
            if (!fgets(buf, sizeof buf, p)) buf[0] = 0;
            pclose(p);
            if (strncmp(buf, "true", 4) == 0)
                fprintf(stderr, "MyPocketTanks: sound is on, but the macOS "
                        "output device is muted — unmute to hear anything "
                        "(F10, or: osascript -e 'set volume without output "
                        "muted')\n");
        }).detach();
    }
    bool isEnabled() const override { return ok; }
    void play(const std::string& name) override {
        if (!ok || muted) return;
        auto it = files.find(name);
        if (it == files.end()) return;
        pid_t pid;
        const char* argv[] = { "/usr/bin/afplay", it->second.c_str(), NULL };
        posix_spawn(&pid, "/usr/bin/afplay", NULL, NULL,
                    (char* const*)argv, environ);
    }
};

static NSColor* nsColor(RGB c) {
    return [NSColor colorWithSRGBRed:c.r / 255.0 green:c.g / 255.0
                                blue:c.b / 255.0 alpha:1.0];
}

static NSFont* sceneFont(int size, bool bold, bool italic) {
    const char* name = bold ? (italic ? "Menlo-BoldItalic" : "Menlo-Bold")
                            : (italic ? "Menlo-Italic" : "Menlo-Regular");
    NSFont* f = [NSFont fontWithName:[NSString stringWithUTF8String:name]
                                size:size];
    if (!f) f = [NSFont userFixedPitchFontOfSize:size];
    return f;
}

@interface TanksView : NSView {
@public
    PocketTanks* core;
    CGContextRef terrainCtx;
}
@end

@implementation TanksView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)drawRect:(NSRect)dirty {
    if (!core) return;
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    for (const Cmd& c : core->scene) {
        NSRect r = NSMakeRect(c.x0, c.y0, c.x1 - c.x0, c.y1 - c.y0);
        switch (c.type) {
        case Cmd::Terrain: {
            if (core->terrainPix.empty()) break;
            if (!terrainCtx) {
                CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
                terrainCtx = CGBitmapContextCreate(
                    core->terrainPix.data(), FIELD_W, FIELD_H, 8,
                    FIELD_W * 4, cs,
                    kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
                CGColorSpaceRelease(cs);
            }
            CGImageRef img = CGBitmapContextCreateImage(terrainCtx);
            CGContextSaveGState(ctx);
            // Double-flip inside the image rect so the top-down buffer
            // renders upright in this flipped view.
            CGContextTranslateCTM(ctx, 0, FIELD_H);
            CGContextScaleCTM(ctx, 1, -1);
            CGContextDrawImage(ctx, CGRectMake(0, 0, FIELD_W, FIELD_H), img);
            CGContextRestoreGState(ctx);
            CGImageRelease(img);
            break;
        }
        case Cmd::Line: {
            NSBezierPath* p = [NSBezierPath bezierPath];
            [p moveToPoint:NSMakePoint(c.x0, c.y0)];
            [p lineToPoint:NSMakePoint(c.x1, c.y1)];
            p.lineWidth = c.width;
            if (c.roundCap) p.lineCapStyle = NSLineCapStyleRound;
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
                NSFontAttributeName: sceneFont(c.size, c.bold, c.italic),
                NSForegroundColorAttributeName: nsColor(c.fill),
            };
            NSSize ext = [s sizeWithAttributes:attrs];
            double tx = c.x0, ty = c.y0 - ext.height / 2;
            if (c.anchor == Cmd::Center) tx = c.x0 - ext.width / 2;
            else if (c.anchor == Cmd::E) tx = c.x0 - ext.width;
            [s drawAtPoint:NSMakePoint(tx, ty) withAttributes:attrs];
            break;
        }
        }
    }
}

- (Key)mapEvent:(NSEvent*)e {
    NSString* chars = [e charactersIgnoringModifiers];
    if ([chars length] == 0) return Key::None;
    unichar ch = [chars characterAtIndex:0];
    switch (ch) {
    case NSUpArrowFunctionKey: return Key::Up;
    case NSDownArrowFunctionKey: return Key::Down;
    case NSLeftArrowFunctionKey: return Key::Left;
    case NSRightArrowFunctionKey: return Key::Right;
    case NSHomeFunctionKey: return Key::Home;
    case NSEndFunctionKey: return Key::End;
    case NSPageUpFunctionKey: return Key::PgUp;
    case NSPageDownFunctionKey: return Key::PgDn;
    case '\t': case 0x19: return Key::Tab;
    case '\r': case 0x03: return Key::Enter;
    case 27: return Key::Escape;
    case ' ': return Key::Space;
    case 'a': case 'A': return Key::A;
    case 'd': case 'D': return Key::D;
    case 'y': case 'Y': return Key::Y;
    case 'n': case 'N': return Key::N;
    case '1': return Key::One;
    case '2': return Key::Two;
    case 'r': case 'R': return Key::R;
    case 'm': case 'M': return Key::M;
    case '[': return Key::BracketLeft;
    case ']': return Key::BracketRight;
    default: return Key::None;
    }
}

- (void)keyDown:(NSEvent*)e {
    // Auto-repeats pass through on purpose — Tk delivered them too, which
    // is what makes held angle/power keys keep adjusting.
    if (!core) return;
    Key k = [self mapEvent:e];
    if (k != Key::None) {
        NSString* chars = [e charactersIgnoringModifiers];
        unichar ch = [chars length] ? [chars characterAtIndex:0] : 0;
        bool shift = ([e modifierFlags] & NSEventModifierFlagShift) != 0
                     || ch == 0x19;
        core->handleKey(k, shift);
    }
}

- (void)mouseDown:(NSEvent*)e {
    if (!core) return;
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    core->onClick(p.x, p.y);
}
@end

@interface TanksAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@public
    PocketTanks* core;
    NSWindow* window;
    NSTimer* timer;
}
@end

@implementation TanksAppDelegate
- (void)windowWillClose:(NSNotification*)n {
    if (core && window) {
        NSRect f = [window frame];
        NSRect screen = [[NSScreen mainScreen] frame];
        int x = (int)f.origin.x;
        int y = (int)(screen.size.height - (f.origin.y + f.size.height));
        core->saveWinPos(x, y);
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
        [appMenu addItemWithTitle:@"Quit MyPocketTanks"
                           action:@selector(terminate:) keyEquivalent:@"q"];
        [appItem setSubmenu:appMenu];
        [NSApp setMainMenu:bar];

        MacSound sound;
        PocketTanks core(true, &sound, /*persist=*/true);

        NSRect rect = NSMakeRect(0, 0, WIN_W, WIN_H);
        NSWindow* win = [[NSWindow alloc]
            initWithContentRect:rect
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [win setTitle:@"MyPocketTanks"];
        TanksView* view = [[TanksView alloc] initWithFrame:rect];
        view->core = &core;
        view->terrainCtx = NULL;
        [win setContentView:view];
        [win makeFirstResponder:view];

        TanksAppDelegate* delegate = [[TanksAppDelegate alloc] init];
        delegate->core = &core;
        delegate->window = win;
        [win setDelegate:delegate];
        [NSApp setDelegate:delegate];
        core.requestClose = [win]() { [win close]; };

        int sx, sy;
        if (core.savedWinPos(sx, sy)) {
            NSRect screen = [[NSScreen mainScreen] frame];
            [win setFrameTopLeftPoint:NSMakePoint(sx, screen.size.height - sy)];
        } else {
            [win center];
        }

        delegate->timer =
            [NSTimer scheduledTimerWithTimeInterval:FRAME_MS / 1000.0
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

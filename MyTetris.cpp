// MyTetris.cpp — a C++ port of MyTetris.py with identical functionality.
//
// Same guideline mechanics (SRS + wall kicks, 7-bag, DAS, lock delay,
// hold, ghost, next-3, T-spins, back-to-back), same menu/pause/gameover
// screens with the same keyboard-focus model, same synthesized sounds,
// and the same JSON persistence in %APPDATA%\MyTetris (macOS: ~/MyTetris)
// — the .py and the .exe read each other's high scores and config.
//
// No third-party libraries — the C++ analog of the repo's "pure stdlib"
// rule: the game core below is platform-free C++17, followed by a Win32 +
// GDI backend and a Cocoa backend (this one file compiles as
// Objective-C++ on macOS).
//
// Build (Windows, MSVC — or just run build_mytetris.ps1):
//   cl /nologo /EHsc /O2 /std:c++17 MyTetris.cpp ^
//      /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib winmm.lib msimg32.lib
//
// Build (macOS — or just run build_mytetris.command):
//   clang++ -x objective-c++ -std=c++17 -O2 MyTetris.cpp \
//           -framework Cocoa -o MyTetris
//
// Self-test (headless logic, same checks as MyTetris.py --selftest):
//   MyTetris.exe --selftest      (prints to the launching console)

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
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// Board / rendering constants (same values as MyTetris.py)
// ----------------------------------------------------------------------------
static const int COLS = 10, ROWS = 20;
static const int CELL = 30;
static const int PNEXT = 18;        // next-queue / hold preview cell
static const int FRAME_MS = 16;     // ~60 FPS fixed timestep

static const int DAS_DELAY = 150;
static const int DAS_REPEAT = 40;
static const int SOFT_DROP_INTERVAL = 30;
static const int MAX_LOCK_RESETS = 15;
static const int TOAST_FRAMES = 80; // how long a "TETRIS" / "T-SPIN" banner lingers

// The Tk version spreads the UI over a padded frame + side panel; here one
// client area holds everything at the same offsets: 14px padding, a 2px
// board border, then the side panel.
static const int PAD = 14;
static const int BORDER = 2;
static const int BOARD_X = PAD + BORDER;              // board pixels start here
static const int BOARD_Y = PAD + BORDER;
static const int BOARD_W = COLS * CELL;               // 300
static const int BOARD_H = ROWS * CELL;               // 600
static const int SIDE_X = PAD + BOARD_W + 2 * BORDER + PAD;   // side panel left
static const int SIDE_W = 132;
static const int CLIENT_W = SIDE_X + SIDE_W + PAD;
static const int CLIENT_H = PAD + BOARD_H + 2 * BORDER + PAD;

struct RGB { uint8_t r, g, b; };
static inline bool operator==(RGB a, RGB b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static RGB hexColor(const char* h) {            // "#rrggbb" -> RGB
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
static const RGB BG_CELL   = hexColor("#15151f");
static const RGB GRID_LINE = hexColor("#22222e");
static const RGB PANEL_CELL= hexColor("#15151f");
static const RGB TEXT_C    = hexColor("#e6e6ec");
static const RGB SUBTEXT   = hexColor("#9a9ab0");
static const RGB GOLD      = hexColor("#ffd91a");
static const RGB BTN_BG    = hexColor("#1d1d2c");
static const RGB BTN_EDGE  = hexColor("#3a3a52");
static const RGB FOCUS_C   = hexColor("#7ec8ff");   // keyboard-focus halo
static const RGB BTN_DIS_BG= hexColor("#15151d");
static const RGB BTN_DIS_FG= hexColor("#55556a");
static const RGB BOARD_EDGE= hexColor("#3a3a55");
static const RGB BOX_EDGE  = hexColor("#33334a");
static const RGB BLOCK_EDGE= hexColor("#0a0a0f");
static const RGB SEL_BG    = hexColor("#26263a");
static const RGB START_BG  = hexColor("#183822");
static const RGB START_FG  = hexColor("#7ee08a");
static const RGB WHITE     = hexColor("#ffffff");
static const RGB BLACK     = hexColor("#000000");

static const char PIECE_NAMES[8] = "IJLOSTZ";      // index 0..6
static const RGB PIECE_COLORS[7] = {
    hexColor("#19d3da"), hexColor("#3f63e0"), hexColor("#ff9f1a"),
    hexColor("#ffd91a"), hexColor("#2ecc55"), hexColor("#a64ddb"),
    hexColor("#ef4444"),
};

static RGB adjust(RGB c, double factor) {
    auto f = [factor](uint8_t v) {
        int x = (int)(v * factor);
        return (uint8_t)std::max(0, std::min(255, x));
    };
    return RGB{ f(c.r), f(c.g), f(c.b) };
}

// Difficulty presets (same knobs as the Python DIFFICULTIES dict).
struct Difficulty {
    const char* name;
    int start_level;
    double gravity_mult;
    int lock_delay;
    double score_mult;
    const char* blurb;
};
static const Difficulty DIFFICULTIES[3] = {
    { "Easy",   1, 1.6,  700, 1.0,  "Slower fall, gentle start" },
    { "Normal", 1, 1.0,  500, 1.0,  "Standard fall speed" },
    { "Hard",   5, 0.55, 350, 1.25, "Starts at level 5, x1.25" },
};
static const int N_DIFFICULTIES = 3;

// Player-adjustable gravity ramp (persisted; shared by every difficulty).
static const double SPEED_STEP_MIN = 0.20;
static const double SPEED_STEP_MAX = 1.00;
static const double SPEED_STEP_DEFAULT = 0.50;
static const double SPEED_STEP_INCREMENT = 0.05;

// ----------------------------------------------------------------------------
// Piece geometry: the base matrices rotated 4x into per-rotation cell lists,
// exactly like PIECE_CELLS in the Python version.
// ----------------------------------------------------------------------------
struct CellXY { int x, y; };
static std::vector<CellXY> PIECE_CELLS[7][4];

static void buildPieceCells() {
    static const char* BASE[7] = {          // rows top-to-bottom, '1' = filled
        "0000/1111/0000/0000",              // I  (4x4)
        "100/111/000",                      // J  (3x3)
        "001/111/000",                      // L
        "11/11",                            // O  (2x2)
        "011/110/000",                      // S
        "010/111/000",                      // T
        "110/011/000",                      // Z
    };
    for (int p = 0; p < 7; p++) {
        std::vector<std::vector<int>> m;
        std::vector<int> row;
        for (const char* s = BASE[p]; ; s++) {
            if (*s == '/' || *s == '\0') { m.push_back(row); row.clear();
                                           if (*s == '\0') break; }
            else row.push_back(*s - '0');
        }
        for (int r = 0; r < 4; r++) {
            std::vector<CellXY> cells;
            for (int y = 0; y < (int)m.size(); y++)
                for (int x = 0; x < (int)m[y].size(); x++)
                    if (m[y][x]) cells.push_back(CellXY{ x, y });
            PIECE_CELLS[p][r] = cells;
            int n = (int)m.size();          // rotate clockwise for next state
            std::vector<std::vector<int>> rot(n, std::vector<int>(n, 0));
            for (int i = 0; i < n; i++)
                for (int j = 0; j < n; j++)
                    rot[i][j] = m[n - 1 - j][i];
            m = rot;
        }
    }
}

// SRS wall-kick tables: 5 offsets per (from -> to) rotation transition.
struct Kick { int dx, dy; };
static const Kick* kicksFor(bool isI, int from, int to, int& count) {
    static const Kick JLSTZ[8][5] = {
        /*0->1*/ {{0,0},{-1,0},{-1,-1},{0,2},{-1,2}},
        /*1->0*/ {{0,0},{1,0},{1,1},{0,-2},{1,-2}},
        /*1->2*/ {{0,0},{1,0},{1,1},{0,-2},{1,-2}},
        /*2->1*/ {{0,0},{-1,0},{-1,-1},{0,2},{-1,2}},
        /*2->3*/ {{0,0},{1,0},{1,-1},{0,2},{1,2}},
        /*3->2*/ {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}},
        /*3->0*/ {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}},
        /*0->3*/ {{0,0},{1,0},{1,-1},{0,2},{1,2}},
    };
    static const Kick I[8][5] = {
        /*0->1*/ {{0,0},{-2,0},{1,0},{-2,1},{1,-2}},
        /*1->0*/ {{0,0},{2,0},{-1,0},{2,-1},{-1,2}},
        /*1->2*/ {{0,0},{-1,0},{2,0},{-1,-2},{2,1}},
        /*2->1*/ {{0,0},{1,0},{-2,0},{1,2},{-2,-1}},
        /*2->3*/ {{0,0},{2,0},{-1,0},{2,-1},{-1,2}},
        /*3->2*/ {{0,0},{-2,0},{1,0},{-2,1},{1,-2}},
        /*3->0*/ {{0,0},{1,0},{-2,0},{1,2},{-2,-1}},
        /*0->3*/ {{0,0},{-1,0},{2,0},{-1,-2},{2,1}},
    };
    static const int FROM[8] = {0,1,1,2,2,3,3,0}, TO[8] = {1,0,2,1,3,2,0,3};
    for (int i = 0; i < 8; i++)
        if (FROM[i] == from && TO[i] == to) {
            count = 5;
            return isI ? I[i] : JLSTZ[i];
        }
    count = 0;
    return nullptr;
}

// Front-corner pairs of the T's 3x3 box by rotation (0=A top-left, 1=B
// top-right, 2=C bottom-left, 3=D bottom-right). "Front" = pointing side.
static const int T_FRONT_CORNERS[4][2] = { {0,1}, {1,3}, {2,3}, {0,2} };

// Base points (before level/difficulty/B2B multipliers) for a lock.
enum class TSpin { None, Mini, Full };
static int clearBase(int cleared, TSpin tspin) {
    if (tspin == TSpin::Full) {
        switch (cleared) { case 0: return 400; case 1: return 800;
                           case 2: return 1200; case 3: return 1600; }
        return 0;
    }
    if (tspin == TSpin::Mini) {
        switch (cleared) { case 0: return 100; case 1: return 200;
                           case 2: return 400; }
        return 0;
    }
    switch (cleared) { case 1: return 100; case 2: return 300;
                       case 3: return 500; case 4: return 800; }
    return 0;
}

// ----------------------------------------------------------------------------
// Sound synthesis: identical WAV buffers to the Python _tone/_seq/_wav trio
// (22050 Hz, 16-bit mono, square/sine with a 4 ms attack and linear decay).
// Playback is per-platform; the specs are shared.
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
    m["blip"]      = wavBytes(tone(440, 30, 0.25));
    m["start"]     = wavBytes(seq({ tone(523, 60, 0.4), tone(784, 110, 0.4) }));
    m["rotate"]    = wavBytes(tone(600, 38, 0.3));
    m["hold"]      = wavBytes(tone(380, 45, 0.3));
    m["lock"]      = wavBytes(tone(200, 55, 0.35, false));
    m["harddrop"]  = wavBytes(tone(110, 70, 0.5, false));
    m["lineclear"] = wavBytes(seq({ tone(523, 55), tone(659, 55), tone(784, 70) }));
    m["tetris"]    = wavBytes(seq({ tone(523, 60, 0.5), tone(659, 60, 0.5),
                                    tone(784, 60, 0.5), tone(1047, 120, 0.5) }));
    m["tspin"]     = wavBytes(seq({ tone(880, 45, 0.45), tone(1175, 45, 0.45),
                                    tone(1568, 110, 0.45) }));
    m["levelup"]   = wavBytes(seq({ tone(659, 50), tone(880, 50), tone(1175, 100) }));
    m["gameover"]  = wavBytes(seq({ tone(440, 130, 0.4, false), tone(330, 130, 0.4, false),
                                    tone(247, 130, 0.4, false), tone(165, 220, 0.4, false) }));
    return m;
}

// The core talks to sound through this; each platform subclasses it, and the
// selftest uses the silent base class.
class SoundIface {
public:
    bool muted = false;
    virtual ~SoundIface() {}
    virtual bool isEnabled() const { return false; }
    virtual void play(const std::string&) {}
    virtual void toggleMute() { muted = !muted; }
};

// ----------------------------------------------------------------------------
// Minimal JSON (parse + dump) — just enough for highscores.json/config.json,
// defensive like the Python loaders: any parse problem yields "no data".
// ----------------------------------------------------------------------------
struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;   // preserves key order

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
        if (lit("true"))  { JVal v; v.type = JVal::Bool; v.b = true; return v; }
        if (lit("false")) { JVal v; v.type = JVal::Bool; v.b = false; return v; }
        if (lit("null"))  return JVal();
        // number
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
                case 'u': {                       // \uXXXX -> UTF-8 (BMP only)
                    if (end - p >= 5) {
                        unsigned v = 0;
                        for (int i = 1; i <= 4; i++) {
                            char c = p[i]; v <<= 4;
                            if (c >= '0' && c <= '9') v += c - '0';
                            else if (c >= 'a' && c <= 'f') v += c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') v += c - 'A' + 10;
                            else { ok = false; }
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
        if (p < end) p++; else ok = false;       // closing quote
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
        if (v.num == (long long)v.num &&
            std::fabs(v.num) < 1e15)            // integers stay integers
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
            JVal k = JVal::string(v.obj[i].first);
            jsonDump(k, out, 0);
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
// Persistence: the same files and shapes as the Python version —
// %APPDATA%\MyTetris\{highscores,config}.json (or ~/MyTetris/ without APPDATA).
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
    return dir + "\\MyTetris";
#else
    return dir + "/MyTetris";
#endif
}

static std::string pathJoin(const std::string& dir, const char* name) {
#ifdef _WIN32
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static void makeAppDir();                     // per-platform (defined below)
static bool writeFile(const std::string& path, const std::string& data) {
    makeAppDir();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << data;
    return f.good();
}

struct ScoreEntry { int score, lines, level; };

static std::map<std::string, std::vector<ScoreEntry>> loadScores() {
    std::map<std::string, std::vector<ScoreEntry>> out;
    for (auto& d : DIFFICULTIES) out[d.name] = {};
    std::string text;
    if (!readFile(pathJoin(appDataDir(), "highscores.json"), text)) return out;
    JParser parser(text);
    JVal data = parser.parse();
    if (!parser.ok || data.type != JVal::Obj) return out;
    for (auto& d : DIFFICULTIES) {
        const JVal* list = data.get(d.name);
        if (!list || list->type != JVal::Arr) continue;
        for (const JVal& e : list->arr) {
            if (e.type != JVal::Obj || !e.get("score")) continue;
            auto numOr0 = [&](const char* k) {
                const JVal* v = e.get(k);
                return (v && v->type == JVal::Num) ? (int)v->num : 0;
            };
            out[d.name].push_back(ScoreEntry{ numOr0("score"), numOr0("lines"),
                                              numOr0("level") });
            if (out[d.name].size() >= 10) break;
        }
    }
    return out;
}

static void saveScores(const std::map<std::string, std::vector<ScoreEntry>>& scores) {
    JVal root = JVal::object();
    for (auto& d : DIFFICULTIES) {
        JVal list = JVal::array();
        auto it = scores.find(d.name);
        if (it != scores.end())
            for (const ScoreEntry& e : it->second) {
                JVal o = JVal::object();
                o.set("score", JVal::number(e.score));
                o.set("lines", JVal::number(e.lines));
                o.set("level", JVal::number(e.level));
                list.arr.push_back(std::move(o));
            }
        root.set(d.name, std::move(list));
    }
    std::string out;
    jsonDump(root, out, 0);
    writeFile(pathJoin(appDataDir(), "highscores.json"), out);
}

static JVal loadConfig() {
    std::string text;
    if (!readFile(pathJoin(appDataDir(), "config.json"), text)) return JVal::object();
    JParser parser(text);
    JVal data = parser.parse();
    if (!parser.ok || data.type != JVal::Obj) return JVal::object();
    return data;
}

static void saveConfig(const JVal& config) {
    std::string out;
    jsonDump(config, out, 0);
    writeFile(pathJoin(appDataDir(), "config.json"), out);
}

// ----------------------------------------------------------------------------
// The scene: render() emits draw commands (the Tk canvas calls, reified) plus
// button hit boxes; the platform backends just rasterize the commands. That
// keeps the whole game — including every screen's layout — headlessly testable.
// ----------------------------------------------------------------------------
struct Cmd {
    enum Type { Line, Rect, FillAlpha, RoundRect, Text } type;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    RGB fill{}; bool hasFill = false;
    RGB outline{}; bool hasOutline = false;
    double width = 1;
    double alpha = 1;                         // FillAlpha
    double radius = 0;                        // RoundRect
    std::string text;                         // Text (UTF-8)
    int size = 11; bool bold = false;
    enum Anchor { Center, W, E } anchor = Center;
};

struct Button { double x0, y0, x1, y1; std::string action; };

enum class Key {
    None, Left, Right, Down, Up, X, Z, Ctrl, Space, C, Shift, P, M, R,
    Escape, Enter, Tab, Home, End, PgUp, PgDn,
    BracketLeft, BracketRight, Minus, Equal,
};

enum class GState { Menu, Playing, Paused, GameOver };

// ----------------------------------------------------------------------------
// The game core — a line-for-line port of the Python TetrisGame class, minus
// Tk: state machine, SRS, scoring, DAS/gravity, focus model, and rendering
// into the command scene above.
// ----------------------------------------------------------------------------
class TetrisCore {
public:
    SoundIface* sound;
    bool persist;

    std::map<std::string, std::vector<ScoreEntry>> scores;
    JVal config;
    double speed_step;
    int difficulty = 1;                        // index into DIFFICULTIES ("Normal")
    int board[ROWS][COLS];                     // -1 empty, else piece index
    std::vector<int> queue;
    std::vector<int> bag;
    int hold_piece = -1;
    int piece = -1;
    int rot = 0, px = 0, py = 0;
    int score = 0, lines = 0, level = 1;
    bool new_best = false;
    bool b2b = false;
    bool confirm_menu = false;
    bool can_hold = true;
    bool last_action_was_rotate = false;
    int last_kick_index = 0;
    std::string toast_text;
    int toast_frames = 0;
    std::set<Key> held;
    int last_dir = 0;
    int das_dir = 0;
    double das_charge = 0;
    bool das_active = false;
    double fall_charge = 0;
    double lock_counter = 0;
    int lock_resets = 0;
    bool gameover_handled = true;
    GState state = GState::Menu;

    std::vector<Cmd> scene;
    std::vector<Button> buttons;
    std::string focus_action;                  // "" = nothing focused
    std::function<void()> requestClose;        // menu-Escape quits the app

    std::mt19937 rng;

    TetrisCore(SoundIface* snd, bool persistFlag)
        : sound(snd), persist(persistFlag), rng(std::random_device{}()) {
        buildPieceCells();
        scores = loadScores();
        config = loadConfig();
        speed_step = loadSpeedStep();
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++)
                board[y][x] = -1;
    }

    const Difficulty& diff() const { return DIFFICULTIES[difficulty]; }

    // ----- piece geometry ----------------------------------------------------
    static const std::vector<CellXY>& cellsOf(int p, int rotation) {
        return PIECE_CELLS[p][((rotation % 4) + 4) % 4];
    }
    const std::vector<CellXY>& currentCells() const { return cellsOf(piece, rot); }

    bool validAt(int p, int rotation, int atX, int atY) const {
        for (const CellXY& c : cellsOf(p, rotation)) {
            int x = atX + c.x, y = atY + c.y;
            if (x < 0 || x >= COLS || y >= ROWS) return false;
            if (y >= 0 && board[y][x] != -1) return false;
        }
        return true;
    }
    bool grounded() const { return !validAt(piece, rot, px, py + 1); }

    // ----- spawning / 7-bag --------------------------------------------------
    void fillQueue() {
        while ((int)queue.size() < 5) {
            if (bag.empty()) {
                for (int i = 0; i < 7; i++) bag.push_back(i);
                std::shuffle(bag.begin(), bag.end(), rng);
            }
            queue.push_back(bag.back());
            bag.pop_back();
        }
    }

    void setCurrent(int p) {
        piece = p;
        rot = 0;
        px = (PIECE_NAMES[p] == 'O') ? 4 : 3;
        py = 0;
        fall_charge = 0;
        lock_counter = 0;
        lock_resets = 0;
        last_action_was_rotate = false;
        last_kick_index = 0;
        if (!validAt(piece, rot, px, py)) state = GState::GameOver;
    }

    void spawnFromQueue() {
        int p = queue.front();
        queue.erase(queue.begin());
        fillQueue();
        setCurrent(p);
    }

    // ----- locking, T-spin, line clears, scoring ------------------------------
    bool cornerFilled(int x, int y) const {
        if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return true;
        return board[y][x] != -1;
    }

    TSpin detectTspin() const {
        if (PIECE_NAMES[piece] != 'T' || !last_action_was_rotate) return TSpin::None;
        bool corners[4] = {
            cornerFilled(px + 0, py + 0),      // A top-left
            cornerFilled(px + 2, py + 0),      // B top-right
            cornerFilled(px + 0, py + 2),      // C bottom-left
            cornerFilled(px + 2, py + 2),      // D bottom-right
        };
        int filled = corners[0] + corners[1] + corners[2] + corners[3];
        if (filled < 3) return TSpin::None;
        bool full = corners[T_FRONT_CORNERS[rot][0]] && corners[T_FRONT_CORNERS[rot][1]];
        if (last_kick_index >= 4) full = true; // the big "TST"-style kick is always full
        return full ? TSpin::Full : TSpin::Mini;
    }

    void lockNow(const char* soundName) {
        sound->play(soundName);
        lockPiece();
    }

    void lockPiece() {
        TSpin tspin = detectTspin();
        bool topout = false;
        for (const CellXY& c : currentCells()) {
            int x = px + c.x, y = py + c.y;
            if (y < 0) { topout = true; continue; }
            if (x >= 0 && x < COLS && y < ROWS) board[y][x] = piece;
        }
        int cleared = removeFullRows();
        applyScore(cleared, tspin);
        can_hold = true;
        if (topout) { state = GState::GameOver; return; }
        spawnFromQueue();
    }

    int removeFullRows() {
        std::vector<std::array<int, COLS>> kept;
        for (int y = 0; y < ROWS; y++) {
            bool hasHole = false;
            for (int x = 0; x < COLS; x++)
                if (board[y][x] == -1) { hasHole = true; break; }
            if (hasHole) {
                std::array<int, COLS> row;
                for (int x = 0; x < COLS; x++) row[x] = board[y][x];
                kept.push_back(row);
            }
        }
        int cleared = ROWS - (int)kept.size();
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++) {
                int src = y - cleared;
                board[y][x] = (src >= 0) ? kept[src][x] : -1;
            }
        return cleared;
    }

    void applyScore(int cleared, TSpin tspin) {
        int base = clearBase(cleared, tspin);
        double b2bBonus = 1.0;
        bool b2bApplied = false;
        if (cleared > 0) {
            bool difficult = (cleared == 4) || (tspin != TSpin::None);
            if (difficult) {
                if (b2b) { b2bBonus = 1.5; b2bApplied = true; }
                b2b = true;
            } else b2b = false;
        }
        score += (int)(base * level * diff().score_mult * b2bBonus);

        bool leveled = false;
        if (cleared) {
            lines += cleared;
            int oldLevel = level;
            level = diff().start_level + lines / 10;
            leveled = level > oldLevel;
        }

        std::string label = clearLabel(cleared, tspin, b2bApplied);
        if (!label.empty()) {
            toast_text = label;
            toast_frames = TOAST_FRAMES;
        }

        if (leveled) sound->play("levelup");
        else if (tspin != TSpin::None && cleared > 0) sound->play("tetris");
        else if (tspin != TSpin::None) sound->play("tspin");
        else if (cleared >= 4) sound->play("tetris");
        else if (cleared > 0) sound->play("lineclear");
        else sound->play("lock");
    }

    static std::string clearLabel(int cleared, TSpin tspin, bool b2bApplied) {
        static const char* names[5] = { "", "SINGLE", "DOUBLE", "TRIPLE", "TETRIS" };
        std::string text;
        if (tspin == TSpin::Full)
            text = std::string("T-SPIN") + (cleared ? std::string(" ") + names[cleared] : "");
        else if (tspin == TSpin::Mini)
            text = std::string("T-SPIN MINI") + (cleared ? std::string(" ") + names[cleared] : "");
        else if (cleared == 4)
            text = "TETRIS";
        if (!text.empty() && b2bApplied) text = "B2B " + text;
        return text;
    }

    // ----- movement / rotation -----------------------------------------------
    bool tryMove(int dx, int dy) {
        if (!validAt(piece, rot, px + dx, py + dy)) return false;
        px += dx;
        py += dy;
        last_action_was_rotate = false;
        if (dy > 0) {
            lock_counter = 0;
            lock_resets = 0;
        } else if (dx != 0 && grounded()) {
            resetLockOnAction();
        }
        return true;
    }

    bool rotate(int direction) {
        if (state != GState::Playing || PIECE_NAMES[piece] == 'O') return false;
        int newRot = ((rot + direction) % 4 + 4) % 4;
        int count = 0;
        const Kick* table = kicksFor(PIECE_NAMES[piece] == 'I', rot, newRot, count);
        for (int i = 0; i < count; i++) {
            if (validAt(piece, newRot, px + table[i].dx, py + table[i].dy)) {
                rot = newRot;
                px += table[i].dx;
                py += table[i].dy;
                last_action_was_rotate = true;
                last_kick_index = i;
                if (grounded()) resetLockOnAction();
                sound->play("rotate");
                return true;
            }
        }
        return false;
    }

    void resetLockOnAction() {
        if (lock_resets < MAX_LOCK_RESETS) {
            lock_counter = 0;
            lock_resets++;
        }
    }

    void hardDrop() {
        int dist = 0;
        while (tryMove(0, 1)) dist++;
        score += 2 * dist;
        lockNow("harddrop");
    }

    void hold() {
        if (!can_hold || state != GState::Playing) return;
        int current = piece;
        if (hold_piece == -1) {
            hold_piece = current;
            spawnFromQueue();
        } else {
            int incoming = hold_piece;
            hold_piece = current;
            setCurrent(incoming);
        }
        can_hold = false;
        sound->play("hold");
    }

    // ----- per-frame update ----------------------------------------------------
    double gravityInterval() const {
        // Classic guideline gravity: time-per-cell = base^(L-1) seconds, with
        // base = 0.8 - (L-1)*0.007; speed_step stretches the ramp (see the .py).
        double eff = 1 + (level - 1) * speed_step;
        double base = 0.8 - (eff - 1) * 0.007;
        if (base <= 0) base = 0.01;
        double ms = std::pow(base, eff - 1) * 1000 * diff().gravity_mult;
        return std::max(ms, (double)FRAME_MS);
    }

    void update(double dt) {
        updateHorizontal(dt);
        updateGravity(dt);
        if (state != GState::Playing) return;
        if (grounded()) {
            lock_counter += dt;
            if (lock_counter >= diff().lock_delay) lockNow("lock");
        } else {
            lock_counter = 0;
        }
    }

    void updateHorizontal(double dt) {
        bool left = held.count(Key::Left) > 0, right = held.count(Key::Right) > 0;
        int direction;
        if (left && right) direction = last_dir;
        else if (left) direction = -1;
        else if (right) direction = 1;
        else direction = 0;

        if (direction == 0) {
            das_dir = 0;
            das_charge = 0;
            das_active = false;
            return;
        }
        if (direction != das_dir) {
            das_dir = direction;
            das_charge = 0;
            das_active = false;
            return;
        }
        das_charge += dt;
        if (!das_active) {
            if (das_charge >= DAS_DELAY) {
                das_active = true;
                das_charge = 0;
                tryMove(direction, 0);
            }
        } else {
            while (das_charge >= DAS_REPEAT) {
                das_charge -= DAS_REPEAT;
                if (!tryMove(direction, 0)) break;
            }
        }
    }

    void updateGravity(double dt) {
        bool soft = held.count(Key::Down) > 0;
        double interval = std::max(soft ? (double)SOFT_DROP_INTERVAL
                                        : gravityInterval(), 1.0);
        fall_charge += dt;
        while (fall_charge >= interval) {
            fall_charge -= interval;
            if (tryMove(0, 1)) {
                if (soft) score += 1;
            } else {
                fall_charge = 0;
                break;
            }
        }
    }

    void tick() {
        if (state == GState::Playing) {
            update(FRAME_MS);
            if (toast_frames > 0) toast_frames--;
        }
        if (state == GState::GameOver && !gameover_handled) {
            gameover_handled = true;
            sound->play("gameover");
            recordScore();
        }
        render();
    }

    // ----- scores ---------------------------------------------------------------
    int best(int diffIndex) const {
        auto it = scores.find(DIFFICULTIES[diffIndex].name);
        if (it == scores.end() || it->second.empty()) return 0;
        return it->second[0].score;
    }

    void recordScore() {
        auto& entries = scores[diff().name];
        int oldTop = entries.empty() ? 0 : entries[0].score;
        entries.push_back(ScoreEntry{ score, lines, level });
        std::stable_sort(entries.begin(), entries.end(),
                         [](const ScoreEntry& a, const ScoreEntry& b) {
                             return a.score > b.score;
                         });
        if (entries.size() > 10) entries.resize(10);
        // Same semantics as "entries[0] is entry" after Python's stable sort:
        // the new entry leads only when it strictly beats the old top.
        new_best = score > 0 && score > oldTop;
        if (persist) saveScores(scores);
    }

    // ----- window position / speed-ramp persistence ------------------------------
    bool savedWindowPos(int& x, int& y) const {
        const JVal* win = config.get("window");
        if (!win || win->type != JVal::Obj) return false;
        const JVal* jx = win->get("x");
        const JVal* jy = win->get("y");
        if (!jx || jx->type != JVal::Num || !jy || jy->type != JVal::Num) return false;
        x = (int)jx->num;
        y = (int)jy->num;
        return true;
    }

    void saveWindowPos(int x, int y) {
        if (!persist) return;
        JVal win = JVal::object();
        win.set("x", JVal::number(x));
        win.set("y", JVal::number(y));
        config.set("window", std::move(win));
        saveConfig(config);
    }

    double loadSpeedStep() const {
        const JVal* v = config.get("speed_step");
        if (!v || v->type != JVal::Num) return SPEED_STEP_DEFAULT;
        double r = std::round(v->num * 100) / 100;
        return std::min(SPEED_STEP_MAX, std::max(SPEED_STEP_MIN, r));
    }

    void saveSpeedStep() {
        if (!persist) return;
        config.set("speed_step", JVal::number(speed_step));
        saveConfig(config);
    }

    void onClose() {
        if (requestClose) requestClose();
    }

    // ----- buttons & keyboard focus (same model as MyPocketTanks) -----------------
    void emitRoundRect(double x0, double y0, double x1, double y1, double radius,
                       bool hasFill, RGB fill, RGB outline, double width) {
        Cmd c;
        c.type = Cmd::RoundRect;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
        c.radius = std::min(radius, std::min((x1 - x0) / 2, (y1 - y0) / 2));
        c.hasFill = hasFill; c.fill = fill;
        c.hasOutline = true; c.outline = outline; c.width = width;
        scene.push_back(c);
    }

    void emitText(double x, double y, const std::string& s, RGB color, int size,
                  bool bold, Cmd::Anchor anchor = Cmd::Center) {
        Cmd c;
        c.type = Cmd::Text;
        c.x0 = x; c.y0 = y;
        c.text = s; c.fill = color; c.hasFill = true;
        c.size = size; c.bold = bold; c.anchor = anchor;
        scene.push_back(c);
    }

    void emitLine(double x0, double y0, double x1, double y1, RGB color,
                  double width = 1) {
        Cmd c;
        c.type = Cmd::Line;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
        c.outline = color; c.hasOutline = true; c.width = width;
        scene.push_back(c);
    }

    void emitRect(double x0, double y0, double x1, double y1, bool hasFill,
                  RGB fill, bool hasOutline, RGB outline, double width = 1) {
        Cmd c;
        c.type = Cmd::Rect;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
        c.hasFill = hasFill; c.fill = fill;
        c.hasOutline = hasOutline; c.outline = outline; c.width = width;
        scene.push_back(c);
    }

    void emitFillAlpha(double x0, double y0, double x1, double y1, RGB color,
                       double alpha) {
        Cmd c;
        c.type = Cmd::FillAlpha;
        c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
        c.fill = color; c.hasFill = true; c.alpha = alpha;
        scene.push_back(c);
    }

    void button(double x0, double y0, double x1, double y1,
                const std::string& label, const std::string& action,
                bool enabled = true, RGB fill = BTN_BG, RGB fg = TEXT_C,
                int size = 11) {
        emitRoundRect(x0, y0, x1, y1, 10, true, enabled ? fill : BTN_DIS_BG,
                      BTN_EDGE, 1);
        emitText((x0 + x1) / 2, (y0 + y1) / 2, label,
                 enabled ? fg : BTN_DIS_FG, size, true);
        if (enabled) {
            buttons.push_back(Button{ x0, y0, x1, y1, action });
            if (action == focus_action) focusRing(x0, y0, x1, y1);
        }
    }

    void focusRing(double x0, double y0, double x1, double y1, double radius = 12) {
        emitRoundRect(x0 - 3, y0 - 3, x1 + 3, y1 + 3, radius, false, RGB{},
                      FOCUS_C, 2);
    }

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
            focus_action = *std::min_element(ahead.begin(), ahead.end(), lessCand)->action;
        } else if (!behind.empty()) {          // screen edge: wrap to farthest row
            double farthest = 0;               // ("far" is a windows.h macro)
            for (const Cand& c : behind) farthest = std::max(farthest, c.d);
            const Cand* pick = nullptr;
            for (const Cand& c : behind)
                if (c.d > farthest - 0.5 && (!pick || c.dx < pick->dx)) pick = &c;
            if (pick) focus_action = *pick->action;
        }
    }

    bool focusActivate() {
        if (focus_action.empty()) return false;
        for (const Button& b : buttons)
            if (b.action == focus_action) { doAction(b.action); return true; }
        return false;
    }

    void setDifficulty(int index) {
        if (index != difficulty) {
            difficulty = index;
            sound->play("blip");
        }
    }

    void doAction(const std::string& action) {
        if (action.rfind("diff:", 0) == 0) {
            for (int i = 0; i < N_DIFFICULTIES; i++)
                if (action.substr(5) == DIFFICULTIES[i].name) setDifficulty(i);
        } else if (action == "speed-") {
            adjustSpeedStep(-SPEED_STEP_INCREMENT);
        } else if (action == "speed+") {
            adjustSpeedStep(+SPEED_STEP_INCREMENT);
        } else if (action == "start" || action == "retry") {
            startGame(difficulty);
        } else if (action == "resume") {
            state = GState::Playing;
            focus_action.clear();
        } else if (action == "pausemenu") {
            confirm_menu = true;               // ask first, like Esc
            focus_action.clear();
        } else if (action == "menu") {
            state = GState::Menu;
            focus_action.clear();
        } else if (action == "confirmY") {
            confirm_menu = false;
            state = GState::Menu;
            focus_action.clear();
        } else if (action == "confirmN") {
            confirm_menu = false;
            state = GState::Paused;
            focus_action.clear();
        }
    }

    void onClick(double x, double y) {
        for (const Button& b : buttons)
            if (b.x0 <= x && x <= b.x1 && b.y0 <= y && y <= b.y1) {
                doAction(b.action);
                return;
            }
    }

    // ----- input -------------------------------------------------------------
    // keyDown/keyUp mirror on_key_press/on_key_release (held-set + repeat
    // suppression); handlePress is the Python _handle_press and returns true
    // where the Tk handler returned "break".
    bool keyDown(Key key, bool shift) {
        if (held.count(key)) return key == Key::Tab;   // auto-repeat while held
        held.insert(key);
        return handlePress(key, shift);
    }

    void keyUp(Key key) { held.erase(key); }

    bool handlePress(Key key, bool shift = false) {
        if (key == Key::M) { sound->toggleMute(); return false; }
        // Keyboard focus, ahead of every state branch (see the .py comments).
        if (key == Key::Tab) {
            focusMove(shift ? -1 : 1);
            return true;
        }
        if (key == Key::Enter && focusActivate()) return true;
        if (state == GState::Menu || state == GState::Paused ||
            state == GState::GameOver) {
            if (key == Key::Left || key == Key::Home) { focusMove(-1); return true; }
            if (key == Key::Right || key == Key::End) { focusMove(1); return true; }
            if (key == Key::Up || key == Key::PgUp) { focusSpatial(-1); return true; }
            if (key == Key::Down || key == Key::PgDn) { focusSpatial(1); return true; }
        }
        if (state == GState::Menu) {
            menuKey(key);
            return false;
        }
        if (state == GState::GameOver) {
            if (key == Key::Enter) {
                state = GState::Menu;
                focus_action.clear();
            } else if (key == Key::R) {
                startGame(difficulty);
            }
            return false;
        }
        // playing / paused (with optional confirm-menu modal)
        if (confirm_menu) {
            if (key == Key::Enter) {           // Y is handled by the platform
                confirm_menu = false;          // via charKey(); Enter = yes
                state = GState::Menu;
                focus_action.clear();
            } else if (key == Key::Escape) {
                confirm_menu = false;
                state = GState::Paused;
                focus_action.clear();
            }
            return false;
        }
        if (key == Key::P) {
            if (state == GState::Playing) state = GState::Paused;
            else if (state == GState::Paused) state = GState::Playing;
            focus_action.clear();
            return false;
        }
        if (key == Key::Escape) {
            confirm_menu = true;               // ask before abandoning the game
            state = GState::Paused;
            focus_action.clear();
            return false;
        }
        if (key == Key::R) {
            startGame(difficulty);
            return false;
        }
        if (state != GState::Playing) return false;
        if (key == Key::Left) {
            last_dir = -1;
            tryMove(-1, 0);
        } else if (key == Key::Right) {
            last_dir = 1;
            tryMove(1, 0);
        } else if (key == Key::Down) {
            if (tryMove(0, 1)) score += 1;
        } else if (key == Key::Up || key == Key::X) {
            rotate(1);
        } else if (key == Key::Z || key == Key::Ctrl) {
            rotate(-1);
        } else if (key == Key::Space) {
            hardDrop();
        } else if (key == Key::C || key == Key::Shift) {
            hold();
        }
        return false;
    }

    // Y/N inside the confirm modal arrive as plain characters (like Tk's
    // keysyms "y"/"n"); the platform layers forward them here.
    void charKey(char c) {
        if (!confirm_menu) return;
        if (c == 'y' || c == 'Y') {
            confirm_menu = false;
            state = GState::Menu;
            focus_action.clear();
        } else if (c == 'n' || c == 'N') {
            confirm_menu = false;
            state = GState::Paused;
            focus_action.clear();
        }
    }

    void menuKey(Key key) {
        if (key == Key::BracketLeft || key == Key::Minus)
            adjustSpeedStep(-SPEED_STEP_INCREMENT);      // gentler / longer
        else if (key == Key::BracketRight || key == Key::Equal)
            adjustSpeedStep(+SPEED_STEP_INCREMENT);      // steeper / classic
        else if (key == Key::Enter || key == Key::Space)
            startGame(difficulty);
        else if (key == Key::Escape)
            onClose();
    }

    void adjustSpeedStep(double delta) {
        double next = std::round(std::min(SPEED_STEP_MAX,
                                          std::max(SPEED_STEP_MIN,
                                                   speed_step + delta)) * 100) / 100;
        if (next == speed_step) return;        // already at a limit
        speed_step = next;
        sound->play("blip");
        saveSpeedStep();                       // persist the choice immediately
    }

    // ----- lifecycle -----------------------------------------------------------
    void startGame(int diffIndex) {
        difficulty = diffIndex;
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++)
                board[y][x] = -1;
        score = 0;
        lines = 0;
        level = diff().start_level;
        hold_piece = -1;
        can_hold = true;
        bag.clear();
        queue.clear();
        fillQueue();
        held.clear();
        last_dir = 0;
        das_dir = 0;
        das_charge = 0;
        das_active = false;
        fall_charge = 0;
        lock_counter = 0;
        lock_resets = 0;
        last_action_was_rotate = false;
        last_kick_index = 0;
        b2b = false;
        confirm_menu = false;
        focus_action.clear();
        new_best = false;
        toast_text.clear();
        toast_frames = 0;
        gameover_handled = false;
        state = GState::Playing;
        spawnFromQueue();
        sound->play("start");
    }

    // ----- rendering -------------------------------------------------------------
    void drawBlock(double bx, double by, double size, RGB color) {
        emitRect(bx + 0.5, by + 0.5, bx + size - 0.5, by + size - 0.5,
                 true, color, true, BLOCK_EDGE);
        RGB hi = adjust(color, 1.4);
        emitLine(bx + 1.5, by + 1.5, bx + size - 2, by + 1.5, hi);
        emitLine(bx + 1.5, by + 1.5, bx + 1.5, by + size - 2, hi);
    }

    void drawGhost(double bx, double by, double size, RGB color) {
        emitFillAlpha(bx + 1, by + 1, bx + size - 1, by + size - 1, color, 0.25);
        emitRect(bx + 1.5, by + 1.5, bx + size - 1.5, by + size - 1.5,
                 false, RGB{}, true, adjust(color, 1.2), 2);
    }

    void render() {
        scene.clear();
        buttons.clear();
        // Window background + the board's 2px border (the Tk root/canvas chrome).
        emitRect(0, 0, CLIENT_W, CLIENT_H, true, BG, false, RGB{});
        emitRect(PAD, PAD, PAD + BOARD_W + 2 * BORDER, PAD + BOARD_H + 2 * BORDER,
                 true, BG_CELL, true, BOARD_EDGE, 2);
        if (state == GState::Menu) {
            renderMenu();
            renderHud();
            return;
        }
        const double ox = BOARD_X, oy = BOARD_Y;
        for (int x = 0; x <= COLS; x++)
            emitLine(ox + x * CELL, oy, ox + x * CELL, oy + ROWS * CELL, GRID_LINE);
        for (int y = 0; y <= ROWS; y++)
            emitLine(ox, oy + y * CELL, ox + COLS * CELL, oy + y * CELL, GRID_LINE);
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++)
                if (board[y][x] != -1)
                    drawBlock(ox + x * CELL, oy + y * CELL, CELL,
                              PIECE_COLORS[board[y][x]]);
        if (state == GState::Playing || state == GState::Paused) {
            RGB color = PIECE_COLORS[piece];
            int ghostY = py;
            while (validAt(piece, rot, px, ghostY + 1)) ghostY++;
            for (const CellXY& c : currentCells())
                if (ghostY + c.y >= 0)
                    drawGhost(ox + (px + c.x) * CELL, oy + (ghostY + c.y) * CELL,
                              CELL, color);
            for (const CellXY& c : currentCells())
                if (py + c.y >= 0)
                    drawBlock(ox + (px + c.x) * CELL, oy + (py + c.y) * CELL,
                              CELL, color);
            if (toast_frames > 0)
                emitText(ox + COLS * CELL / 2.0, oy + 3 * CELL, toast_text,
                         GOLD, 16, true);
        }
        double w = COLS * CELL, h = ROWS * CELL;
        if (state == GState::Paused) {
            if (confirm_menu) {
                drawOverlay({ { "RETURN TO MENU?", 22, WHITE },
                              { "This game will be lost", 12, SUBTEXT } });
                button(ox + w / 2 - 140, oy + h / 2 + 48, ox + w / 2 - 10,
                       oy + h / 2 + 84, "YES (Y)", "confirmY");
                button(ox + w / 2 + 10, oy + h / 2 + 48, ox + w / 2 + 140,
                       oy + h / 2 + 84, "NO (N)", "confirmN");
            } else {
                drawOverlay({ { "PAUSED", 26, WHITE } });
                const char* labels[3] = { "RESUME (P)", "RETRY (R)", "MENU (Esc)" };
                const char* actions[3] = { "resume", "retry", "pausemenu" };
                for (int i = 0; i < 3; i++) {
                    double y0 = oy + h / 2 + 28 + i * 44;
                    button(ox + w / 2 - 70, y0, ox + w / 2 + 70, y0 + 34,
                           labels[i], actions[i]);
                }
            }
        } else if (state == GState::GameOver) {
            std::vector<OverlayLine> linesv;
            linesv.push_back({ "GAME OVER", 26, WHITE });
            if (new_best)
                linesv.push_back({ "\xE2\x98\x85 NEW HIGH SCORE \xE2\x98\x85", 14, GOLD });
            linesv.push_back({ "Score  " + std::to_string(score), 14, TEXT_C });
            linesv.push_back({ "Best  " + std::to_string(best(difficulty)), 12, SUBTEXT });
            drawOverlay(linesv);
            button(ox + w / 2 - 140, oy + h / 2 + 66, ox + w / 2 - 10,
                   oy + h / 2 + 102, "RETRY (R)", "retry");
            button(ox + w / 2 + 10, oy + h / 2 + 66, ox + w / 2 + 140,
                   oy + h / 2 + 102, "MENU", "menu");
        }
        if (sound->muted || !sound->isEnabled()) {
            const char* label = sound->isEnabled() ? "MUTED" : "NO SOUND";
            emitText(BOARD_X + COLS * CELL - 6, BOARD_Y + 10, label, SUBTEXT, 8,
                     false, Cmd::E);
        }
        renderHud();
    }

    struct OverlayLine { std::string text; int size; RGB color; };

    void drawOverlay(const std::vector<OverlayLine>& linesv) {
        double w = COLS * CELL, h = ROWS * CELL;
        emitFillAlpha(BOARD_X, BOARD_Y, BOARD_X + w, BOARD_Y + h, BLACK, 0.5);
        double gap = 14;
        double total = gap * ((int)linesv.size() - 1);
        for (const auto& l : linesv) total += l.size;
        double y = BOARD_Y + h / 2 - total / 2;
        for (const auto& l : linesv) {
            y += l.size / 2.0;
            emitText(BOARD_X + w / 2, y, l.text, l.color, l.size, true);
            y += l.size / 2.0 + gap;
        }
    }

    void renderMenu() {
        const double ox = BOARD_X, oy = BOARD_Y;
        double w = COLS * CELL, h = ROWS * CELL;
        emitRect(ox, oy, ox + w, oy + h, true, BG_CELL, false, RGB{});
        emitText(ox + w / 2, oy + 66, "MYTETRIS", WHITE, 34, true);
        emitText(ox + w / 2, oy + 100, "a classic clone", SUBTEXT, 11, false);
        emitText(ox + w / 2, oy + 138, "DIFFICULTY", SUBTEXT, 12, true);
        for (int i = 0; i < N_DIFFICULTIES; i++) {
            bool sel = difficulty == i;
            double y0 = oy + 154 + i * 38;
            std::string name = DIFFICULTIES[i].name;
            std::string upper = name;
            for (char& c : upper) c = (char)toupper((unsigned char)c);
            button(ox + 30, y0, ox + w - 30, y0 + 30, upper, "diff:" + name,
                   true, sel ? SEL_BG : BTN_BG, sel ? GOLD : TEXT_C);
        }
        double y = oy + 154 + N_DIFFICULTIES * 38 + 8;
        emitText(ox + w / 2, y, diff().blurb, SUBTEXT, 9, false);
        y += 30;
        char speedBuf[32];
        snprintf(speedBuf, sizeof speedBuf, "SPEED  %.2f", speed_step);
        button(ox + 52, y - 15, ox + 88, y + 15, "-", "speed-",
               speed_step > SPEED_STEP_MIN);
        emitText(ox + w / 2, y, speedBuf, PIECE_COLORS[0], 13, true);
        button(ox + w - 88, y - 15, ox + w - 52, y + 15, "+", "speed+",
               speed_step < SPEED_STEP_MAX);
        y += 34;
        emitText(ox + w / 2, y, "BEST  " + std::to_string(best(difficulty)),
                 GOLD, 16, true);
        y += 30;
        emitText(ox + w / 2, y, "TOP SCORES", SUBTEXT, 11, true);
        y += 22;
        const auto& entries = scores[diff().name];
        if (!entries.empty()) {
            for (int i = 0; i < (int)entries.size() && i < 5; i++) {
                char buf[64];
                snprintf(buf, sizeof buf, "%d. %6d  Lv%d  %dL", i + 1,
                         entries[i].score, entries[i].level, entries[i].lines);
                emitText(ox + w / 2, y, buf, TEXT_C, 11, false);
                y += 20;
            }
        } else {
            emitText(ox + w / 2, y, "\xE2\x80\x94 none yet \xE2\x80\x94",
                     SUBTEXT, 10, false);
        }
        button(ox + 60, oy + h - 120, ox + w - 60, oy + h - 78, "S T A R T",
               "start", true, START_BG, START_FG, 16);
        emitText(ox + w / 2, oy + h - 56, "Tab / arrows navigate    Enter presses",
                 SUBTEXT, 10, false);
        emitText(ox + w / 2, oy + h - 38, "[ ] Speed    M Mute    Esc Quit",
                 SUBTEXT, 10, false);
        if (sound->muted || !sound->isEnabled()) {
            const char* label = sound->isEnabled() ? "MUTED" : "NO SOUND";
            emitText(ox + w - 6, oy + 12, label, SUBTEXT, 8, false, Cmd::E);
        }
    }

    void renderHud() {
        // Side panel: NEXT queue, HOLD, and the stat rows the Tk version keeps
        // in Labels — here they're drawn like everything else.
        double y = PAD;
        emitText(SIDE_X, y + 9, "NEXT", SUBTEXT, 12, true, Cmd::W);
        y += 22;
        double nx = SIDE_X + 1, ny = y + 1;                    // 1px border inset
        emitRect(SIDE_X, y, SIDE_X + 4 * PNEXT + 2, y + 9 * PNEXT + 2,
                 true, PANEL_CELL, true, BOX_EDGE, 1);
        for (int i = 0; i < 3 && i < (int)queue.size(); i++)
            blitPiece(queue[i], nx, ny + i * 3 * PNEXT, PNEXT);
        y += 9 * PNEXT + 2 + 8;
        emitText(SIDE_X, y + 9, "HOLD", SUBTEXT, 12, true, Cmd::W);
        y += 22;
        emitRect(SIDE_X, y, SIDE_X + 4 * PNEXT + 2, y + 3 * PNEXT + 2,
                 true, PANEL_CELL, true, BOX_EDGE, 1);
        if (hold_piece != -1)
            blitPiece(hold_piece, SIDE_X + 1, y + 1, PNEXT);
        y += 3 * PNEXT + 2 + 8;

        struct Row { const char* name; std::string value; };
        Row rows[5] = {
            { "SCORE", std::to_string(score) },
            { "BEST",  std::to_string(best(difficulty)) },
            { "LEVEL", std::to_string(level) },
            { "LINES", std::to_string(lines) },
            { "DIFF",  diff().name },
        };
        for (const Row& r : rows) {
            emitText(SIDE_X, y + 12, r.name, SUBTEXT, 10, true, Cmd::W);
            emitText(SIDE_X + 52, y + 12, r.value, TEXT_C, 14, true, Cmd::W);
            y += 26;
        }
        emitText(SIDE_X, y + 16, "M Mute   Esc Menu", SUBTEXT, 9, false, Cmd::W);
    }

    void blitPiece(int p, double baseX, double baseY, double cell) {
        // Draw `p` centered in a 4-wide x 3-tall cell box at (baseX, baseY).
        const auto& cells = cellsOf(p, 0);
        int minC = 99, maxC = -99, minR = 99, maxR = -99;
        for (const CellXY& c : cells) {
            minC = std::min(minC, c.x); maxC = std::max(maxC, c.x);
            minR = std::min(minR, c.y); maxR = std::max(maxR, c.y);
        }
        double offX = (4 - (maxC - minC + 1)) / 2.0 - minC;
        double offY = (3 - (maxR - minR + 1)) / 2.0 - minR;
        for (const CellXY& c : cells)
            drawBlock(baseX + (c.x + offX) * cell, baseY + (c.y + offY) * cell,
                      cell, PIECE_COLORS[p]);
    }
};

// ----------------------------------------------------------------------------
// Self-test: the same headless exercise and assertions as MyTetris.py
// --selftest — all difficulties, then the keyboard/mouse navigation walk.
// ----------------------------------------------------------------------------
static int gSelftestFailures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "SELFTEST FAIL %s:%d: %s\n", "MyTetris.cpp", __LINE__, #cond); \
    gSelftestFailures++; } } while (0)

static int runSelftest() {
    SoundIface silent;
    TetrisCore game(&silent, /*persist=*/false);
    game.render();
    for (int d = 0; d < N_DIFFICULTIES; d++) {
        game.startGame(d);
        for (int i = 0; i < 1500; i++) {
            game.update(FRAME_MS);
            if (i % 7 == 0) game.rotate(1);
            if (i % 11 == 0) game.tryMove(-1, 0);
            if (i % 13 == 0) game.tryMove(1, 0);
            if (i % 17 == 0) game.rotate(-1);
            if (i % 23 == 0) game.hold();
            if (i % 19 == 0) game.hardDrop();
            if (game.state == GState::GameOver) {
                game.tick();
                game.startGame(d);
            }
            game.render();
        }
    }

    // Keyboard navigation (same focus model as MyPocketTanks).
    game.state = GState::Menu;
    game.confirm_menu = false;
    game.focus_action.clear();
    game.speed_step = SPEED_STEP_DEFAULT;   // a limit value hides a speed button
    game.render();
    std::vector<std::string> acts;
    for (const Button& b : game.buttons) acts.push_back(b.action);
    std::vector<std::string> expected;
    for (auto& d : DIFFICULTIES) expected.push_back(std::string("diff:") + d.name);
    expected.push_back("speed-");
    expected.push_back("speed+");
    expected.push_back("start");
    CHECK(acts == expected);
    CHECK(game.handlePress(Key::Tab));
    CHECK(game.focus_action == std::string("diff:") + DIFFICULTIES[0].name);
    game.handlePress(Key::Down);               // spatial: next difficulty
    CHECK(game.focus_action == std::string("diff:") + DIFFICULTIES[1].name);
    game.handlePress(Key::Enter);              // select it (and stay in menu)
    CHECK(game.state == GState::Menu);
    CHECK(game.difficulty == 1);
    game.handlePress(Key::Home);               // Home = previous
    CHECK(game.focus_action == std::string("diff:") + DIFFICULTIES[0].name);
    game.handlePress(Key::Tab, /*shift=*/true);  // Shift-Tab wraps to the end
    CHECK(game.focus_action == "start");
    game.handlePress(Key::Enter);
    CHECK(game.state == GState::Playing);
    int px = game.px;
    game.handlePress(Key::Left);               // arrows move the PIECE now
    CHECK(game.px == px - 1 && game.focus_action.empty());
    game.handlePress(Key::P);                  // pause overlay buttons
    game.render();
    acts.clear();
    for (const Button& b : game.buttons) acts.push_back(b.action);
    CHECK((acts == std::vector<std::string>{ "resume", "retry", "pausemenu" }));
    game.handlePress(Key::Down);
    game.handlePress(Key::PgDn);               // PgDn = down
    CHECK(game.focus_action == "retry");
    game.handlePress(Key::Escape);             // confirm modal
    game.render();
    acts.clear();
    for (const Button& b : game.buttons) acts.push_back(b.action);
    CHECK((acts == std::vector<std::string>{ "confirmY", "confirmN" }));
    game.handlePress(Key::Right);
    game.handlePress(Key::End);                // End = next
    CHECK(game.focus_action == "confirmN");
    game.handlePress(Key::Enter);
    CHECK(game.state == GState::Paused && !game.confirm_menu);
    game.render();                             // click support: press RESUME
    CHECK(!game.buttons.empty() && game.buttons[0].action == "resume");
    {
        const Button& b = game.buttons[0];
        game.onClick((b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2);
    }
    CHECK(game.state == GState::Playing);
    game.state = GState::GameOver;             // gameover buttons
    game.focus_action.clear();
    game.render();
    acts.clear();
    for (const Button& b : game.buttons) acts.push_back(b.action);
    CHECK((acts == std::vector<std::string>{ "retry", "menu" }));
    game.handlePress(Key::Tab, /*shift=*/true);  // Shift-Tab enters at the last
    CHECK(game.focus_action == "menu");
    game.handlePress(Key::Enter);
    CHECK(game.state == GState::Menu);
    if (gSelftestFailures == 0) {
        printf("keyboard/mouse navigation OK: menu, pause, confirm, gameover\n");
        printf("selftest OK: all difficulties, no exceptions; "
               "last run score=%d, lines=%d, level=%d\n",
               game.score, game.lines, game.level);
    } else {
        fprintf(stderr, "selftest FAILED: %d check(s)\n", gSelftestFailures);
    }
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
#pragma comment(lib, "msimg32.lib")
#endif

static void makeAppDir() {
    CreateDirectoryA(appDataDir().c_str(), NULL);
}

// PlaySound from memory, asynchronously: unlike Python's winsound (which
// copies nothing and therefore forbids SND_MEMORY|SND_ASYNC), we keep every
// WAV buffer alive for the program's lifetime, so the combination is safe.
// PlaySound is one-at-a-time either way — matching winsound's behavior.
class WinSound : public SoundIface {
public:
    std::map<std::string, std::vector<uint8_t>> cache;
    bool ok = false;
    WinSound() {
        cache = soundSpecs();
        ok = true;
    }
    bool isEnabled() const override { return ok; }
    void play(const std::string& name) override {
        if (!ok || muted) return;
        auto it = cache.find(name);
        if (it == cache.end()) return;
        PlaySoundA((LPCSTR)it->second.data(), NULL,
                   SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
    void toggleMute() override {
        muted = !muted;
        if (muted) PlaySoundA(NULL, NULL, SND_PURGE);    // cut current audio
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

// GDI rasterizer for the scene commands, drawing into a back buffer.
class GdiRenderer {
public:
    HDC memdc = NULL;
    HBITMAP bmp = NULL, oldBmp = NULL;
    HDC alphadc = NULL;                       // 1x1 helper for FillAlpha
    HBITMAP alphaBmp = NULL, oldAlphaBmp = NULL;
    std::map<int, HFONT> fonts;               // (size*2 + bold) -> font

    void ensure(HDC winDC) {
        if (memdc) return;
        memdc = CreateCompatibleDC(winDC);
        bmp = CreateCompatibleBitmap(winDC, CLIENT_W, CLIENT_H);
        oldBmp = (HBITMAP)SelectObject(memdc, bmp);
        alphadc = CreateCompatibleDC(winDC);
        alphaBmp = CreateCompatibleBitmap(winDC, 1, 1);
        oldAlphaBmp = (HBITMAP)SelectObject(alphadc, alphaBmp);
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

    void draw(const std::vector<Cmd>& scene) {
        for (const Cmd& c : scene) {
            int x0 = (int)std::lround(c.x0), y0 = (int)std::lround(c.y0);
            int x1 = (int)std::lround(c.x1), y1 = (int)std::lround(c.y1);
            switch (c.type) {
            case Cmd::Line: {
                HPEN pen = CreatePen(PS_SOLID, (int)std::lround(c.width),
                                     cref(c.outline));
                HGDIOBJ oldPen = SelectObject(memdc, pen);
                MoveToEx(memdc, x0, y0, NULL);
                LineTo(memdc, x1, y1);
                SelectObject(memdc, oldPen);
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
                    HGDIOBJ oldBrush = SelectObject(memdc, GetStockObject(NULL_BRUSH));
                    Rectangle(memdc, x0, y0, x1 + 1, y1 + 1);
                    SelectObject(memdc, oldBrush);
                    SelectObject(memdc, oldPen);
                    DeleteObject(pen);
                }
                break;
            }
            case Cmd::FillAlpha: {
                SetPixelV(alphadc, 0, 0, cref(c.fill));
                BLENDFUNCTION bf{};
                bf.BlendOp = AC_SRC_OVER;
                bf.SourceConstantAlpha = (BYTE)std::lround(c.alpha * 255);
                AlphaBlend(memdc, x0, y0, x1 - x0, y1 - y0,
                           alphadc, 0, 0, 1, 1, bf);
                break;
            }
            case Cmd::RoundRect: {
                HPEN pen = c.hasOutline
                    ? CreatePen(PS_SOLID, (int)std::lround(c.width), cref(c.outline))
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
            case Cmd::Text: {
                std::wstring w = widen(c.text);
                HGDIOBJ oldFont = SelectObject(memdc, font(c.size, c.bold));
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
    TetrisCore* core = nullptr;
    GdiRenderer renderer;
} gApp;

static Key mapVk(WPARAM vk) {
    switch (vk) {
    case VK_LEFT: return Key::Left;
    case VK_RIGHT: return Key::Right;
    case VK_DOWN: return Key::Down;
    case VK_UP: return Key::Up;
    case 'X': return Key::X;
    case 'Z': return Key::Z;
    case VK_CONTROL: return Key::Ctrl;
    case VK_SPACE: return Key::Space;
    case 'C': return Key::C;
    case VK_SHIFT: return Key::Shift;
    case 'P': return Key::P;
    case 'M': return Key::M;
    case 'R': return Key::R;
    case VK_ESCAPE: return Key::Escape;
    case VK_RETURN: return Key::Enter;
    case VK_TAB: return Key::Tab;
    case VK_HOME: return Key::Home;
    case VK_END: return Key::End;
    case VK_PRIOR: return Key::PgUp;
    case VK_NEXT: return Key::PgDn;
    case VK_OEM_4: return Key::BracketLeft;    // [
    case VK_OEM_6: return Key::BracketRight;   // ]
    case VK_OEM_MINUS: case VK_SUBTRACT: return Key::Minus;
    case VK_OEM_PLUS: case VK_ADD: return Key::Equal;
    default: return Key::None;
    }
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    TetrisCore* core = gApp.core;
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
        BitBlt(dc, 0, 0, CLIENT_W, CLIENT_H, gApp.renderer.memdc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN: {
        if (!core) return 0;
        if (lp & (1 << 30)) {                 // OS auto-repeat: mirror the
            return 0;                          // Python held-set early-return
        }
        // Y/N answer the confirm modal (Tk delivered them as keysyms).
        if (core->confirm_menu && (wp == 'Y' || wp == 'N')) {
            core->charKey((char)wp);
            return 0;
        }
        Key k = mapVk(wp);
        if (k != Key::None) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            core->keyDown(k, shift);
        }
        return 0;
    }
    case WM_KEYUP: {
        if (core) {
            Key k = mapVk(wp);
            if (k != Key::None) core->keyUp(k);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (core) core->onClick((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_CLOSE: {
        if (core) {
            RECT r;
            if (GetWindowRect(hwnd, &r)) core->saveWindowPos(r.left, r.top);
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
    TetrisCore core(&sound, /*persist=*/true);
    gApp.core = &core;

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MyTetrisWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));   // embedded .ico
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r{ 0, 0, CLIENT_W, CLIENT_H };
    AdjustWindowRect(&r, style, FALSE);
    int winW = r.right - r.left, winH = r.bottom - r.top;

    // Restore the saved window position, clamped to the primary screen —
    // same rule as the Python _restore_window_position().
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int sx, sy;
    if (core.savedWindowPos(sx, sy)) {
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        x = std::max(0, std::min(sx, std::max(0, sw - winW)));
        y = std::max(0, std::min(sy, std::max(0, sh - winH)));
    }

    HWND hwnd = CreateWindowW(L"MyTetrisWnd", L"MyTetris", style, x, y,
                              winW, winH, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 1;
    core.requestClose = [hwnd]() { PostMessageW(hwnd, WM_CLOSE, 0, 0); };

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    timeBeginPeriod(1);                        // ~16 ms timer needs 1 ms clock
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
        if (strcmp(__argv[i], "--selftest") == 0) {
            // GUI-subsystem exe: when stdout was redirected (pipe/file) the
            // CRT already inherited it — printf just works. Only when run
            // bare from a console do we need to borrow the parent's console.
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

// afplay needs files, so materialize each WAV once into a private temp dir
// and replay those paths — the same approach as the Python SoundManager.
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
        if (access("/usr/bin/afplay", X_OK) != 0) return;   // no backend
        signal(SIGCHLD, SIG_IGN);              // auto-reap afplay children
        char tmpl[] = "/tmp/mytetris-snd-XXXXXX";
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
        // afplay exits 0 even into an OS-muted output; surface the mute
        // instead of hiding it (same diagnostic as the Python version).
        std::thread([]() {
            FILE* p = popen("/usr/bin/osascript -e "
                            "'output muted of (get volume settings)' "
                            "2>/dev/null", "r");
            if (!p) return;
            char buf[32] = { 0 };
            if (!fgets(buf, sizeof buf, p)) buf[0] = 0;
            pclose(p);
            if (strncmp(buf, "true", 4) == 0)
                fprintf(stderr, "MyTetris: sound is on, but the macOS output "
                        "device is muted — unmute to hear anything (F10, or: "
                        "osascript -e 'set volume without output muted')\n");
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

static NSColor* nsColor(RGB c, double alpha = 1.0) {
    return [NSColor colorWithSRGBRed:c.r / 255.0 green:c.g / 255.0
                                blue:c.b / 255.0 alpha:alpha];
}

static NSFont* sceneFont(int size, bool bold) {
    NSFont* f = [NSFont fontWithName:(bold ? @"Menlo-Bold" : @"Menlo-Regular")
                                size:size];
    if (!f) f = [NSFont userFixedPitchFontOfSize:size];
    return f;
}

@interface TetrisView : NSView {
@public
    TetrisCore* core;
    NSEventModifierFlags prevFlags;
}
@end

@implementation TetrisView
- (BOOL)isFlipped { return YES; }              // top-left origin, like the scene
- (BOOL)acceptsFirstResponder { return YES; }

- (void)drawRect:(NSRect)dirty {
    if (!core) return;
    for (const Cmd& c : core->scene) {
        NSRect r = NSMakeRect(c.x0, c.y0, c.x1 - c.x0, c.y1 - c.y0);
        switch (c.type) {
        case Cmd::Line: {
            NSBezierPath* p = [NSBezierPath bezierPath];
            [p moveToPoint:NSMakePoint(c.x0, c.y0)];
            [p lineToPoint:NSMakePoint(c.x1, c.y1)];
            p.lineWidth = c.width;
            [nsColor(c.outline) setStroke];
            [p stroke];
            break;
        }
        case Cmd::Rect: {
            if (c.hasFill) {
                [nsColor(c.fill) setFill];
                NSRectFill(r);
            }
            if (c.hasOutline) {
                NSBezierPath* p = [NSBezierPath bezierPathWithRect:r];
                p.lineWidth = c.width;
                [nsColor(c.outline) setStroke];
                [p stroke];
            }
            break;
        }
        case Cmd::FillAlpha: {
            [nsColor(c.fill, c.alpha) setFill];
            NSRectFillUsingOperation(r, NSCompositingOperationSourceOver);
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
    case '\t': case 0x19: return Key::Tab;     // 0x19 = shift-tab (backtab)
    case '\r': case 0x03: return Key::Enter;   // 0x03 = keypad enter
    case 27: return Key::Escape;
    case ' ': return Key::Space;
    case 'x': case 'X': return Key::X;
    case 'z': case 'Z': return Key::Z;
    case 'c': case 'C': return Key::C;
    case 'p': case 'P': return Key::P;
    case 'm': case 'M': return Key::M;
    case 'r': case 'R': return Key::R;
    case '[': return Key::BracketLeft;
    case ']': return Key::BracketRight;
    case '-': return Key::Minus;
    case '=': case '+': return Key::Equal;
    default: return Key::None;
    }
}

- (void)keyDown:(NSEvent*)e {
    if (!core || [e isARepeat]) return;        // mirror the held-set early-return
    NSString* chars = [e charactersIgnoringModifiers];
    unichar ch = [chars length] ? [chars characterAtIndex:0] : 0;
    if (core->confirm_menu && (ch == 'y' || ch == 'Y' || ch == 'n' || ch == 'N')) {
        core->charKey((char)ch);
        return;
    }
    Key k = [self mapEvent:e];
    if (k != Key::None) {
        bool shift = ([e modifierFlags] & NSEventModifierFlagShift) != 0
                     || ch == 0x19;
        core->keyDown(k, shift);
    }
}

- (void)keyUp:(NSEvent*)e {
    if (!core) return;
    Key k = [self mapEvent:e];
    if (k != Key::None) core->keyUp(k);
}

- (void)flagsChanged:(NSEvent*)e {
    // Shift = hold, Ctrl = rotate ccw: modifiers arrive here, not in keyDown.
    if (!core) return;
    NSEventModifierFlags now = [e modifierFlags];
    bool shiftNow = (now & NSEventModifierFlagShift) != 0;
    bool shiftWas = (prevFlags & NSEventModifierFlagShift) != 0;
    bool ctrlNow = (now & NSEventModifierFlagControl) != 0;
    bool ctrlWas = (prevFlags & NSEventModifierFlagControl) != 0;
    if (shiftNow && !shiftWas) core->keyDown(Key::Shift, true);
    if (!shiftNow && shiftWas) core->keyUp(Key::Shift);
    if (ctrlNow && !ctrlWas) core->keyDown(Key::Ctrl, shiftNow);
    if (!ctrlNow && ctrlWas) core->keyUp(Key::Ctrl);
    prevFlags = now;
}

- (void)mouseDown:(NSEvent*)e {
    if (!core) return;
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    core->onClick(p.x, p.y);
}
@end

@interface TetrisAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@public
    TetrisCore* core;
    NSWindow* window;
    NSTimer* timer;
}
@end

@implementation TetrisAppDelegate
- (void)saveWindowPosition {
    if (!core || !window) return;
    NSRect f = [window frame];
    NSRect screen = [[NSScreen mainScreen] frame];
    // Tk positions are from the screen's top-left; Cocoa from the bottom-left.
    int x = (int)f.origin.x;
    int y = (int)(screen.size.height - (f.origin.y + f.size.height));
    core->saveWindowPos(x, y);
}
- (void)windowWillClose:(NSNotification*)n {
    [self saveWindowPosition];
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

        // Minimal menu bar so Cmd+Q works.
        NSMenu* bar = [[NSMenu alloc] init];
        NSMenuItem* appItem = [[NSMenuItem alloc] init];
        [bar addItem:appItem];
        NSMenu* appMenu = [[NSMenu alloc] init];
        [appMenu addItemWithTitle:@"Quit MyTetris" action:@selector(terminate:)
                    keyEquivalent:@"q"];
        [appItem setSubmenu:appMenu];
        [NSApp setMainMenu:bar];

        MacSound sound;
        TetrisCore core(&sound, /*persist=*/true);

        NSRect rect = NSMakeRect(0, 0, CLIENT_W, CLIENT_H);
        NSWindow* win = [[NSWindow alloc]
            initWithContentRect:rect
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [win setTitle:@"MyTetris"];
        TetrisView* view = [[TetrisView alloc] initWithFrame:rect];
        view->core = &core;
        view->prevFlags = 0;
        [win setContentView:view];
        [win makeFirstResponder:view];

        TetrisAppDelegate* delegate = [[TetrisAppDelegate alloc] init];
        delegate->core = &core;
        delegate->window = win;
        [win setDelegate:delegate];
        [NSApp setDelegate:delegate];
        core.requestClose = [win]() { [win close]; };

        // Restore the saved position (top-left coords), clamped on-screen.
        int sx, sy;
        if (core.savedWindowPos(sx, sy)) {
            NSRect screen = [[NSScreen mainScreen] frame];
            NSRect f = [win frame];
            int sw = (int)screen.size.width, sh = (int)screen.size.height;
            int w = (int)f.size.width, h = (int)f.size.height;
            int x = std::max(0, std::min(sx, std::max(0, sw - w)));
            int y = std::max(0, std::min(sy, std::max(0, sh - h)));
            [win setFrameTopLeftPoint:NSMakePoint(x, screen.size.height - y)];
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

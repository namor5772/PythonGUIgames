"""MyPocketTanks.py — a Tkinter artillery game in the spirit of Pocket Tanks.

Two tanks trade shots across destructible terrain. No health bars: every point
of damage you deal is a point on the scoreboard, and whoever has the most
points after all volleys wins.

Features:
  * Procedural, fully destructible per-pixel terrain (craters, dirt piles)
  * Weapon PICK phase: players alternate drafting 10 weapons from a pool of 20
  * ...or a ONE WEAPON match: both sides fire one chosen weapon for a
    settable number of rounds (1-20)
  * 20 distinct weapons: splitters, rollers, diggers, napalm, lasers, nukes...
  * Wind that changes every turn and bends every shot
  * Tank movement with a limited fuel tank (and slopes too steep to climb)
  * 1-player mode vs. an aiming AI (Easy / Normal / Hard) or 2-player hotseat
  * Synthesized sound effects (stdlib; Windows winsound / macOS afplay)
  * Config persistence (window position, mode, AI level, match style)
    in %APPDATA%\\MyPocketTanks

Controls (aiming):
  Left / Right .... turret angle    Up / Down ....... power
  End / Home ...... angle + / -     A / D ........... move tank
  Tab / [ ] ....... cycle weapon    Space / Enter ... FIRE
  M ............... mute            Esc ............. menu (asks)
  Mouse ........... all panel buttons work too

Run:  python MyPocketTanks.py       Self-test (headless):  python MyPocketTanks.py --selftest
"""

import atexit
import io
import json
import math
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import wave
import tkinter as tk

# ----------------------------------------------------------------------------
# Field / rendering constants
# ----------------------------------------------------------------------------
FIELD_W = 1000            # battlefield width in pixels (1 terrain column each)
FIELD_H = 560             # battlefield height
PANEL_H = 190             # control panel below the field
WIN_W, WIN_H = FIELD_W, FIELD_H + PANEL_H
FRAME_MS = 16             # ~60 FPS fixed timestep
DT = FRAME_MS / 1000.0

GRAVITY = 600.0           # px/s^2 downward on projectiles
WIND_ACCEL = 14.0         # px/s^2 of horizontal drift per unit of wind
WIND_MAX = 10             # wind ranges -10 .. +10
POWER_SPEED = 7.5         # muzzle speed px/s per point of power (0..100)
SUBSTEPS = 4              # physics substeps per frame (terrain is per-pixel)

ROUNDS = 10               # draft mode: shots per player = weapons drafted
POOL_SIZE = 2 * ROUNDS    # weapon cards offered in the draft pick phase
ROUNDS_MIN, ROUNDS_MAX = 1, 20   # settable shots-per-side, one-weapon mode
FUEL_MAX = 100.0          # per-tank movement budget for the whole match
FUEL_PER_PX = 0.5
MAX_CLIMB = 2.5           # max slope (px rise per px run) a tank can drive up

TANK_W, TANK_H = 26, 12   # hull size (dome and turret drawn on top)
BARREL_LEN = 22

BG = "#0b0b12"
TEXT = "#e6e6ec"
SUBTEXT = "#9a9ab0"
GOLD = "#ffd91a"
PANEL_BG = "#12121c"
PANEL_EDGE = "#2a2a3a"
BTN_BG = "#1d1d2c"
BTN_EDGE = "#3a3a52"
P1_COLOR = "#ef4444"      # player 1 = red tank
P2_COLOR = "#3f7fe0"      # player 2 = blue tank
FONT = "Consolas"

AI_LEVELS = {
    # aim_err: stddev of angle/power noise. sims: candidate shots evaluated.
    "Easy":   {"aim_err": 9.0, "sims": 25,  "blurb": "Wobbly aim"},
    "Normal": {"aim_err": 4.0, "sims": 80,  "blurb": "Decent gunner"},
    "Hard":   {"aim_err": 1.2, "sims": 220, "blurb": "Deadly accurate"},
}
AI_LEVEL_NAMES = list(AI_LEVELS)

# ----------------------------------------------------------------------------
# The arsenal. Every weapon is data + a `kind` that the projectile engine
# dispatches on. dmg is the maximum for a dead-center hit; blasts fall off
# linearly with distance. weight biases the random draft pool.
# ----------------------------------------------------------------------------
WEAPONS = [
    dict(key="single",  name="Single Shot",  kind="shell",   r=30,  dmg=24,
         weight=10, color="#d7d7e0", blurb="The dependable classic"),
    dict(key="bigone",  name="Big One",      kind="shell",   r=70,  dmg=50,
         weight=3,  color="#ff6a3d", blurb="One very large boom"),
    dict(key="triple",  name="Triple Shot",  kind="triple",  r=24,  dmg=15,
         weight=6,  color="#ffd91a", blurb="Three shells, slight spread"),
    dict(key="bucks",   name="Buckshot",     kind="scatter", r=12,  dmg=7,
         weight=6,  color="#ffb46a", blurb="Eight-pellet spray"),
    dict(key="dirt",    name="Dirt Ball",    kind="dirt",    r=48,  dmg=0,
         weight=5,  color="#a8743d", blurb="Buries the target in soil"),
    dict(key="excav",   name="Excavator",    kind="dig",     r=62,  dmg=8,
         weight=5,  color="#8a6a4a", blurb="Scoops a giant crater"),
    dict(key="drill",   name="Drill Bit",    kind="drill",   r=10,  dmg=22,
         weight=4,  color="#b0b0c0", blurb="Bores straight down"),
    dict(key="bounce",  name="Bouncy Bomb",  kind="bouncer", r=28,  dmg=17,
         weight=5,  color="#2ecc55", blurb="Detonates on each of 3 bounces"),
    dict(key="roller",  name="Steamroller",  kind="roller",  r=36,  dmg=32,
         weight=5,  color="#9a5ce0", blurb="Rolls downhill to find you"),
    dict(key="mirv",    name="Pentabomb",    kind="mirv",    r=22,  dmg=14,
         weight=4,  color="#19d3da", blurb="Splits into 5 at the apex"),
    dict(key="napalm",  name="Firestorm",    kind="napalm",  r=26,  dmg=34,
         weight=4,  color="#ff5522", blurb="Flames flow downhill"),
    dict(key="laser",   name="Sky Laser",    kind="beam",    r=16,  dmg=45,
         weight=3,  color="#66eaff", blurb="Orbital beam at impact point"),
    dict(key="hopper",  name="Jack Hopper",  kind="hopper",  r=26,  dmg=16,
         weight=4,  color="#7ee08a", blurb="Explodes, hops on, twice more"),
    dict(key="sniper",  name="Sniper Round", kind="sniper",  r=13,  dmg=55,
         weight=4,  color="#e6e6ec", blurb="Fast, tiny, devastating"),
    dict(key="nuke",    name="Kiloton",      kind="shell",   r=115, dmg=75,
         weight=1,  color="#ffef7a", blurb="You will feel this one"),
    dict(key="quake",   name="Tremor",       kind="quake",   r=85,  dmg=28,
         weight=4,  color="#c08a5a", blurb="Collapses the ground nearby"),
    dict(key="slinger", name="Dirt Slinger", kind="dirtarc", r=30,  dmg=6,
         weight=4,  color="#caa05a", blurb="Flings a ramp of dirt onward"),
    dict(key="magnet",  name="Magno Shot",   kind="magnet",  r=28,  dmg=32,
         weight=4,  color="#e05ac8", blurb="Steers toward the enemy"),
    dict(key="cluster", name="Cluster Pod",  kind="cluster", r=16,  dmg=9,
         weight=5,  color="#ff9f1a", blurb="Pops into 6 bomblets"),
    dict(key="skimmer", name="Skimmer",      kind="skipper", r=22,  dmg=15,
         weight=5,  color="#5ad0ff", blurb="Skips along the ground"),
]
WEAPON_BY_KEY = {w["key"]: w for w in WEAPONS}


def draft_pool(rng):
    """POOL_SIZE weighted-random weapon keys for the pick phase (dupes OK,
    but at most 2 of a kind so the pool stays varied)."""
    keys = [w["key"] for w in WEAPONS]
    weights = [w["weight"] for w in WEAPONS]
    pool, counts = [], {}
    while len(pool) < POOL_SIZE:
        k = rng.choices(keys, weights=weights)[0]
        if counts.get(k, 0) < 2:
            counts[k] = counts.get(k, 0) + 1
            pool.append(k)
    rng.shuffle(pool)
    return pool


# ----------------------------------------------------------------------------
# Terrain: a per-column heightmap. terrain[x] = y of the surface (smaller =
# higher ground). Explosions carve circles out of it; dirt weapons pile onto
# it. A heightmap can't do overhangs, but it makes physics and repainting
# simple and fast, and dirt "settles" for free.
# ----------------------------------------------------------------------------
TERRAIN_MIN = 90          # highest the ground may reach (y, from top)
TERRAIN_MAX = FIELD_H - 30


def generate_terrain(rng):
    """Rolling hills from a few random sine waves plus midpoint jitter."""
    waves = []
    for _ in range(5):
        waves.append((rng.uniform(20, 90),                  # amplitude
                      rng.uniform(1.0, 4.0),                # frequency
                      rng.uniform(0, 2 * math.pi)))         # phase
    base = rng.uniform(FIELD_H * 0.55, FIELD_H * 0.72)
    terrain = []
    for x in range(FIELD_W):
        t = x / FIELD_W
        y = base
        for amp, freq, phase in waves:
            y += amp * math.sin(2 * math.pi * freq * t + phase) / len(waves) * 2
        terrain.append(clamp_terrain(y))
    return terrain


def clamp_terrain(y):
    return max(TERRAIN_MIN, min(TERRAIN_MAX, int(round(y))))


# ----------------------------------------------------------------------------
# Sound: synthesized WAVs, per-platform playback (same approach as MyTetris —
# winsound must NOT combine SND_MEMORY with SND_ASYNC, so playback is
# synchronous on a daemon thread; macOS materializes temp files for afplay).
# ----------------------------------------------------------------------------
SAMPLE_RATE = 22050


def _tone(freq, ms, vol=0.4, wave_type="square"):
    n = int(SAMPLE_RATE * ms / 1000)
    attack = max(1, int(0.004 * SAMPLE_RATE))
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        if wave_type == "square":
            base = 1.0 if math.sin(2 * math.pi * freq * t) >= 0 else -1.0
        else:
            base = math.sin(2 * math.pi * freq * t)
        env = min(1.0, i / attack) * (1.0 - i / n)
        samples.append(int(max(-1.0, min(1.0, base * env * vol)) * 32767))
    return samples


def _noise(ms, vol=0.5, lowpass=0.25):
    """Filtered white noise — the basis of every explosion sound."""
    rng = random.Random(1234)
    n = int(SAMPLE_RATE * ms / 1000)
    samples, prev = [], 0.0
    for i in range(n):
        raw = rng.uniform(-1.0, 1.0)
        prev += lowpass * (raw - prev)          # crude one-pole low-pass
        env = (1.0 - i / n) ** 2
        samples.append(int(max(-1.0, min(1.0, prev * env * vol * 3)) * 32767))
    return samples


def _seq(*tone_lists):
    out = []
    for t in tone_lists:
        out.extend(t)
    return out


def _wav(samples):
    buf = io.BytesIO()
    w = wave.open(buf, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SAMPLE_RATE)
    w.writeframes(b"".join(struct.pack("<h", s) for s in samples))
    w.close()
    return buf.getvalue()


def _sound_specs():
    """name -> WAV bytes for every effect (shared by both playback backends)."""
    return {
        "blip": _wav(_tone(440, 30, 0.25)),
        "pick": _wav(_seq(_tone(523, 40, 0.3), _tone(659, 60, 0.3))),
        "fire": _wav(_seq(_tone(160, 40, 0.5, "sine"), _noise(90, 0.35, 0.5))),
        "boom": _wav(_noise(300, 0.55, 0.12)),
        "bigboom": _wav(_noise(650, 0.7, 0.07)),
        "dirt": _wav(_noise(220, 0.3, 0.5)),
        "bounce": _wav(_tone(300, 45, 0.35, "sine")),
        "laser": _wav(_seq(_tone(1600, 60, 0.35), _tone(1200, 60, 0.35),
                           _tone(800, 90, 0.35))),
        "move": _wav(_tone(90, 35, 0.3, "sine")),
        "win": _wav(_seq(_tone(523, 70, 0.45), _tone(659, 70, 0.45),
                         _tone(784, 70, 0.45), _tone(1047, 160, 0.45))),
        "lose": _wav(_seq(_tone(440, 130, 0.4, "sine"),
                          _tone(330, 130, 0.4, "sine"),
                          _tone(220, 220, 0.4, "sine"))),
    }


class SoundManager:
    def __init__(self, enable=True):
        self.muted = False
        self.enabled = False
        self._ws = None         # winsound module (Windows backend)
        self._afplay = None     # path to afplay (macOS backend)
        self._cache = {}        # name -> WAV bytes (Windows: played from memory)
        self._files = {}        # name -> temp WAV path (macOS: played by afplay)
        if not enable:
            return
        if sys.platform == "win32":
            try:
                import winsound
                self._ws = winsound
            except Exception:
                return
        elif sys.platform == "darwin" and os.path.exists("/usr/bin/afplay"):
            self._afplay = "/usr/bin/afplay"
        else:
            return                                  # no supported backend
        self.enabled = True
        self._build()

    def _build(self):
        self._cache = _sound_specs()
        if self._afplay:
            tmpdir = tempfile.mkdtemp(prefix="mypockettanks-snd-")
            for name, data in self._cache.items():
                path = os.path.join(tmpdir, name + ".wav")
                with open(path, "wb") as f:
                    f.write(data)
                self._files[name] = path
            atexit.register(shutil.rmtree, tmpdir, ignore_errors=True)

    def play(self, name):
        if not self.enabled or self.muted:
            return
        if self._ws is not None:
            data = self._cache.get(name)
            if data is not None:
                threading.Thread(target=self._play_winsound, args=(data,),
                                 daemon=True).start()
        elif self._afplay is not None:
            path = self._files.get(name)
            if path is not None:
                threading.Thread(target=self._play_afplay, args=(path,),
                                 daemon=True).start()

    def _play_winsound(self, data):
        try:
            self._ws.PlaySound(data, self._ws.SND_MEMORY)
        except Exception:
            pass

    def _play_afplay(self, path):
        try:
            subprocess.run([self._afplay, path],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception:
            pass

    def toggle_mute(self):
        self.muted = not self.muted
        if self.muted and self._ws is not None:
            try:
                self._ws.PlaySound(None, self._ws.SND_PURGE)
            except Exception:
                pass


# ----------------------------------------------------------------------------
# Config persistence (window position, last mode/AI level) — one shared dict,
# merged on save so independent savers never clobber each other's keys.
# ----------------------------------------------------------------------------
def _config_path():
    base = os.environ.get("APPDATA") or os.path.expanduser("~")
    return os.path.join(base, "MyPocketTanks", "config.json")


def load_config():
    try:
        with open(_config_path(), "r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def save_config(config):
    try:
        path = _config_path()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=2)
    except Exception:
        pass


# ----------------------------------------------------------------------------
# Tanks
# ----------------------------------------------------------------------------
class Tank:
    def __init__(self, pid, x, color, name, is_ai=False):
        self.pid = pid                  # 0 or 1
        self.x = float(x)               # center of the hull
        self.y = 0.0                    # hull top y; settled onto the terrain
        self.color = color
        self.name = name
        self.is_ai = is_ai
        self.angle = 60.0 if pid == 0 else 120.0   # 0=right, 90=up, 180=left
        self.power = 60.0
        self.score = 0
        self.fuel = FUEL_MAX
        self.arsenal = []               # list of weapon keys, consumed on fire
        self.weapon_i = 0

    def settle(self, terrain):
        """Rest the hull on the terrain (highest ground under the hull)."""
        x0 = max(0, int(self.x) - TANK_W // 2)
        x1 = min(FIELD_W - 1, int(self.x) + TANK_W // 2)
        self.y = min(terrain[x] for x in range(x0, x1 + 1)) - TANK_H

    def center(self):
        return (self.x, self.y + TANK_H * 0.5)

    def muzzle(self):
        a = math.radians(self.angle)
        bx = self.x + math.cos(a) * BARREL_LEN
        by = self.y - 2 - math.sin(a) * BARREL_LEN
        return bx, by

    def current_weapon(self):
        if not self.arsenal:
            return None
        return WEAPON_BY_KEY[self.arsenal[self.weapon_i]]

    def cycle_weapon(self, step):
        if self.arsenal:
            self.weapon_i = (self.weapon_i + step) % len(self.arsenal)


# ----------------------------------------------------------------------------
# The game. Logic and rendering are split: pass root=None for a fully
# headless game (used by --selftest), and all drawing / input binding is
# skipped.
# ----------------------------------------------------------------------------
class PocketTanks:
    def __init__(self, root=None, enable_sound=True, persist=True, seed=None):
        self.root = root
        self.persist = persist
        self.rng = random.Random(seed)
        self.sound = SoundManager(enable=enable_sound and root is not None)
        self.config = load_config() if persist else {}

        self.state = "menu"             # menu | pick | playing | gameover
        self.confirm_menu = False       # "return to menu?" modal over playing
        self.mode = self.config.get("mode", "1P")           # 1P | 2P
        if self.mode not in ("1P", "2P"):
            self.mode = "1P"
        self.ai_level = self.config.get("ai_level", "Normal")
        if self.ai_level not in AI_LEVELS:
            self.ai_level = "Normal"
        # Match style: "draft" = alternate-pick 10 weapons from a pool;
        # "single" = ONE chosen weapon arms both tanks for a settable
        # number of rounds.
        self.match_type = self.config.get("match_type", "draft")
        if self.match_type not in ("draft", "single"):
            self.match_type = "draft"
        sr = self.config.get("single_rounds", ROUNDS)
        if not (isinstance(sr, int) and ROUNDS_MIN <= sr <= ROUNDS_MAX):
            sr = ROUNDS
        self.single_rounds = sr
        self.single_weapon = self.config.get("single_weapon", "single")
        if self.single_weapon not in WEAPON_BY_KEY:
            self.single_weapon = "single"
        self.rounds = ROUNDS            # shots per side; set per match

        self.terrain = generate_terrain(self.rng)
        self.tanks = []
        self.turn = 0                   # index into self.tanks
        self.wind = 0
        self.phase = "aim"              # aim | flight (projectiles/effects live)
        self.projectiles = []
        self.effects = []               # visual-only animations
        self.flames = []                # napalm ground fire (does DoT)
        self.shots_fired = [0, 0]
        self.pool = []                  # pick-phase weapon keys still available
        self.picker = 0                 # whose pick it is
        self.ai_wait = 0                # frames before the AI acts (pacing)
        self.ai_plan = None
        self.winner = None
        self.toast = None               # (text, frames_left) banner
        self.frame = 0

        # UI handles (all None when headless)
        self.canvas = None
        self.terrain_img = None
        self._sky_row = None
        self._dirt_row = None
        self._stars = {}
        self.buttons = []               # (x0,y0,x1,y1, action) hit boxes
        if root is not None:
            self._build_ui()
            self._tick_scheduled = root.after(FRAME_MS, self.tick)
        # Headless games still tick, but the caller drives tick() directly.

    # ------------------------------------------------------------------ match
    def start_match(self):
        """Begin a fresh match: new terrain, tanks, and the draft pool."""
        self.terrain = generate_terrain(self.rng)
        margin = 120
        x1 = self.rng.randint(margin, margin + 120)
        x2 = self.rng.randint(FIELD_W - margin - 120, FIELD_W - margin)
        self.tanks = [
            Tank(0, x1, P1_COLOR, "PLAYER 1"),
            Tank(1, x2, P2_COLOR,
                 "COMPUTER" if self.mode == "1P" else "PLAYER 2",
                 is_ai=(self.mode == "1P")),
        ]
        for t in self.tanks:
            t.settle(self.terrain)
        self.turn = self.rng.randint(0, 1)
        self.rounds = ROUNDS if self.match_type == "draft" \
            else self.single_rounds
        if self.match_type == "single":
            self.pool = [w["key"] for w in WEAPONS]  # one card per weapon
            self.picker = 0                          # a human picks for both
        else:
            self.pool = draft_pool(self.rng)
            self.picker = self.turn
        self.shots_fired = [0, 0]
        self.projectiles, self.effects, self.flames = [], [], []
        self.winner = None
        self.phase = "aim"
        self.confirm_menu = False
        self.ai_plan = None
        self.ai_wait = 30
        self.state = "pick"
        self._new_wind()
        self._repaint_terrain()

    def _new_wind(self):
        self.wind = self.rng.randint(-WIND_MAX, WIND_MAX)

    def current_tank(self):
        return self.tanks[self.turn]

    def enemy_tank(self):
        return self.tanks[1 - self.turn]

    # ------------------------------------------------------------------ pick
    def pick_weapon(self, pool_index):
        """Pick one card from the pool. Draft mode: players alternate until
        each side holds self.rounds weapons. Single mode: the one chosen
        weapon arms BOTH tanks for the whole match and combat begins."""
        if self.state != "pick" or not (0 <= pool_index < len(self.pool)):
            return
        if self.match_type == "single":
            key = self.pool[pool_index]
            self.single_weapon = key           # remembered (and persisted)
            for t in self.tanks:
                t.arsenal = [key] * self.rounds
                t.weapon_i = 0
            self.sound.play("pick")
            self._begin_combat()
            return
        key = self.pool.pop(pool_index)
        self.tanks[self.picker].arsenal.append(key)
        self.sound.play("pick")
        if all(len(t.arsenal) >= self.rounds for t in self.tanks):
            self._begin_combat()
            return
        self.picker = 1 - self.picker
        if len(self.tanks[self.picker].arsenal) >= self.rounds:
            self.picker = 1 - self.picker      # other side still drafting
        self.ai_wait = 20

    def _begin_combat(self):
        self.state = "playing"
        self.phase = "aim"
        self._set_toast(f"{self.current_tank().name} SHOOTS FIRST")
        self.ai_wait = 45

    def _ai_pick(self):
        """AI drafts greedily by damage potential, with some randomness."""
        def value(key):
            w = WEAPON_BY_KEY[key]
            v = w["dmg"] * (1 + w["r"] / 60)
            if w["kind"] in ("mirv", "cluster", "scatter", "triple",
                             "bouncer", "hopper", "skipper"):
                v *= 2.2                       # multi-hit weapons
            return v * self.rng.uniform(0.7, 1.3)
        best = max(range(len(self.pool)), key=lambda i: value(self.pool[i]))
        self.pick_weapon(best)

    # ------------------------------------------------------------------ aim
    def adjust_angle(self, d):
        t = self.current_tank()
        t.angle = max(0.0, min(180.0, t.angle + d))

    def adjust_power(self, d):
        t = self.current_tank()
        t.power = max(5.0, min(100.0, t.power + d))

    def move_tank(self, direction):
        """Drive 1px left/right if fuel remains and the slope is climbable."""
        t = self.current_tank()
        if self.phase != "aim" or t.fuel < FUEL_PER_PX:
            return False
        nx = t.x + direction
        half = TANK_W // 2
        if not (half <= nx <= FIELD_W - 1 - half):
            return False
        here = self.terrain[int(t.x)]
        there = self.terrain[int(nx)]
        if here - there > MAX_CLIMB:           # too steep uphill
            return False
        other = self.enemy_tank()
        if abs(nx - other.x) < TANK_W:         # don't drive through the enemy
            return False
        t.x = nx
        t.fuel -= FUEL_PER_PX
        t.settle(self.terrain)
        if self.frame % 6 == 0:
            self.sound.play("move")
        return True

    # ------------------------------------------------------------------ fire
    def fire(self):
        t = self.current_tank()
        w = t.current_weapon()
        if self.state != "playing" or self.phase != "aim" or w is None:
            return
        mx, my = t.muzzle()
        a = math.radians(t.angle)
        speed = t.power * POWER_SPEED
        vx, vy = math.cos(a) * speed, -math.sin(a) * speed
        kind = w["kind"]
        if kind == "triple":
            for da in (-4, 0, 4):
                aa = math.radians(t.angle + da)
                self._spawn(mx, my, math.cos(aa) * speed,
                            -math.sin(aa) * speed, w, t.pid)
        elif kind == "scatter":
            for _ in range(8):
                aa = math.radians(t.angle + self.rng.uniform(-7, 7))
                sp = speed * self.rng.uniform(0.85, 1.05)
                self._spawn(mx, my, math.cos(aa) * sp,
                            -math.sin(aa) * sp, w, t.pid)
        elif kind == "sniper":
            self._spawn(mx, my, vx * 1.6, vy * 1.6, w, t.pid)
        else:
            p = self._spawn(mx, my, vx, vy, w, t.pid)
            if kind == "bouncer":
                p["bounces"] = 3
            elif kind == "hopper":
                p["hops"] = 3
            elif kind == "skipper":
                p["skips"] = 4
        # The shot is spent whether it lands well or not.
        del t.arsenal[t.weapon_i]
        if t.arsenal:
            t.weapon_i %= len(t.arsenal)
        self.shots_fired[t.pid] += 1
        self.phase = "flight"
        self.sound.play("fire")

    def _spawn(self, x, y, vx, vy, weapon, owner):
        p = dict(x=x, y=y, vx=vx, vy=vy, w=weapon, owner=owner,
                 rolling=False, trail=[])
        self.projectiles.append(p)
        return p

    # ------------------------------------------------------------- projectile
    def _update_projectiles(self):
        """One frame of flight for every live projectile (SUBSTEPS substeps)."""
        dt = DT / SUBSTEPS
        for _ in range(SUBSTEPS):
            for p in self.projectiles[:]:
                if p["rolling"]:
                    self._roll_step(p, dt)
                    continue
                p["vy"] += GRAVITY * dt
                p["vx"] += self.wind * WIND_ACCEL * dt
                if p["w"]["kind"] == "magnet":
                    ex, ey = self.tanks[1 - p["owner"]].center()
                    d = math.hypot(ex - p["x"], ey - p["y"]) or 1.0
                    pull = 260.0
                    p["vx"] += (ex - p["x"]) / d * pull * dt
                    p["vy"] += (ey - p["y"]) / d * pull * dt
                # MIRV splits when it tips over the top of its arc.
                if (p["w"]["kind"] == "mirv" and not p.get("split")
                        and p["vy"] >= 0):
                    p["split"] = True
                    self.projectiles.remove(p)
                    for i in range(5):
                        q = dict(p, vx=p["vx"] + (i - 2) * 42.0, split=True)
                        q["trail"] = list(p["trail"])
                        self.projectiles.append(q)
                    continue
                p["x"] += p["vx"] * dt
                p["y"] += p["vy"] * dt
                if p["x"] < -60 or p["x"] > FIELD_W + 60 or p["y"] > FIELD_H + 60:
                    self.projectiles.remove(p)      # flew off the world
                    continue
                if self.frame % 2 == 0:
                    p["trail"].append((p["x"], p["y"]))
                    if len(p["trail"]) > 40:
                        p["trail"].pop(0)
                hit_tank = self._tank_at(p["x"], p["y"])
                if hit_tank is not None or self._in_ground(p["x"], p["y"]):
                    self._impact(p, direct=hit_tank)

    def _tank_at(self, x, y):
        for t in self.tanks:
            if (abs(x - t.x) <= TANK_W / 2 + 2
                    and t.y - 8 <= y <= t.y + TANK_H + 2):
                return t
        return None

    def _in_ground(self, x, y):
        xi = int(x)
        return 0 <= xi < FIELD_W and y >= self.terrain[xi]

    def _roll_step(self, p, dt):
        """Steamroller ground travel: follow the surface, speed up downhill,
        explode on the enemy, on stalling, or at the field edge."""
        xi = int(p["x"])
        if not (2 <= xi <= FIELD_W - 3):
            self._detonate(p, p["x"], p["y"])
            return
        # y grows downward, so a positive slope means downhill is to the right.
        slope = (self.terrain[xi + 2] - self.terrain[xi - 2]) / 4.0
        p["vx"] += slope * 900.0 * dt               # gravity along the slope
        p["vx"] *= (1 - 0.4 * dt)                   # rolling friction
        p["x"] += p["vx"] * dt
        xi = int(max(0, min(FIELD_W - 1, p["x"])))
        p["y"] = self.terrain[xi] - 3
        p["roll_time"] = p.get("roll_time", 0.0) + dt
        if self.frame % 2 == 0:
            p["trail"].append((p["x"], p["y"]))
            if len(p["trail"]) > 40:
                p["trail"].pop(0)
        hit = self._tank_at(p["x"], p["y"] + 2)
        stalled = abs(p["vx"]) < 12 and p["roll_time"] > 0.4
        if hit is not None or stalled or p["roll_time"] > 6.0:
            self._detonate(p, p["x"], p["y"], direct=hit)

    # ---------------------------------------------------------------- impact
    def _impact(self, p, direct=None):
        """Projectile touched ground or a tank — dispatch on weapon kind."""
        kind = p["w"]["kind"]
        x, y = p["x"], p["y"]
        if kind == "bouncer" and p.get("bounces", 0) > 1 and direct is None:
            p["bounces"] -= 1
            self._explode(p["owner"], x, y, p["w"]["r"], p["w"]["dmg"])
            self._bounce(p, 0.65)
            self.sound.play("bounce")
            return
        if kind == "skipper" and p.get("skips", 0) > 1 and direct is None \
                and abs(p["vy"]) < abs(p["vx"]) * 0.9:
            p["skips"] -= 1
            self._explode(p["owner"], x, y, p["w"]["r"] * 0.7,
                          p["w"]["dmg"] * 0.6)
            self._bounce(p, 0.75)
            self.sound.play("bounce")
            return
        self._detonate(p, x, y, direct=direct)

    def _bounce(self, p, damping):
        """Reflect off the local terrain surface and lift out of the ground."""
        xi = int(max(2, min(FIELD_W - 3, p["x"])))
        slope = (self.terrain[xi + 2] - self.terrain[xi - 2]) / 4.0
        # Surface normal for heightmap terrain y=f(x): (-slope, -1) normalized.
        nx, ny = -slope, -1.0
        nl = math.hypot(nx, ny)
        nx, ny = nx / nl, ny / nl
        dot = p["vx"] * nx + p["vy"] * ny
        p["vx"] = (p["vx"] - 2 * dot * nx) * damping
        p["vy"] = (p["vy"] - 2 * dot * ny) * damping
        p["y"] = self.terrain[xi] - 4

    def _detonate(self, p, x, y, direct=None):
        """The projectile is done; apply its weapon's terminal effect."""
        if p in self.projectiles:
            self.projectiles.remove(p)
        w, owner, kind = p["w"], p["owner"], p["w"]["kind"]
        if kind == "roller" and not p["rolling"] and direct is None \
                and 0 <= int(x) < FIELD_W:
            # First ground contact: start rolling instead of exploding.
            p["rolling"] = True
            p["x"] = max(2, min(FIELD_W - 3, x))
            p["y"] = self.terrain[int(p["x"])] - 3
            if abs(p["vx"]) < 30:
                p["vx"] = 30.0 if p["vx"] >= 0 else -30.0
            self.projectiles.append(p)
            return
        if kind == "dirt":
            self._add_dirt(x, w["r"])
            self.sound.play("dirt")
        elif kind == "dirtarc":
            direction = 1 if p["vx"] >= 0 else -1
            for i in range(5):
                self._add_dirt(x + direction * i * 22, 18 + i * 3)
            self._explode(owner, x, y, w["r"], w["dmg"], direct=direct)
        elif kind == "dig":
            self._carve(x, y, w["r"])
            self._explode(owner, x, y, w["r"], w["dmg"], carve=False,
                          direct=direct)
            self.sound.play("dirt")
        elif kind == "drill":
            xi = int(max(0, min(FIELD_W - 1, x)))
            for step in range(6):                   # staged charges downward
                yy = self.terrain[xi] + 1
                self._carve(xi, yy + step * 12, 14)
                self._explode(owner, xi, yy + step * 12, w["r"] + 6,
                              w["dmg"] / 3.0, carve=False,
                              direct=direct if step == 0 else None)
        elif kind == "beam":
            self._sky_laser(owner, x, w)
        elif kind == "napalm":
            self._explode(owner, x, y, w["r"], w["dmg"] * 0.4, direct=direct)
            self._spawn_flames(owner, x, w)
        elif kind == "quake":
            self._tremor(owner, x, w)
        elif kind == "cluster":
            self._explode(owner, x, y, w["r"], w["dmg"], direct=direct)
            for _ in range(6):
                self._spawn(x, y - 4, self.rng.uniform(-140, 140),
                            self.rng.uniform(-320, -160),
                            dict(w, kind="shell"), owner)
        elif kind == "hopper":
            self._explode(owner, x, y, w["r"], w["dmg"], direct=direct)
            if p.get("hops", 0) > 1:
                direction = 1 if p["vx"] >= 0 else -1
                q = self._spawn(x, y - 6, direction * 190.0, -330.0,
                                w, owner)
                q["hops"] = p["hops"] - 1
        else:                                       # shell / triple / scatter /
            big = w["r"] >= 60                      # sniper / magnet / roller...
            self._explode(owner, x, y, w["r"], w["dmg"], direct=direct)
            if big:
                self.sound.play("bigboom")

    # ------------------------------------------------------ terrain & damage
    def _carve(self, cx, cy, r):
        """Remove a circle of ground centered at (cx, cy) from the heightmap."""
        x0 = max(0, int(cx - r))
        x1 = min(FIELD_W - 1, int(cx + r))
        for x in range(x0, x1 + 1):
            dx = x - cx
            half = math.sqrt(max(0.0, r * r - dx * dx))
            top, bottom = cy - half, cy + half
            h = self.terrain[x]
            if bottom <= h:
                continue                            # circle entirely in the air
            # The slice of circle below the current surface is removed and the
            # ground above it (unsupported in a heightmap) collapses down.
            self.terrain[x] = clamp_terrain(h + (bottom - max(h, top)))
        self._repaint_terrain(x0, x1)
        self._settle_tanks()

    def _add_dirt(self, cx, r):
        """Pile a dome of dirt onto the surface centered at column cx."""
        x0 = max(0, int(cx - r))
        x1 = min(FIELD_W - 1, int(cx + r))
        for x in range(x0, x1 + 1):
            dx = x - cx
            lift = math.sqrt(max(0.0, r * r - dx * dx))
            self.terrain[x] = clamp_terrain(self.terrain[x] - lift)
        self._repaint_terrain(x0, x1)
        self._settle_tanks()

    def _settle_tanks(self):
        for t in self.tanks:
            t.settle(self.terrain)

    def _explode(self, owner, x, y, r, dmg, carve=True, direct=None):
        """Crater + blast damage + score. Direct hits earn a 25% bonus."""
        if carve:
            self._carve(x, y, r)
        for t in self.tanks:
            cx, cy = t.center()
            d = math.hypot(cx - x, cy - y)
            reach = r + TANK_W / 2
            if d >= reach or dmg <= 0:
                continue
            amount = dmg * (1.0 - d / reach)
            if direct is t:
                amount = dmg * 1.25
            amount = int(round(amount))
            if amount <= 0:
                continue
            # Points go to the shooter for enemy damage; gifting yourself
            # damage scores for your opponent instead.
            scorer = owner if t.pid != owner else 1 - owner
            self.tanks[scorer].score += amount
            self._add_effect("text", x=cx, y=t.y - 16, frames=45,
                             text=f"+{amount}",
                             color=self.tanks[scorer].color)
        self._add_effect("boom", x=x, y=y, r=r, frames=18)
        if dmg > 0 or r >= 40:
            self.sound.play("boom")

    def _sky_laser(self, owner, x, w):
        """Vertical orbital beam: vaporize a narrow trench at column x and
        hurt anything under it."""
        xi = int(max(0, min(FIELD_W - 1, x)))
        ground = self.terrain[xi]
        for t in self.tanks:
            if abs(t.x - xi) <= w["r"] + TANK_W / 2:
                amount = int(round(w["dmg"] *
                                   (1 - abs(t.x - xi) / (w["r"] + TANK_W))))
                if amount > 0:
                    scorer = owner if t.pid != owner else 1 - owner
                    self.tanks[scorer].score += amount
                    self._add_effect("text", x=t.x, y=t.y - 16, frames=45,
                                     text=f"+{amount}",
                                     color=self.tanks[scorer].color)
        self._carve(xi, ground + 18, w["r"] + 8)
        self._add_effect("beam", x=xi, y=ground, frames=16)
        self.sound.play("laser")

    def _tremor(self, owner, x, w):
        """Slump the terrain toward its local average around the epicenter."""
        x0 = max(0, int(x - w["r"]))
        x1 = min(FIELD_W - 1, int(x + w["r"]))
        avg = sum(self.terrain[x0:x1 + 1]) / (x1 - x0 + 1)
        for xx in range(x0, x1 + 1):
            f = 1 - abs(xx - x) / w["r"]            # strongest at the center
            jitter = self.rng.uniform(-4, 4)
            self.terrain[xx] = clamp_terrain(
                self.terrain[xx] + (avg - self.terrain[xx]) * 0.6 * f
                + 10 * f + jitter)
        self._repaint_terrain(x0, x1)
        self._settle_tanks()
        for t in self.tanks:
            d = abs(t.x - x)
            if d < w["r"]:
                amount = int(round(w["dmg"] * (1 - d / w["r"])))
                if amount > 0:
                    scorer = owner if t.pid != owner else 1 - owner
                    self.tanks[scorer].score += amount
                    self._add_effect("text", x=t.x, y=t.y - 16, frames=45,
                                     text=f"+{amount}",
                                     color=self.tanks[scorer].color)
        self._add_effect("quake", x=x, y=self.terrain[int(x)], r=w["r"],
                         frames=24)
        self.sound.play("bigboom")

    def _spawn_flames(self, owner, x, w):
        """Napalm: fire particles that slide downhill and burn where they sit.
        All 14 flames share one damage budget so a tank bathing in fire takes
        at most ~1.5x the weapon's rated damage, not unbounded DoT."""
        budget = {"left": w["dmg"] * 1.5}
        for _ in range(14):
            self.flames.append(dict(x=x + self.rng.uniform(-6, 6),
                                    owner=owner, budget=budget,
                                    life=self.rng.randint(50, 110),
                                    dmg=w["dmg"] / 40.0))
        self.sound.play("boom")

    def _update_flames(self):
        for f in self.flames[:]:
            xi = int(max(1, min(FIELD_W - 2, f["x"])))
            # Slide toward the lower neighboring column.
            if self.terrain[xi - 1] > self.terrain[xi + 1]:
                f["x"] -= 25 * DT
            elif self.terrain[xi + 1] > self.terrain[xi - 1]:
                f["x"] += 25 * DT
            f["life"] -= 1
            if f["life"] <= 0:
                self.flames.remove(f)
                continue
            for t in self.tanks:
                if abs(t.x - f["x"]) < TANK_W * 0.8 \
                        and abs(t.y + TANK_H - self.terrain[xi]) < 30:
                    amount = min(f["dmg"], f["budget"]["left"])
                    if amount <= 0:
                        continue
                    f["budget"]["left"] -= amount
                    # Accumulate fractional burn into whole scoreboard points.
                    acc = getattr(t, "_burn_acc", 0.0) + amount
                    whole = int(acc)
                    t._burn_acc = acc - whole
                    if whole > 0:
                        scorer = f["owner"] if t.pid != f["owner"] \
                            else 1 - f["owner"]
                        self.tanks[scorer].score += whole

    def _add_effect(self, etype, **kw):
        kw["type"] = etype
        kw["age"] = 0
        self.effects.append(kw)

    def _set_toast(self, text):
        self.toast = [text, 100]

    # ------------------------------------------------------------- turn flow
    def _flight_done(self):
        return not self.projectiles and not self.flames and \
            all(e["age"] >= e["frames"] for e in self.effects)

    def _end_turn(self):
        """Shot fully resolved: next player, or game over after all volleys."""
        if all(s >= self.rounds for s in self.shots_fired):
            self._finish_game()
            return
        self.turn = 1 - self.turn
        if self.shots_fired[self.turn] >= self.rounds:  # ran dry (odd start)
            self.turn = 1 - self.turn
        self._new_wind()
        self.phase = "aim"
        self.ai_plan = None
        self.ai_wait = 40
        shot = self.shots_fired[self.turn] + 1
        self._set_toast(f"{self.current_tank().name} — "
                        f"SHOT {shot}/{self.rounds}")

    def _finish_game(self):
        self.state = "gameover"
        t0, t1 = self.tanks
        if t0.score == t1.score:
            self.winner = None
            self._set_toast("DEAD HEAT!")
        else:
            self.winner = t0 if t0.score > t1.score else t1
            human_won = not self.winner.is_ai
            self.sound.play("win" if human_won else "lose")

    # ---------------------------------------------------------------- AI turn
    def _simulate_shot(self, angle, power):
        """Cheap ballistic preview (wind + gravity, no weapon behavior).
        Returns the impact point (x, y), or None if the shot leaves the field."""
        t = self.current_tank()
        e = self.enemy_tank()
        a = math.radians(angle)
        x = t.x + math.cos(a) * BARREL_LEN
        y = t.y - 2 - math.sin(a) * BARREL_LEN
        speed = power * POWER_SPEED
        vx, vy = math.cos(a) * speed, -math.sin(a) * speed
        dt = 0.02
        for _ in range(700):
            vy += GRAVITY * dt
            vx += self.wind * WIND_ACCEL * dt
            x += vx * dt
            y += vy * dt
            if x < -40 or x > FIELD_W + 40 or y > FIELD_H + 40:
                return None
            if (abs(x - e.x) <= TANK_W / 2 + 2
                    and e.y - 8 <= y <= e.y + TANK_H + 2):
                return (x, y)
            xi = int(x)
            if 0 <= xi < FIELD_W and y >= self.terrain[xi]:
                return (x, y)
        return None

    def _ai_plan_shot(self):
        """Pick a weapon and search angle/power for the closest impact to the
        enemy, then blur the answer by the difficulty's aim error."""
        lvl = AI_LEVELS[self.ai_level]
        t = self.current_tank()
        ex, ey = self.enemy_tank().center()

        def wvalue(key):
            w = WEAPON_BY_KEY[key]
            v = w["dmg"] * (1 + w["r"] / 80)
            if w["kind"] in ("dirt", "dirtarc"):
                v = 12                               # burying is situational
            return v
        if self.ai_level == "Hard":
            weapon = max(t.arsenal, key=wvalue)
        else:
            weapon = self.rng.choice(t.arsenal)

        def error(angle, power):
            hit = self._simulate_shot(angle, power)
            if hit is None:
                return 1e9
            return math.hypot(hit[0] - ex, hit[1] - ey)

        # Aim into the enemy's half of the sky, then refine the best find.
        toward_right = ex > t.x
        best, best_err = (60.0 if toward_right else 120.0, 60.0), 1e9
        for _ in range(lvl["sims"]):
            angle = self.rng.uniform(15, 88) if toward_right \
                else self.rng.uniform(92, 165)
            power = self.rng.uniform(25, 100)
            err = error(angle, power)
            if err < best_err:
                best, best_err = (angle, power), err
        for _ in range(lvl["sims"] // 2):
            angle = best[0] + self.rng.uniform(-4, 4)
            power = best[1] + self.rng.uniform(-6, 6)
            angle = max(1.0, min(179.0, angle))
            power = max(5.0, min(100.0, power))
            err = error(angle, power)
            if err < best_err:
                best, best_err = (angle, power), err
        angle = max(1.0, min(179.0, best[0] + self.rng.gauss(0, lvl["aim_err"])))
        power = max(5.0, min(100.0, best[1] + self.rng.gauss(0, lvl["aim_err"] * 0.8)))
        return dict(angle=angle, power=power, weapon=weapon)

    def _ai_act(self):
        """One frame of AI behavior during its aim phase: plan once, then
        visibly swing the turret onto the solution before firing."""
        if self.ai_wait > 0:
            self.ai_wait -= 1
            return
        t = self.current_tank()
        if not t.arsenal:
            return
        if self.ai_plan is None:
            self.ai_plan = self._ai_plan_shot()
        plan = self.ai_plan
        if t.arsenal[t.weapon_i] != plan["weapon"] \
                and plan["weapon"] in t.arsenal:
            t.weapon_i = t.arsenal.index(plan["weapon"])
        da = plan["angle"] - t.angle
        dp = plan["power"] - t.power
        if abs(da) > 0.6:
            t.angle += max(-2.0, min(2.0, da))
        elif abs(dp) > 0.6:
            t.power += max(-1.6, min(1.6, dp))
        else:
            t.angle, t.power = plan["angle"], plan["power"]
            self.fire()

    # -------------------------------------------------------------- main loop
    def tick(self):
        """Fixed-timestep frame: advance logic, then draw (when not headless)."""
        if self.root is not None:
            self._tick_scheduled = self.root.after(FRAME_MS, self.tick)
        self.step()
        if self.canvas is not None:
            self.draw()

    def step(self):
        """Pure logic for one frame — everything --selftest exercises."""
        self.frame += 1
        if self.toast is not None:
            self.toast[1] -= 1
            if self.toast[1] <= 0:
                self.toast = None
        for e in self.effects[:]:
            e["age"] += 1
            if e["age"] >= e["frames"]:
                self.effects.remove(e)
        if self.state == "pick":
            if self.mode == "1P" and self.picker == 1 \
                    and not self.confirm_menu:
                if self.ai_wait > 0:
                    self.ai_wait -= 1
                else:
                    self._ai_pick()
                    self.ai_wait = 14
        elif self.state == "playing" and not self.confirm_menu:
            if self.phase == "aim":
                if self.current_tank().is_ai:
                    self._ai_act()
            else:
                self._update_projectiles()
                self._update_flames()
                if self._flight_done():
                    self._end_turn()

    # ---------------------------------------------------------- state changes
    def request_menu(self):
        if self.state in ("playing", "pick"):
            self.confirm_menu = True
        elif self.state == "gameover":
            self.to_menu()

    def to_menu(self):
        self.confirm_menu = False
        self.state = "menu"
        self.projectiles, self.effects, self.flames = [], [], []
        self._save_settings()

    def _save_settings(self):
        if not self.persist:
            return
        self.config["mode"] = self.mode
        self.config["ai_level"] = self.ai_level
        self.config["match_type"] = self.match_type
        self.config["single_rounds"] = self.single_rounds
        self.config["single_weapon"] = self.single_weapon
        save_config(self.config)

    # -------------------------------------------------------------------- UI
    def _build_ui(self):
        root = self.root
        root.title("MyPocketTanks")
        root.resizable(False, False)
        root.configure(bg=BG)
        try:
            ico = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "mypockettanks.ico")
            if sys.platform == "win32" and os.path.exists(ico):
                root.iconbitmap(ico)
        except Exception:
            pass
        self.canvas = tk.Canvas(root, width=WIN_W, height=WIN_H, bg=BG,
                                highlightthickness=0)
        self.canvas.pack()

        # Precompute one hex color per row for the sky gradient and the dirt
        # strata, so painting a terrain column is just an indexed lookup.
        def lerp(c0, c1, t):
            return tuple(int(a + (b - a) * t) for a, b in zip(c0, c1))
        sky_top, sky_bot = (11, 13, 26), (52, 64, 100)
        self._sky_row = []
        for y in range(FIELD_H):
            r, g, b = lerp(sky_top, sky_bot, y / FIELD_H)
            self._sky_row.append(f"#{r:02x}{g:02x}{b:02x}")
        self._dirt_row = []
        for y in range(FIELD_H):
            base = lerp((124, 86, 48), (74, 50, 30), y / FIELD_H)
            band = 1.0 + 0.06 * math.sin(y * 0.22)      # subtle strata bands
            r, g, b = (max(0, min(255, int(c * band))) for c in base)
            self._dirt_row.append(f"#{r:02x}{g:02x}{b:02x}")
        self._grass_row = ["#3fae4f", "#379a46", "#2f853c"]
        star_rng = random.Random(99)
        self._stars = {}
        for _ in range(90):
            sx = star_rng.randrange(0, FIELD_W)
            sy = star_rng.randrange(0, int(FIELD_H * 0.7))
            shade = star_rng.choice(["#8888aa", "#bbbbdd", "#eeeeff"])
            self._stars[(sx, sy)] = shade

        self.terrain_img = tk.PhotoImage(width=FIELD_W, height=FIELD_H)
        self.canvas.create_image(0, 0, anchor="nw", image=self.terrain_img)
        self._repaint_terrain()

        root.bind("<KeyPress>", self._on_key)
        self.canvas.bind("<Button-1>", self._on_click)
        root.protocol("WM_DELETE_WINDOW", self._on_close)
        self._restore_window_pos()

    def _restore_window_pos(self):
        pos = self.config.get("win_pos") if self.persist else None
        if isinstance(pos, str) and len(pos) < 20 and pos.count("+") + pos.count("-") >= 2:
            try:
                self.root.geometry(pos)
            except Exception:
                pass

    def _on_close(self):
        if self.persist:
            # Save the wm position from geometry() (not winfo_x/y — mixing the
            # two makes the window creep by the title-bar height every launch).
            try:
                geo = self.root.geometry()
                i = min(x for x in (geo.find("+"), geo.find("-")) if x > 0)
                self.config["win_pos"] = geo[i:]
            except Exception:
                pass
            self._save_settings()
        self.root.destroy()

    def _repaint_terrain(self, x0=0, x1=FIELD_W - 1):
        """Redraw terrain columns x0..x1 into the PhotoImage buffer."""
        if self.canvas is None or self.terrain_img is None:
            return
        x0 = max(0, int(x0))
        x1 = min(FIELD_W - 1, int(x1))
        if x1 < x0:
            return
        sky, dirt, grass = self._sky_row, self._dirt_row, self._grass_row
        stars, terrain = self._stars, self.terrain
        rows = []
        for y in range(FIELD_H):
            row = []
            for x in range(x0, x1 + 1):
                h = terrain[x]
                if y < h:
                    row.append(stars.get((x, y), sky[y]))
                elif y - h < 3:
                    row.append(grass[y - h])
                else:
                    row.append(dirt[y])
            rows.append("{" + " ".join(row) + "}")
        self.terrain_img.put(" ".join(rows), to=(x0, 0, x1 + 1, FIELD_H))

    # ------------------------------------------------------------------ draw
    def draw(self):
        c = self.canvas
        c.delete("dyn")
        self.buttons = []
        if self.state == "menu":
            c.create_rectangle(0, 0, WIN_W, WIN_H, fill=BG, width=0,
                               tags="dyn")
            self._draw_menu()
            return
        if self.state == "pick":
            c.create_rectangle(0, 0, WIN_W, WIN_H, fill=BG, width=0,
                               tags="dyn")
            self._draw_pick()
            if self.confirm_menu:
                self._draw_confirm()
            return
        self._draw_field()
        self._draw_panel()
        if self.state == "gameover":
            self._draw_gameover()
        if self.confirm_menu:
            self._draw_confirm()

    def _draw_field(self):
        c = self.canvas
        # Tanks (terrain image already sits underneath as a canvas item).
        for t in self.tanks:
            x, y = t.x, t.y
            hw = TANK_W / 2
            mx, my = t.muzzle()
            c.create_line(x, y - 2, mx, my, width=4, fill=_shade(t.color, 0.8),
                          capstyle="round", tags="dyn")
            c.create_oval(x - hw * 0.55, y - 9, x + hw * 0.55, y + 4,
                          fill=_shade(t.color, 1.15), width=0, tags="dyn")
            c.create_rectangle(x - hw, y, x + hw, y + TANK_H - 4,
                               fill=t.color, width=0, tags="dyn")
            c.create_oval(x - hw, y + TANK_H - 9, x + hw, y + TANK_H + 1,
                          fill=_shade(t.color, 0.6), width=0, tags="dyn")
            if (self.state == "playing" and self.phase == "aim"
                    and t is self.current_tank()):
                c.create_polygon(x - 6, y - 26, x + 6, y - 26, x, y - 17,
                                 fill=GOLD, width=0, tags="dyn")
        # Projectiles and their trails.
        for p in self.projectiles:
            for i, (tx, ty) in enumerate(p["trail"][-24:]):
                c.create_oval(tx - 1, ty - 1, tx + 1, ty + 1, width=0,
                              fill=_shade(p["w"]["color"], 0.35 + 0.5 * i / 24),
                              tags="dyn")
            c.create_oval(p["x"] - 3, p["y"] - 3, p["x"] + 3, p["y"] + 3,
                          fill=p["w"]["color"], outline="#ffffff", tags="dyn")
        # Napalm flames flicker between two sizes/colors by frame parity.
        for f in self.flames:
            xi = int(max(0, min(FIELD_W - 1, f["x"])))
            gy = self.terrain[xi]
            big = (self.frame + xi) % 4 < 2
            h = 10 if big else 7
            col = "#ff7a22" if big else "#ffd91a"
            c.create_oval(f["x"] - 3, gy - h, f["x"] + 3, gy + 2,
                          fill=col, width=0, tags="dyn")
        # Effects.
        for e in self.effects:
            frac = e["age"] / e["frames"]
            if e["type"] == "boom":
                r = e["r"] * (0.35 + 0.65 * frac)
                for rr, col in ((r, "#ff5522"), (r * 0.7, "#ff9f1a"),
                                (r * 0.4, "#ffef7a")):
                    c.create_oval(e["x"] - rr, e["y"] - rr,
                                  e["x"] + rr, e["y"] + rr,
                                  outline=col, width=3, tags="dyn")
            elif e["type"] == "beam":
                wdt = 10 * (1 - frac) + 2
                c.create_rectangle(e["x"] - wdt, 0, e["x"] + wdt, e["y"] + 14,
                                   fill="#66eaff", width=0, tags="dyn")
                c.create_rectangle(e["x"] - wdt / 3, 0, e["x"] + wdt / 3,
                                   e["y"] + 14, fill="#e8ffff", width=0,
                                   tags="dyn")
            elif e["type"] == "quake":
                r = e["r"] * frac
                c.create_arc(e["x"] - r, e["y"] - r / 2, e["x"] + r,
                             e["y"] + r / 2, start=0, extent=180,
                             style="arc", outline="#c08a5a", width=3,
                             tags="dyn")
            elif e["type"] == "text":
                c.create_text(e["x"], e["y"] - 22 * frac, text=e["text"],
                              fill=e["color"], font=(FONT, 13, "bold"),
                              tags="dyn")
        # HUD: scores, wind, toast.
        t0, t1 = self.tanks
        c.create_text(14, 12, anchor="w", fill=t0.color, font=(FONT, 14, "bold"),
                      text=f"{t0.name}  {t0.score:>4}", tags="dyn")
        c.create_text(FIELD_W - 14, 12, anchor="e", fill=t1.color,
                      font=(FONT, 14, "bold"),
                      text=f"{t1.score:<4}  {t1.name}", tags="dyn")
        self._draw_wind(FIELD_W // 2, 16)
        if self.toast:
            c.create_text(FIELD_W // 2, 52, text=self.toast[0], fill=GOLD,
                          font=(FONT, 15, "bold"), tags="dyn")

    def _draw_wind(self, cx, cy):
        c = self.canvas
        c.create_text(cx, cy - 6, text="WIND", fill=SUBTEXT, font=(FONT, 8),
                      tags="dyn")
        if self.wind == 0:
            c.create_text(cx, cy + 8, text="CALM", fill=TEXT, font=(FONT, 10),
                          tags="dyn")
            return
        ln = abs(self.wind) * 4
        d = 1 if self.wind > 0 else -1
        c.create_line(cx - d * ln / 2, cy + 8, cx + d * ln / 2, cy + 8,
                      fill="#8fd0ff", width=3, arrow="last", tags="dyn")
        c.create_text(cx, cy + 20, text=str(abs(self.wind)), fill="#8fd0ff",
                      font=(FONT, 9, "bold"), tags="dyn")

    # ----------------------------------------------------------------- panel
    def _round_rect(self, x0, y0, x1, y1, radius=10, **kw):
        """Rounded rectangle: Tk has no native one, so use the classic trick —
        a smoothed polygon whose doubled corner points pin the straight edges
        while smooth=True bends quadratic curves around each corner."""
        r = min(radius, (x1 - x0) / 2, (y1 - y0) / 2)
        pts = [x0 + r, y0, x1 - r, y0, x1, y0, x1, y0 + r,
               x1, y1 - r, x1, y1, x1 - r, y1, x0 + r, y1,
               x0, y1, x0, y1 - r, x0, y0 + r, x0, y0]
        return self.canvas.create_polygon(pts, smooth=True, **kw)

    def _button(self, x0, y0, x1, y1, label, action, enabled=True,
                fill=BTN_BG, fg=TEXT, size=11):
        c = self.canvas
        self._round_rect(x0, y0, x1, y1, 10,
                         fill=fill if enabled else "#15151d",
                         outline=BTN_EDGE, width=1, tags="dyn")
        c.create_text((x0 + x1) / 2, (y0 + y1) / 2, text=label,
                      fill=fg if enabled else "#55556a",
                      font=(FONT, size, "bold"), tags="dyn")
        if enabled:
            self.buttons.append((x0, y0, x1, y1, action))

    def _draw_panel(self):
        c = self.canvas
        top = FIELD_H
        c.create_rectangle(0, top, WIN_W, WIN_H, fill=PANEL_BG,
                           outline=PANEL_EDGE, width=2, tags="dyn")
        t = self.current_tank()
        live = (self.state == "playing" and self.phase == "aim"
                and not t.is_ai and not self.confirm_menu)

        # --- left: angle / power / move -----------------------------------
        lx = 20
        c.create_text(lx, top + 24, anchor="w", text="ANGLE", fill=SUBTEXT,
                      font=(FONT, 10), tags="dyn")
        c.create_text(lx + 74, top + 24, anchor="w",
                      text=f"{t.angle:5.1f}°", fill=TEXT,
                      font=(FONT, 13, "bold"), tags="dyn")
        self._button(lx + 160, top + 12, lx + 196, top + 36, "-", "angle-", live)
        self._button(lx + 202, top + 12, lx + 238, top + 36, "+", "angle+", live)
        c.create_text(lx, top + 60, anchor="w", text="POWER", fill=SUBTEXT,
                      font=(FONT, 10), tags="dyn")
        c.create_text(lx + 74, top + 60, anchor="w", text=f"{t.power:5.1f} ",
                      fill=TEXT, font=(FONT, 13, "bold"), tags="dyn")
        self._button(lx + 160, top + 48, lx + 196, top + 72, "-", "power-", live)
        self._button(lx + 202, top + 48, lx + 238, top + 72, "+", "power+", live)
        # power bar
        c.create_rectangle(lx, top + 76, lx + 238, top + 84,
                           outline=BTN_EDGE, width=1, tags="dyn")
        c.create_rectangle(lx + 1, top + 77, lx + 1 + 236 * t.power / 100,
                           top + 83, fill=t.color, width=0, tags="dyn")
        c.create_text(lx, top + 104, anchor="w", text="MOVE", fill=SUBTEXT,
                      font=(FONT, 10), tags="dyn")
        can_move = live and t.fuel >= FUEL_PER_PX
        self._button(lx + 74, top + 92, lx + 116, top + 116, "◀",
                     "moveL", can_move)
        self._button(lx + 122, top + 92, lx + 164, top + 116, "▶",
                     "moveR", can_move)
        c.create_text(lx, top + 134, anchor="w",
                      text=f"FUEL {t.fuel:5.1f}", fill=SUBTEXT,
                      font=(FONT, 9), tags="dyn")
        c.create_rectangle(lx + 90, top + 128, lx + 238, top + 138,
                           outline=BTN_EDGE, width=1, tags="dyn")
        c.create_rectangle(lx + 91, top + 129,
                           lx + 91 + 146 * t.fuel / FUEL_MAX, top + 137,
                           fill="#2ecc55", width=0, tags="dyn")
        c.create_text(lx, top + 162, anchor="w",
                      text="keys: ←→ or End/Home angle  ↑↓ power  "
                           "A/D move  Tab weapon  Space fire",
                      fill="#55556a", font=(FONT, 9), tags="dyn")

        # --- middle: weapon selector ---------------------------------------
        mx = 320
        w = t.current_weapon()
        c.create_text(mx + 170, top + 18, text=f"{t.name} — SHOT "
                      f"{min(self.rounds, self.shots_fired[t.pid] + 1)}"
                      f"/{self.rounds}",
                      fill=t.color, font=(FONT, 11, "bold"), tags="dyn")
        self._button(mx, top + 34, mx + 40, top + 86, "◀", "wprev", live)
        self._button(mx + 300, top + 34, mx + 340, top + 86, "▶",
                     "wnext", live)
        c.create_rectangle(mx + 48, top + 34, mx + 292, top + 86,
                           fill="#191926", outline=BTN_EDGE, tags="dyn")
        if w is not None:
            c.create_rectangle(mx + 58, top + 44, mx + 78, top + 76,
                               fill=w["color"], width=0, tags="dyn")
            count = t.arsenal.count(w["key"])
            extra = f" x{count}" if count > 1 else ""
            c.create_text(mx + 88, top + 52, anchor="w", text=w["name"] + extra,
                          fill=TEXT, font=(FONT, 13, "bold"), tags="dyn")
            c.create_text(mx + 88, top + 71, anchor="w", text=w["blurb"],
                          fill=SUBTEXT, font=(FONT, 9), tags="dyn")
        # remaining arsenal dots (pitch shrinks so long one-weapon
        # arsenals still fit between the selector and the FIRE button)
        n = len(t.arsenal)
        pitch = min(26, 356 // n) if n else 26
        dw = max(4, min(18, pitch - 8))
        for i, key in enumerate(t.arsenal):
            col = WEAPON_BY_KEY[key]["color"]
            outline = "#ffffff" if i == t.weapon_i else ""
            x0 = mx + 48 + i * pitch
            c.create_rectangle(x0, top + 96, x0 + dw, top + 114,
                               fill=col, outline=outline, width=2, tags="dyn")
        noun = "shots" if self.match_type == "single" else "weapons"
        c.create_text(mx + 170, top + 136, text=f"{noun} left: "
                      f"{len(t.arsenal)}", fill=SUBTEXT, font=(FONT, 9),
                      tags="dyn")

        # --- right: FIRE ----------------------------------------------------
        fx = 740
        self._button(fx, top + 30, fx + 230, top + 100, "F I R E", "fire",
                     live, fill="#38182a" if live else BTN_BG,
                     fg="#ff6a6a" if live else TEXT, size=22)
        status = ""
        if self.state == "playing" and t.is_ai and self.phase == "aim":
            status = "computer is aiming..."
        elif self.phase == "flight":
            status = "shot in flight"
        c.create_text(fx + 115, top + 120, text=status, fill=SUBTEXT,
                      font=(FONT, 10, "italic"), tags="dyn")
        c.create_text(fx + 115, top + 156, text="M mute   Esc menu",
                      fill="#55556a", font=(FONT, 9), tags="dyn")

    # ------------------------------------------------------------------ menu
    def _draw_menu(self):
        c = self.canvas
        cx = WIN_W // 2
        c.create_text(cx, 100, text="MY POCKET TANKS", fill=GOLD,
                      font=(FONT, 44, "bold"), tags="dyn")
        c.create_text(cx, 146, text="artillery duel on destructible ground",
                      fill=SUBTEXT, font=(FONT, 14), tags="dyn")
        # mode select
        y = 218
        c.create_text(cx - 260, y, anchor="w", text="MODE", fill=SUBTEXT,
                      font=(FONT, 13), tags="dyn")
        for i, (mode, label) in enumerate([("1P", "1 PLAYER vs COMPUTER"),
                                           ("2P", "2 PLAYER HOTSEAT")]):
            sel = self.mode == mode
            x0 = cx - 150 + i * 260
            self._button(x0, y - 18, x0 + 245, y + 18, label, f"mode:{mode}",
                         True, fill="#26263a" if sel else BTN_BG,
                         fg=GOLD if sel else TEXT, size=11)
        y = 282
        if self.mode == "1P":
            c.create_text(cx - 260, y, anchor="w", text="AI", fill=SUBTEXT,
                          font=(FONT, 13), tags="dyn")
            for i, name in enumerate(AI_LEVEL_NAMES):
                sel = self.ai_level == name
                x0 = cx - 150 + i * 175
                self._button(x0, y - 18, x0 + 160, y + 18,
                             name.upper(), f"ai:{name}", True,
                             fill="#26263a" if sel else BTN_BG,
                             fg=GOLD if sel else TEXT, size=11)
            c.create_text(cx - 150, y + 32, anchor="w",
                          text=AI_LEVELS[self.ai_level]["blurb"],
                          fill=SUBTEXT, font=(FONT, 10), tags="dyn")
        # match style: classic draft, or one weapon for a settable rounds
        y = 352
        c.create_text(cx - 260, y, anchor="w", text="MATCH", fill=SUBTEXT,
                      font=(FONT, 13), tags="dyn")
        for i, (mt, label) in enumerate([("draft", "DRAFT 10 FROM POOL"),
                                         ("single", "ONE WEAPON ONLY")]):
            sel = self.match_type == mt
            x0 = cx - 150 + i * 260
            self._button(x0, y - 18, x0 + 245, y + 18, label, f"match:{mt}",
                         True, fill="#26263a" if sel else BTN_BG,
                         fg=GOLD if sel else TEXT, size=11)
        if self.match_type == "single":
            y = 416
            c.create_text(cx - 260, y, anchor="w", text="ROUNDS",
                          fill=SUBTEXT, font=(FONT, 13), tags="dyn")
            self._button(cx - 150, y - 18, cx - 114, y + 18, "-", "rounds-",
                         self.single_rounds > ROUNDS_MIN)
            c.create_text(cx - 80, y, text=str(self.single_rounds),
                          fill=GOLD, font=(FONT, 16, "bold"), tags="dyn")
            self._button(cx - 46, y - 18, cx - 10, y + 18, "+", "rounds+",
                         self.single_rounds < ROUNDS_MAX)
            c.create_text(cx + 20, y, anchor="w",
                          text="shots per tank — pick the weapon next",
                          fill=SUBTEXT, font=(FONT, 10), tags="dyn")
        self._button(cx - 130, 450, cx + 130, 508, "S T A R T", "start",
                     True, fill="#183822", fg="#7ee08a", size=20)
        if self.match_type == "single":
            n = self.single_rounds
            what = ("Pick ONE weapon; both tanks fire it every round: "
                    f"{n} shot{'s' if n != 1 else ''} per side.")
        else:
            what = ("Draft 10 weapons each, then take turns: "
                    f"{ROUNDS} shots per side.")
        c.create_text(cx, 548, fill=SUBTEXT, font=(FONT, 10),
                      justify="center",
                      text=what + "\nDamage dealt = points scored. "
                           "Self-damage scores for your opponent. "
                           "Most points wins.",
                      tags="dyn")
        c.create_text(cx, WIN_H - 60, fill="#55556a", font=(FONT, 10),
                      justify="center",
                      text="←→ or End/Home angle   ↑↓ power   A/D move"
                           "   Tab/[ ] weapon   Space fire   M mute   "
                           "Esc menu\nEnter/click START to play",
                      tags="dyn")

    # ------------------------------------------------------------------ pick
    def _draw_pick(self):
        c = self.canvas
        cx = WIN_W // 2
        single = self.match_type == "single"
        if single:
            c.create_text(cx, 34, text="CHOOSE THE MATCH WEAPON", fill=GOLD,
                          font=(FONT, 24, "bold"), tags="dyn")
            c.create_text(cx, 66, text="both tanks fire it for all "
                          f"{self.rounds} round"
                          f"{'s' if self.rounds != 1 else ''}",
                          fill=TEXT, font=(FONT, 13, "bold"), tags="dyn")
        else:
            picker = self.tanks[self.picker]
            c.create_text(cx, 34, text="WEAPON DRAFT", fill=GOLD,
                          font=(FONT, 24, "bold"), tags="dyn")
            who = f"{picker.name} PICKS" if not (self.mode == "1P" and
                                                 self.picker == 1) \
                else "COMPUTER IS PICKING..."
            c.create_text(cx, 66, text=who, fill=picker.color,
                          font=(FONT, 15, "bold"), tags="dyn")
            for pid, t in enumerate(self.tanks):
                x = 130 if pid == 0 else WIN_W - 130
                c.create_text(x, 34, text=f"{t.name}: "
                              f"{len(t.arsenal)}/{self.rounds}",
                              fill=t.color, font=(FONT, 11, "bold"),
                              tags="dyn")
        # cards, 5 x 4 grid
        human_turn = (single or not (self.mode == "1P" and self.picker == 1)) \
            and not self.confirm_menu
        cols, cw, ch, gap = 5, 182, 96, 8
        gx = (WIN_W - cols * cw - (cols - 1) * gap) / 2
        gy = 96
        for i, key in enumerate(self.pool):
            w = WEAPON_BY_KEY[key]
            col, row = i % cols, i // cols
            x0 = gx + col * (cw + gap)
            y0 = gy + row * (ch + gap)
            last = single and key == self.single_weapon
            self._round_rect(x0, y0, x0 + cw, y0 + ch, 12, fill="#191926",
                             outline=GOLD if last else BTN_EDGE,
                             width=2 if last else 1, tags="dyn")
            c.create_rectangle(x0 + 10, y0 + 12, x0 + 26, y0 + 40,
                               fill=w["color"], width=0, tags="dyn")
            c.create_text(x0 + 34, y0 + 20, anchor="w", text=w["name"],
                          fill=TEXT, font=(FONT, 11, "bold"), tags="dyn")
            c.create_text(x0 + 34, y0 + 38, anchor="w",
                          text=f"dmg {w['dmg']}  r {w['r']}",
                          fill=SUBTEXT, font=(FONT, 9), tags="dyn")
            c.create_text(x0 + 10, y0 + 62, anchor="w", text=w["blurb"],
                          fill=SUBTEXT, font=(FONT, 9), tags="dyn")
            if human_turn:
                self.buttons.append((x0, y0, x0 + cw, y0 + ch, f"pick:{i}"))
        hint = ("click the weapon both tanks will use for the whole match"
                if single else
                "click a card to draft it — "
                "you alternate picks with your opponent")
        c.create_text(cx, WIN_H - 40, fill="#55556a", font=(FONT, 10),
                      text=hint, tags="dyn")

    # ------------------------------------------------------------- overlays
    def _draw_gameover(self):
        c = self.canvas
        cx = WIN_W // 2
        c.create_rectangle(cx - 320, 150, cx + 320, 420, fill="#12121c",
                           outline=GOLD, width=2, tags="dyn")
        if self.winner is None:
            c.create_text(cx, 210, text="DEAD HEAT!", fill=GOLD,
                          font=(FONT, 30, "bold"), tags="dyn")
        else:
            c.create_text(cx, 210, text=f"{self.winner.name} WINS!",
                          fill=self.winner.color, font=(FONT, 30, "bold"),
                          tags="dyn")
        t0, t1 = self.tanks
        c.create_text(cx, 270, text=f"{t0.name} {t0.score}   —   "
                      f"{t1.score} {t1.name}", fill=TEXT,
                      font=(FONT, 16, "bold"), tags="dyn")
        self._button(cx - 220, 320, cx - 20, 370, "REMATCH (R)", "rematch")
        self._button(cx + 20, 320, cx + 220, 370, "MENU (Esc)", "menu")

    def _draw_confirm(self):
        c = self.canvas
        cx = WIN_W // 2
        c.create_rectangle(cx - 240, 220, cx + 240, 340, fill="#12121c",
                           outline=GOLD, width=2, tags="dyn")
        c.create_text(cx, 258, text="Return to menu? Match is lost.",
                      fill=TEXT, font=(FONT, 13, "bold"), tags="dyn")
        self._button(cx - 160, 285, cx - 20, 320, "YES (Y)", "confirmY")
        self._button(cx + 20, 285, cx + 160, 320, "NO (N)", "confirmN")

    # ----------------------------------------------------------------- input
    def _on_click(self, ev):
        for (x0, y0, x1, y1, action) in self.buttons:
            if x0 <= ev.x <= x1 and y0 <= ev.y <= y1:
                self._do_action(action)
                return

    def _do_action(self, action):
        if action == "start":
            self.start_match()
        elif action.startswith("mode:"):
            self.mode = action[5:]
            self.sound.play("blip")
        elif action.startswith("ai:"):
            self.ai_level = action[3:]
            self.sound.play("blip")
        elif action.startswith("match:"):
            self.match_type = action[6:]
            self.sound.play("blip")
        elif action == "rounds-":
            self.single_rounds = max(ROUNDS_MIN, self.single_rounds - 1)
            self.sound.play("blip")
        elif action == "rounds+":
            self.single_rounds = min(ROUNDS_MAX, self.single_rounds + 1)
            self.sound.play("blip")
        elif action.startswith("pick:"):
            self.pick_weapon(int(action[5:]))
        elif action == "angle-":
            self.adjust_angle(-1)
        elif action == "angle+":
            self.adjust_angle(1)
        elif action == "power-":
            self.adjust_power(-1)
        elif action == "power+":
            self.adjust_power(1)
        elif action == "moveL":
            for _ in range(4):
                self.move_tank(-1)
        elif action == "moveR":
            for _ in range(4):
                self.move_tank(1)
        elif action == "wprev":
            self.current_tank().cycle_weapon(-1)
            self.sound.play("blip")
        elif action == "wnext":
            self.current_tank().cycle_weapon(1)
            self.sound.play("blip")
        elif action == "fire":
            self.fire()
        elif action == "rematch":
            self.start_match()
        elif action == "menu":
            self.to_menu()
        elif action == "confirmY":
            self.to_menu()
        elif action == "confirmN":
            self.confirm_menu = False

    def _on_key(self, ev):
        key = ev.keysym
        if key in ("m", "M"):
            self.sound.toggle_mute()
            return
        if self.confirm_menu:
            if key in ("y", "Y"):
                self.to_menu()
            elif key in ("n", "N", "Escape"):
                self.confirm_menu = False
            return
        if self.state == "menu":
            if key in ("Return", "space"):
                self.start_match()
            elif key in ("1", "2"):
                self.mode = "1P" if key == "1" else "2P"
            elif key in ("Left", "Right") and self.mode == "1P":
                i = AI_LEVEL_NAMES.index(self.ai_level)
                i = (i + (1 if key == "Right" else -1)) % len(AI_LEVEL_NAMES)
                self.ai_level = AI_LEVEL_NAMES[i]
            return
        if self.state == "gameover":
            if key in ("r", "R", "Return", "space"):
                self.start_match()
            elif key == "Escape":
                self.to_menu()
            return
        if key == "Escape":
            self.request_menu()
            return
        if self.state != "playing" or self.phase != "aim" \
                or self.current_tank().is_ai:
            return
        shift = bool(ev.state & 0x0001)
        step = 5 if shift else 1
        if key == "Left":
            self.adjust_angle(step)      # left = raise toward the left
        elif key == "Right":
            self.adjust_angle(-step)
        elif key in ("End", "KP_End"):
            self.adjust_angle(step)      # End = angle readout up
        elif key in ("Home", "KP_Home"):
            self.adjust_angle(-step)     # Home = angle readout down
        elif key == "Up":
            self.adjust_power(step)
        elif key == "Down":
            self.adjust_power(-step)
        elif key in ("a", "A"):
            self.move_tank(-1)
        elif key in ("d", "D"):
            self.move_tank(1)
        elif key in ("Tab", "bracketright"):
            self.current_tank().cycle_weapon(1)
            self.sound.play("blip")
        elif key == "bracketleft":
            self.current_tank().cycle_weapon(-1)
            self.sound.play("blip")
        elif key in ("space", "Return"):
            self.fire()


def _shade(hex_color, factor):
    """Lighten (>1) or darken (<1) a #rrggbb color."""
    h = hex_color.lstrip("#")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    r = max(0, min(255, int(r * factor)))
    g = max(0, min(255, int(g * factor)))
    b = max(0, min(255, int(b * factor)))
    return f"#{r:02x}{g:02x}{b:02x}"


# ----------------------------------------------------------------------------
# Self-test: headless (no Tk window). Fires every weapon in a controlled game
# and then plays full AI-vs-AI matches at every difficulty, asserting that
# every shot resolves, scores stay sane, and the terrain stays in bounds.
# ----------------------------------------------------------------------------
def _assert_terrain_ok(g, ctx):
    assert len(g.terrain) == FIELD_W, f"{ctx}: terrain length changed"
    for x in range(0, FIELD_W, 7):
        y = g.terrain[x]
        assert TERRAIN_MIN <= y <= TERRAIN_MAX, f"{ctx}: terrain[{x}]={y}"


def _run_until_resolved(g, ctx, max_frames=4000):
    frames = 0
    while g.phase == "flight" and frames < max_frames:
        g.step()
        frames += 1
    assert g.phase == "aim" or g.state == "gameover", \
        f"{ctx}: shot never resolved ({len(g.projectiles)} projectiles, " \
        f"{len(g.flames)} flames, {len(g.effects)} effects live)"


def selftest():
    print("MyPocketTanks selftest...")

    # 1. Every weapon fires and resolves, at several angles/powers.
    for w in WEAPONS:
        for angle, power in ((45, 75), (80, 40), (135, 90), (20, 100)):
            g = PocketTanks(root=None, enable_sound=False, persist=False,
                            seed=hash((w["key"], angle)) & 0xFFFF)
            g.mode = "2P"
            g.start_match()
            g.state = "playing"
            g.phase = "aim"
            for t in g.tanks:
                t.arsenal = [w["key"]] * 3
                t.weapon_i = 0
            g.turn = 0
            t = g.current_tank()
            t.angle, t.power = float(angle), float(power)
            before = [tk.score for tk in g.tanks]
            g.fire()
            assert g.phase == "flight", f"{w['key']}: fire() did not launch"
            _run_until_resolved(g, w["key"])
            _assert_terrain_ok(g, w["key"])
            assert all(tk.score >= b for tk, b in zip(g.tanks, before)), \
                f"{w['key']}: score decreased"
        print(f"  weapon ok: {w['name']}")

    # 2. Tank movement respects fuel, bounds, and slopes.
    g = PocketTanks(root=None, enable_sound=False, persist=False, seed=7)
    g.mode = "2P"
    g.start_match()
    g.state = "playing"
    for t in g.tanks:
        t.arsenal = ["single"] * ROUNDS
    t = g.current_tank()
    moved = 0
    for _ in range(1000):
        if g.move_tank(1):
            moved += 1
    assert t.fuel >= 0, "fuel went negative"
    assert moved <= FUEL_MAX / FUEL_PER_PX, "moved farther than fuel allows"
    print(f"  movement ok ({moved} px on a full tank)")

    # 3. Full AI-vs-AI matches at every difficulty level.
    for level in AI_LEVEL_NAMES:
        g = PocketTanks(root=None, enable_sound=False, persist=False,
                        seed=level)
        g.mode = "1P"
        g.ai_level = level
        g.start_match()
        g.tanks[0].is_ai = True            # both sides play themselves
        while g.state == "pick":
            g._ai_pick()
        assert all(len(t.arsenal) == ROUNDS for t in g.tanks)
        frames = 0
        while g.state != "gameover" and frames < 400000:
            g.step()
            frames += 1
        assert g.state == "gameover", f"{level}: match never finished"
        assert g.shots_fired == [ROUNDS, ROUNDS], \
            f"{level}: shots_fired={g.shots_fired}"
        assert all(t.score >= 0 for t in g.tanks)
        _assert_terrain_ok(g, level)
        s0, s1 = g.tanks[0].score, g.tanks[1].score
        print(f"  match ok: AI {level:<6} — final {s0} : {s1} "
              f"({frames} frames)")

    # 4. One-weapon matches: both tanks share one weapon for N rounds.
    for rounds, wkey in ((1, "bigone"), (4, "triple")):
        g = PocketTanks(root=None, enable_sound=False, persist=False,
                        seed=rounds)
        g.mode = "1P"
        g.ai_level = "Easy"
        g.match_type = "single"
        g.single_rounds = rounds
        g.start_match()
        assert g.state == "pick" and len(g.pool) == len(WEAPONS), \
            "single mode should offer every weapon"
        g.pick_weapon(g.pool.index(wkey))
        assert g.state == "playing", "single pick did not start combat"
        assert all(t.arsenal == [wkey] * rounds for t in g.tanks), \
            "both tanks should hold the chosen weapon x rounds"
        g.tanks[0].is_ai = True            # both sides play themselves
        frames = 0
        while g.state != "gameover" and frames < 200000:
            g.step()
            frames += 1
        assert g.state == "gameover", f"one-weapon x{rounds}: never finished"
        assert g.shots_fired == [rounds, rounds], \
            f"one-weapon: shots_fired={g.shots_fired}"
        _assert_terrain_ok(g, f"one-weapon {wkey}")
        print(f"  one-weapon ok: {wkey} x{rounds} — "
              f"{g.tanks[0].score}:{g.tanks[1].score}")

    print("All selftests passed.")


def main():
    if "--selftest" in sys.argv:
        selftest()
        return
    root = tk.Tk()
    PocketTanks(root)
    root.mainloop()


if __name__ == "__main__":
    main()

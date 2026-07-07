"""package_games.py — build friend-shareable zips of the apps.

Bundles each app with its icons, the license, a per-app install/play guide
and the one-click installers into dist/<Name>.zip (git-ignored). A friend
unzips it on Windows or macOS, double-clicks the installer for their OS,
and gets a Desktop shortcut/app — see packaging/README-<Name>.txt.

There is ONE pair of installer sources (packaging/INSTALL-Windows.bat,
packaging/Install-macOS.command), written for MyPocketTanks and verified
end-to-end; the other apps' copies are derived from them at zip time by
token substitution: "MyPocketTanks" -> app name (which also rewrites
MyPocketTanks.py, since every app is <Name>.py), "mypockettanks" -> icon
base name, and the shortcut-description tagline. A leftover-token check
fails the build if an installer grows app-specific wording outside those
tokens.

The zip is built from bytes we control, not raw working-tree files,
because the installers only work with the right *platform* details:
  * Install-macOS.command must be LF-only (a CR after `#!/bin/bash` breaks
    the interpreter line) and carry the executable bit — stored via a unix
    ZipInfo so macOS's Archive Utility restores `chmod +x` on extraction.
  * INSTALL-Windows.bat must be CRLF (cmd.exe mis-parses labels/GOTO in
    LF-only batch files).
  * All friend-facing text stays plain ASCII (cmd.exe's OEM codepage would
    garble anything fancier).

Run:  python package_games.py                    # all three apps
      python package_games.py MyTetris Sun2Set   # a subset
"""

import os
import py_compile
import sys
import time
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.join(ROOT, "dist")

# App name -> the tagline used in the Desktop shortcut's description.
GAMES = {
    "MyPocketTanks": "turn-based artillery duel",
    "MyTetris":      "the classic falling-blocks game",
    "Sun2Set":       "sunrise and sunset almanac",
}
_CANON = "MyPocketTanks"          # the app the installer sources are written for


def _customize(text, name):
    """Rewrite the canonical (MyPocketTanks) installer text for another app."""
    text = text.replace(_CANON.lower(), name.lower())   # icon file names
    text = text.replace(_CANON, name)                   # app/script/shortcut names
    text = text.replace(GAMES[_CANON], GAMES[name])     # shortcut description
    if name != _CANON:
        for leftover in ("ockettanks", "artillery"):
            if leftover in text.lower():
                raise SystemExit(
                    f"installer template leaked '{leftover}' into the {name} "
                    "build - route app-specific wording through the "
                    "MyPocketTanks/tagline tokens (see package_games.py)")
    return text


def _files(name):
    lo = name.lower()
    # (source relative to repo root, name inside the zip, eol, mode, customize)
    # eol: "crlf"/"lf" = normalize text line endings, None = verbatim bytes.
    return [
        (f"{name}.py",                      f"{name}.py",            None,   0o644, False),
        (f"{lo}.ico",                       f"{lo}.ico",             None,   0o644, False),
        (f"{lo}.png",                       f"{lo}.png",             None,   0o644, False),
        ("LICENSE",                         "LICENSE.txt",           "crlf", 0o644, False),
        (f"packaging/README-{name}.txt",    "README.txt",            "crlf", 0o644, False),
        ("packaging/INSTALL-Windows.bat",   "INSTALL-Windows.bat",   "crlf", 0o644, True),
        ("packaging/Install-macOS.command", "Install-macOS.command", "lf",   0o755, True),
    ]


def _load(path, eol, customize, name):
    with open(path, "rb") as f:
        data = f.read()
    if eol:
        text = data.decode("utf-8").replace("\r\n", "\n")
        if customize:
            text = _customize(text, name)
        if eol == "crlf":
            text = text.replace("\n", "\r\n")
        try:
            data = text.encode("ascii")     # friend-facing text stays ASCII
        except UnicodeEncodeError as e:
            raise SystemExit(f"{path} must stay plain ASCII: {e}")
    return data


def build(name):
    # Refuse to ship an app that doesn't even compile.
    py_compile.compile(os.path.join(ROOT, f"{name}.py"), doraise=True)

    files = _files(name)
    missing = [src for src, *_ in files
               if not os.path.exists(os.path.join(ROOT, src))]
    if missing:
        hint = (f"  (run make_{name.lower().lstrip('my')}_icon*.py to "
                "regenerate icons)" if any(m.endswith((".ico", ".png"))
                                           for m in missing) else "")
        raise SystemExit(f"{name}: missing input file(s): "
                         + ", ".join(missing) + hint)

    zip_path = os.path.join(DIST, f"{name}.zip")
    os.makedirs(DIST, exist_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for src, arc, eol, mode, customize in files:
            path = os.path.join(ROOT, src)
            data = _load(path, eol, customize, name)
            zi = zipfile.ZipInfo(
                f"{name}/{arc}",
                date_time=time.localtime(os.path.getmtime(path))[:6])
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.create_system = 3                        # "unix": macOS Archive
            zi.external_attr = (0o100000 | mode) << 16  # Utility restores mode
            z.writestr(zi, data)
    print(f"Wrote {zip_path} ({os.path.getsize(zip_path) / 1024:.0f} KB)")


def main(argv):
    names = argv or list(GAMES)
    unknown = [n for n in names if n not in GAMES]
    if unknown:
        raise SystemExit(f"unknown app(s): {', '.join(unknown)} "
                         f"(choose from: {', '.join(GAMES)})")
    for name in names:
        build(name)
    # Plain ASCII output on purpose: the Windows console garbles fancier.
    print("Send a zip to a friend - they extract it and double-click "
          "INSTALL-Windows.bat (Windows) or Install-macOS.command (macOS).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

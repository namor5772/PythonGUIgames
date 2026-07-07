=============================================================
 MyPocketTanks - a turn-based artillery duel
 (in the spirit of Pocket Tanks / Scorched Earth)
=============================================================

Two tanks trade shots across fully destructible terrain.
No health bars: every point of damage you deal is a point on
the scoreboard - and damaging yourself scores for your
opponent! Play against the computer (Easy / Normal / Hard)
or hotseat against a friend. 20 weapons, shifting wind, and
tanks you can drive on a limited fuel budget.

The whole game is one Python file - nothing to compile and
no packages to install. The only thing it needs is Python 3
itself (free, from python.org); the installers below check
for it and help you get it. Both installers are plain text
scripts - open them in any editor if you want to see exactly
what they do.


INSTALL ON WINDOWS
------------------
1. Extract this zip somewhere permanent, e.g. Documents
   (right-click the zip > Extract All). The Desktop shortcut
   will point INTO this folder, so don't delete the folder
   afterwards; if you move it later, just run the installer
   again.
2. Double-click  INSTALL-Windows.bat
   - If Windows SmartScreen says "Windows protected your
     PC", click "More info", then "Run anyway".
   - If Python is missing, the installer opens the download
     page for you: run the Python installer, TICK the
     "Add python.exe to PATH" box on its first screen, then
     double-click INSTALL-Windows.bat again.
3. Done - a "MyPocketTanks" shortcut with a tank icon is now
   on your Desktop. Double-click it to play.


INSTALL ON MACOS
----------------
1. Double-click the zip to unpack it, and put the
   MyPocketTanks folder somewhere permanent, e.g. your home
   folder. (The Desktop app will point INTO this folder; if
   you move it later, run the installer again.)
2. Double-click  Install-macOS.command
   - If macOS says it "cannot be opened because it is from
     an unidentified developer": right-click (Control-click)
     the file and choose Open, then Open again. On newer
     macOS you may instead need to go to System Settings >
     Privacy & Security, scroll down, and click "Open
     Anyway".
   - Still blocked? Open Terminal, type  bash  followed by
     a space, drag Install-macOS.command onto the window,
     and press Return.
   - If Python is missing, the installer sends you to
     python.org - the standard installer there includes
     everything the game needs. Then run this again.
3. Done - a "MyPocketTanks" app is now on your Desktop.
   Double-click it to play.


HOW TO PLAY
-----------
Choose DRAFT (you and your opponent alternate picking 10
weapons each from a pool of 20) or ONE WEAPON (both tanks
fire the same weapon for a settable 1-20 rounds). Each turn:

  Left / Right ..... turret angle    (hold Shift = x5)
  Up / Down ........ power           (hold Shift = x5)
  A / D ............ drive the tank  (uses limited fuel)
  [ and ] .......... change weapon
  Space or Enter ... FIRE!
  M ................ mute            Esc ... menu (asks)

Mind the wind arrow above the battlefield - it changes every
turn and bends every shot. Everything is also clickable with
the mouse, and every screen can be walked with Tab or the
arrow keys and pressed with Enter.


UNINSTALL
---------
Delete the MyPocketTanks folder and the Desktop shortcut
(Windows) or the MyPocketTanks app (macOS). Your settings
live in %APPDATA%\MyPocketTanks on Windows or ~/MyPocketTanks
on macOS - delete that folder too if you want every trace
gone.

License: GNU GPL v3 (see LICENSE.txt). Enjoy!

=============================================================
 MyTetris - the classic falling-blocks game
 (an accurate, guideline-faithful clone)
=============================================================

Stack the falling pieces, complete horizontal lines to clear
them, and don't let the stack reach the top. This clone plays
by the modern competitive rules: SRS rotation with wall
kicks, the 7-bag randomizer, ghost piece, hold, a next-3
preview, T-spin detection and back-to-back bonuses. Three
difficulties, an adjustable speed ramp, synthesized sound,
and your top-10 scores per difficulty are saved.

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
3. Done - a "MyTetris" shortcut is now on your Desktop.
   Double-click it to play.


INSTALL ON MACOS
----------------
1. Double-click the zip to unpack it, and put the MyTetris
   folder somewhere permanent, e.g. your home folder. (The
   Desktop app will point INTO this folder; if you move it
   later, run the installer again.)
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
3. Done - a "MyTetris" app is now on your Desktop.
   Double-click it to play.


HOW TO PLAY
-----------
On the menu, pick a difficulty (Easy / Normal / Hard), nudge
the SPEED ramp with the - / + buttons if you like gentler
speed-ups (it's remembered), and hit START. Then:

  Left / Right ..... move           Up or X ... rotate cw
  Down ............. soft drop      Z or Ctrl . rotate ccw
  Space ............ hard drop      C or Shift  hold piece
  P ................ pause          M ......... mute
  R ................ restart        Esc ....... menu (asks)

Clear multiple lines at once for big points - a Tetris (4
lines) or a T-spin scores best, and chaining them back-to-
back multiplies the bonus. Every menu is also clickable, or
walk the buttons with Tab / arrow keys and press Enter.


UNINSTALL
---------
Delete the MyTetris folder and the Desktop shortcut
(Windows) or the MyTetris app (macOS). High scores and
settings live in %APPDATA%\MyTetris on Windows or ~/MyTetris
on macOS - delete that folder too if you want every trace
gone.

License: GNU GPL v3 (see LICENSE.txt). Enjoy!

=============================================================
 Sun2Set - a sunrise / sunset almanac
 (not a game - a handy little astronomy app)
=============================================================

Type any place on Earth (latitude / longitude) and Sun2Set
computes sunrise, sunset and day length for today and every
day for a year ahead - with the compass direction of each
event (true bearings AND what a magnetic compass actually
reads, via the embedded World Magnetic Model), and how long
the sun's disc takes to cross the horizon. Results appear as
a graph you can hover for exact per-day readouts, and as a
text table you can save and load back later. It handles
daylight saving honestly, polar day/night, and even hills on
your horizon. Accuracy is typically within a minute or two
of published almanac values (NOAA equations).

The whole app is one Python file - nothing to compile and no
packages to install. The only thing it needs is Python 3
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
3. Done - a "Sun2Set" shortcut is now on your Desktop.
   Double-click it to start.


INSTALL ON MACOS
----------------
1. Double-click the zip to unpack it, and put the Sun2Set
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
     everything the app needs. Then run this again.
3. Done - a "Sun2Set" app is now on your Desktop.
   Double-click it to start.


HOW TO USE
----------
1. Type your latitude and longitude. Decimals (-34.2196),
   hemisphere letters (34.2196 S), degrees-minutes-seconds,
   or text pasted straight from Wikipedia / Google Maps all
   work - stray degree signs and odd minus signs are fine.
2. Choose the time zone handling: your computer's own zone
   (daylight saving applied day by day), or a fixed UTC
   offset like 10, -3.5 or 9:30 - optionally with manual
   daylight-saving rules, the way the law writes them
   ("1st Sun of Oct" to "1st Sun of Apr").
3. Click CALCULATE. The GRAPH tab draws sunrise, sunset and
   day length for the whole span - hover anywhere for that
   day's exact times, bearings and crossing durations. The
   TABLE tab shows the same data as text.
4. SAVE AS... writes the table to a file; LOAD... brings a
   saved file back - graph, table and all the settings that
   produced it.

Extras: the days field accepts 1-1500 (default 366) from any
start date; SKYLINE raises the horizon if you live among
hills (a single number like 5, or a profile like
60:2, 90:6, 240:8 by compass direction); THEME switches
between the dark and light look. Everything you type is
remembered for next time.


UNINSTALL
---------
Delete the Sun2Set folder and the Desktop shortcut (Windows)
or the Sun2Set app (macOS). Settings and the auto-saved
latest table live in %APPDATA%\Sun2Set on Windows or
~/Sun2Set on macOS - delete that folder too if you want
every trace gone.

License: GNU GPL v3 (see LICENSE.txt). Enjoy!

Valor: Medal of Honor (PlayStation, 1999) on a native engine

This package holds no game data. It needs your own disc image of Medal of Honor (USA): the .cue
and .bin made from your disc (Mode 2/2352). The game reads it where it is; nothing is copied.

First run: the game asks for the image (YOUR DISC). Type its path, or BROWSE to the .cue or .bin.
The choice is kept in the settings; OPTIONS > DISC changes it. From a command line:

  Linux:    ./valor.x86_64 -- --disc-image="/path/to/Medal of Honor (USA).cue"
  Windows:  valor.exe -- --disc-image="C:\path\to\Medal of Honor (USA).cue"

Controls (OPTIONS > CONTROLS rebinds them): W A S D move, the mouse turns and looks, the left
mouse button fires, the right one aims, Space jumps, C crouches, E uses and reloads, Q switches
weapons, Esc pauses.

Settings and saves live in the user folder (Linux: ~/.local/share/godot/app_userdata/Valor/;
Windows: %APPDATA%\Godot\app_userdata\Valor\), apart from any memory card saves you keep.

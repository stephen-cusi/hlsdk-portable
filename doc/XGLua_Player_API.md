# Player API

These server-side methods expose read-only HL1 player state. `GetWeapons()` returns a new dense array of valid weapon handles in item-slot order. `HasWeapon(classname)` compares the native entity classname exactly. `Name()` and `Nick()` both return the player net name. `Team()` returns the native `TeamID()` string, which is empty when the active rules do not assign one.

`Frags()` reads the native `pev->frags` score and may be fractional in unusual entities; `Deaths()` reads the player death counter. `IsBot()` uses the native `FL_FAKECLIENT` flag. `IsConnected()` means the entity is a live, initialized client slot in the current server VM.

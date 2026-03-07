#Work in Progress


This is a fork of the Snes9x project, designed to add a BS‑X clock override for increased Satellaview support.
The goal of this fork is to be able to alter the system time of the emulator to match the time saved to the Satellaview memory pack dump.
When loading Satellaview ROMs from within the BIOS (ＢＳ－Ｘ　それは名前を盗まれた街の物語), some games will not load because the system time is different from the time when the game was broadcasted via satellite and saved to the memory pack. 

This feature is already present in another emulator called bsnes-plus (https://github.com/devinacker/bsnes-plus), but I want to add it to Snes9x and more specifically the Snes9x libretro core because this will allow for more Satellaview games to be played on a raspberry pi running retropie.



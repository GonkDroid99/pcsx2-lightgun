# PCSX2 LightGun Edition

<<<<<<< HEAD
![Windows Build Status](https://img.shields.io/github/actions/workflow/status/PCSX2/pcsx2/windows_build_matrix.yml?label=%F0%9F%96%A5%EF%B8%8F%20Windows%20Builds)
![Linux Build Status](https://img.shields.io/github/actions/workflow/status/PCSX2/pcsx2/linux_build_matrix.yml?label=%F0%9F%90%A7%20Linux%20Builds)
![MacOS Build Status](https://img.shields.io/github/actions/workflow/status/PCSX2/pcsx2/macos_build_matrix.yml?label=%F0%9F%8D%8E%20MacOS%20Builds)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/1f7c0d75fec74d6daa6adb084e5b4f71)](https://app.codacy.com/gh/PCSX2/pcsx2/dashboard?utm_source=github.com&utm_medium=referral&utm_content=PCSX2/pcsx2&utm_campaign=Badge_Grade)
[![Discord Server](https://img.shields.io/discord/309643527816609793?color=%235CA8FA&label=PCSX2%20Discord&logo=discord&logoColor=white)](https://discord.com/invite/TCz3t9k)
=======
A custom build of PCSX2 made for LightGun Games
Only for guncon2 games, It will not solve the issue for Silent Scope.
Priorize US versions of games, EU will work, but some will have less features (recoil), miss some interesting patch...
<<<<<<< HEAD
>>>>>>> d3ab751c5 (update readme)
=======
GunCon must be configured in joystick mode using relative aiming.
>>>>>>> fad140d19 (update readme)

Game Specific notes :
For 2 player Vampire Night, i had issue with calibrating 2nd gun, so you need to boot with gun 2 off, and activate it once in game.
For endgame, remember that gun2 = player 1 and gun1 = player2
For RE Gun Survivor 2, you have to bypass the qualibration screen, don't calibrate the gun.

ChangeLog :
- MameHooker and Direct Gun4IR recoil support
- Aim Fix for few games (Resident Evil Survivor 2, Starsky & Hutch (E))
- 2 players Aim fix for Time Crisis 2 and Time Crisis 3
- Build in cheats and configuration to fix some issue, remove some gunflash...
- Some extra stuff like per game reshade profile, autoload savestate 10 on start, reduce the pause menu size to fill in 4:3 if it's the display mode...



<<<<<<< HEAD
<<<<<<< HEAD
## System Requirements

PCSX2 supports Windows, Linux, and Mac platforms. Our [setup documentation page](https://pcsx2.net/docs/setup/requirements) contains additional details on software and hardware requirements.

Please note that a BIOS dump from a legitimately-owned PS2 console is required to use the emulator. For more information, visit [this page](https://pcsx2.net/docs/setup/bios/).

## Contributing / Building

PCSX2 supports translation into other languages using [Crowdin](https://crowdin.com/project/pcsx2-emulator).

See the [Contribution Guide](https://pcsx2.net/docs/contributing/) for more info on how to contribute.
=======
>>>>>>> d3ab751c5 (update readme)
=======
About the per-game reshade fix.
You need to have a default profile nammed DefaultReshadePreset (and so, a DefaultReshadePreset.ini in your pcsx folder).
Game specific profile must be nammed with the game serial, like SCES-50300.ini
Can be use to apply per game bezel or use certain effect on a game basis, like for exemple, i use EyeAdaption shader to reduce the gun flash of game that do not have a patch to remove it.
(https://github.com/brussell1/Shaders/blob/master/Shaders/EyeAdaption.fx)

Other stuff related to pcsx2 : 
I've made a no-smoke patch for Time Crisis 2 if you want, i do not include it by default, because the smoke is not a bug, so it's up to personal preference.
https://github.com/nixxou/pcsx2/releases/download/V1.0/optional_NOSMOKE-Patch-TimeCrisis2.zip

Don't forget there is HD texture pack for some of the games : 
Resident Evil Dead Aim : https://gbatemp.net/threads/resident-evil-dead-aim-hd-textures.649199/
Vampire Night : https://gbatemp.net/threads/vampire-night-hd-texture-pack.643864/
Time Crisis Crisis Zone : https://gbatemp.net/threads/time-crisis-crisis-zone-hd-texture-pack.643871/
Time Crisis 2 : https://gbatemp.net/threads/my-ps2-packs.621422/
>>>>>>> fad140d19 (update readme)

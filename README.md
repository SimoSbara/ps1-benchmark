# ps1-benchmark

## Description

A PS1 benchmark created with [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK).

This benchmark is created for testing PS1 capabilities: graphics, commands and audio.

### Testing
This program is tested on [DuckStation](https://www.duckstation.org/) PS1 emulator.

If you want to test it on a real PS1, you will need to mod it with a [chip](https://quade.co/ps1-modchip-guide/) or using a [modded memory card](https://github.com/brad-lin/FreePSXBoot).

## Burning CD
### Little Side Note
You can't use CD-RW because the PS1 laser reader cannot read it, use only good CD-R (Verbatim or Sony).

Download the latest release and burn .iso into a CD using any CD burner software.

## Development
This project is currently in development on Ubuntu (using WSL), Windows is not tested.

Steps for setting up environment:
* download [last PSn00bSDK release](https://github.com/Lameguy64/PSn00bSDK/releases) 
* open ```sudo nano ~/.bashrc```
* add in the end of the file ```export PS1_SDK_ROOT=/path/to/PSn00bSDK/root/folder```
* ```source ~/.bashrc```
* clone the repository ```https://github.com/SimoSbara/ps1-benchmark.git```
* execute cmake ```cmake --preset default .```

Now you can build the project with ```cmake --build ./build```

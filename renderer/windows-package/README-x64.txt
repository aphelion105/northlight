NORTHLIGHT RENDERER __RELEASE_VERSION__ FOR WINDOWS x64

Extract the archive outside the game folder. Close WoW and run Install.cmd.
Select the folder containing the AMD64 WowClient64.exe and the 3.3.5a Data
archives. The installer backs up files it replaces and can build the world
cache from the local archives. It shows Your game, Your choices, Review and
Install. Press Enter at Review to apply the changes. Updating keeps your
settings, edited appearance presets and dungeon choices. Use --no-tideglass
to omit the water core. It leaves the game executable unchanged.

Run WowClient64.exe -d3d after installation. The Windows x86 client needs a
separate game folder if you want to keep both architectures installed.

The installer uses the packaged AMD64 DXVK by default. Install.cmd --backend
dxvk2 selects the other bundled DXVK version. Use Uninstall.cmd to restore
the original files. For options, run Install.cmd --help.

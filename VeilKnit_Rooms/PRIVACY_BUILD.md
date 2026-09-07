# Privacy-hardened Rooms builds

## Windows

Run `Desktop/scripts/Build-Private-Release.ps1`. Release C++ compilation remaps the source root to `/_/veilknit-rooms`, uses reproducible linker mode, and does not require distributing PDB files.

## Android

Create an untracked `Android/local.properties` or configure `ANDROID_HOME`, then run `Android/Build-VeilKnit-Rooms-Release-Private.bat` or `Android/build-release-private.sh`. R8 minification/shrinking is enabled and source-file attributes are renamed. Keep signing keys outside the repository.

## Linux

Run `Linux/build_all.sh`. GCC source/debug/macro prefixes are remapped and final executables are stripped.

## Audit

Use `scripts/audit-release-metadata.ps1` or `.sh` against the final release directory, adding your actual username, full name, hostname, and build path as tokens.

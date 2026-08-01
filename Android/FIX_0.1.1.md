# VeilKnit Rooms Android 0.1.1 build fix

Changes:

- Removed the direct `androidx.compose.foundation.layout.weight` import. `weight` is supplied by the active Row/Column scope; the explicit import resolves to an internal Compose symbol with the current toolchain.
- Pinned `androidx.core:core-ktx` to `1.17.0`, which is compatible with the project's Android API 36.1 compile SDK.
- Bumped the Android app version to `0.1.1-android` / version code 2.

Rebuild with:

```bat
Build-VeilKnit-Rooms-Android.bat
```

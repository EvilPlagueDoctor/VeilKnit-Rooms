# Validation record

Validation date: 2026-07-22

## Native Windows build evidence

The first Visual Studio 2022 build supplied by the project owner successfully:

- compiled the Rust daemon in release mode;
- configured the Windows 11 SDK and MSVC 19.44 toolchain;
- compiled the VeilKnit C++ SDK;
- compiled `veilknit_rooms_core`;
- built and ran `veilknit_rooms_core_tests`;
- passed 100% of the core tests.

The GUI target then stopped in `win32_app.cpp` because Win32 `RECT` members are
`LONG`, while several `std::max` calls mixed them with `int`. MSVC cannot deduce
a common template type in that situation. Release 0.1.1 changes those calls to
explicit `int` calculations. It also:

- converts child-control IDs through `INT_PTR` before converting to `HMENU`;
- guards the source-level `NOMINMAX` definition;
- removes lossy `wchar_t`-to-`char` fallback construction in `util.cpp`.

A follow-up native Windows build of 0.1.1 has not yet been returned, so final
Win32 compilation and linking remain to be confirmed.

## Portable C++ build after the fix

Commands executed:

```text
cmake -S . -B out-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DVKROOMS_BUILD_TESTS=ON
cmake --build out-linux
ctest --test-dir out-linux --output-on-failure
```

Result:

```text
100% tests passed, 0 tests failed out of 1
```

The following targets compiled with GCC 14.2 in C++17 mode:

- `veilknit_cpp`
- `veilknit_rooms_core`
- `veilknit_rooms_platform_smoke`
- `veilknit_rooms_core_tests`

## Covered by automated tests

- SHA-256 known-answer vector
- Room key derivation and encrypted payload round trip
- `VKROOM1` invite creation and parsing
- JSON room/member/message persistence
- Room engine startup, demo room initialization, snapshot, and shutdown

## Recommended Windows verification

After replacing the 0.1.0 source with 0.1.1, avoid rebuilding the unchanged
daemon while testing the GUI correction:

```powershell
.\scripts\Build-All.ps1 -Configuration Release -SkipDaemon
```

# Building VeilKnit Rooms and the integrated daemon GUI

## Prerequisites

Install on Windows 10 or Windows 11:

1. Visual Studio 2022 Community or newer
2. **Desktop development with C++** workload
3. MSVC v143 x64/x86 tools
4. Windows 10 or 11 SDK
5. C++ CMake tools for Windows
6. Stable Rust MSVC through `rustup`

```powershell
winget install Rustlang.Rustup
rustup default stable-msvc
```

Restart Developer PowerShell afterward.

## One-command build

From **Developer PowerShell for VS 2022**:

```powershell
cd path\to\VeilKnit-Rooms
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\Build-All.ps1 -Configuration Release
```

The first GUI-daemon build downloads and compiles the `eframe` desktop stack and
therefore takes longer than later incremental builds.

The script performs:

1. release build of the Rust GUI daemon;
2. CMake generation with the `vs2022-x64` preset;
3. release build of `VeilKnitRooms.exe` and its SDK;
4. room-core tests;
5. assembly under `dist\Release`.

Expected output:

```text
dist\Release\
    VeilKnitDaemon.exe
    VeilKnitRooms.exe
    README.md
    USER_GUIDE.md
    CURRENT_LIMITATIONS.md
```

## Build only the daemon

```powershell
cargo build --manifest-path .\daemon\Cargo.toml --release
```

Raw Cargo output:

```text
daemon\target\release\veilid_test_node.exe
```

This executable is already the integrated GUI daemon. The assembly script only
renames its copied form to `VeilKnitDaemon.exe`.

## Build only VeilKnit Rooms

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-release
ctest --preset vs2022-release
```

Output:

```text
out\build\vs2022-x64\Release\VeilKnitRooms.exe
```

To rebuild only the C++ application after the daemon has already been built:

```powershell
.\scripts\Build-All.ps1 -Configuration Release -SkipDaemon
```

## First-run application authorization

1. Start `VeilKnitDaemon.exe` and log in.
2. Start `VeilKnitRooms.exe`.
3. Select **Applications** in the daemon.
4. Review the requested capabilities.
5. Press **Approve** or **Deny**.

The equivalent old console commands are still available in the Console tab:

```text
app-pending
app-approve <request-id>
app-reject <request-id> <reason>
```

## Common build issues

### Cargo cannot find a linker

Use the MSVC Rust toolchain and run from Developer PowerShell:

```powershell
rustup default stable-msvc
```

### `windows.h` is missing

Install the Windows SDK and Desktop development with C++ workload.

### An old daemon launcher is still present

`VeilKnitDaemonGui.exe` belonged to the temporary wrapper design and is no
longer built. Delete old copies from `dist` if needed. The GUI is now inside
`VeilKnitDaemon.exe` itself.

### The room app cannot find the endpoint

Log into the daemon first and run both programs as the same Windows user. Avoid
running one elevated and the other unelevated while testing endpoint discovery.

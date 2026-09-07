# VeilKnit Rooms 0.1.1 release notes

This maintenance release corrects the first issues found by a native Visual
Studio 2022 build of the Win32 interface.

## Corrected

- Fixed three MSVC errors caused by mixing Win32 `LONG` values with `int` in
  `std::max` calls.
- Used explicit integer typing for header and message layout calculations.
- Converted Win32 child-control identifiers through `INT_PTR`, removing 64-bit
  `HMENU` cast warnings.
- Guarded `NOMINMAX` so the source does not redefine the CMake-provided macro.
- Replaced lossy Unicode fallback conversions with Windows API conversions
  using the active code page when malformed UTF-8/UTF-16 is encountered.

## Unchanged

The Rust daemon, protocol-v1 C++ SDK, room protocol, persistence format, and
room-engine behavior are unchanged from 0.1.0. Existing local room data and
credentials remain compatible.

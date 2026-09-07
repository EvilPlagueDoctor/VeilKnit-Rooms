# Validation

The SDK was compiled as C++17 with GCC 14.2 and strict warnings. The library,
`veilknit_hello`, `veilknit_room_primitives`, and `veilknit_codec_tests` all
built successfully. CTest passed.

A mock local daemon also verified a complete protocol-v1 authentication flow:
challenge parsing, capability ordering, HMAC-SHA256 proof construction,
session parsing, and an authenticated identity request.

Visual Studio 2022 was not available in the build environment. The included
`vs2022-x64` CMake preset still needs a native Windows build verification.

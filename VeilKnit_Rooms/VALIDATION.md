# Validation status

Completed checks:

- The shared Linux/Windows C++ core configured and compiled with C++17 on Linux.
- OpenSSL 3 was found and the production AES-256-GCM path was compiled.
- The core test suite passed: 100% tests passed.
- The Linux console executable launched and exited normally; daemon connection failure was expected because no daemon was running during the test.
- The Linux GTK source passed a standalone C++ syntax check. A real GTK-linked build was not available in the preparation environment.
- Android Gradle/XML and Windows Visual Studio project XML parse structurally.
- All shell scripts pass `bash -n`.
- The stripped Linux console artifact passed the included private-path metadata scanner.
- The Gradle wrapper JAR is present and its SHA-256 checksum is recorded beside it.
- Generated build directories, credentials, user data, and machine-specific Android configuration are excluded.

Not completed in the preparation environment:

- Android Gradle build.
- Windows Visual Studio build.
- Real GTK-linked GUI build.

Run all target-platform builds and `scripts/audit-release-metadata.*` before publishing release binaries.

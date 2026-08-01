# Validation notes

## Checked in this environment

- All Android XML resources and manifests parse successfully.
- All PNG/WebP resources open and validate.
- The Rooms and daemon AIDL definitions are identical.
- The daemon Binder service is protected by an Android `signature` permission.
- The Binder proxy forwards the daemon's existing protocol-v1 newline-delimited JSON API rather than bypassing authentication or capabilities.
- Protocol-v1 HMAC input follows the Rust implementation's little-endian field layout and preserves the daemon-returned capability ordering.
- Room key derivation, AES-256-GCM associated data, URL-safe Base64 encoding, canonical sorted-key JSON, signing domains, and `VKROOM1:` invite fields match the desktop implementation.
- Public DHT history reads are split into small groups to remain below Android Binder transaction limits.
- Generated build/cache directories and local machine configuration are excluded from the release archives.

## Validation boundary

This environment does not contain the Android SDK/NDK or a usable Gradle dependency cache, and external dependency downloads are unavailable. Consequently, the first full Android Gradle build must be performed in Android Studio Panda on the user's development computer.

The source was also passed through the local Kotlin compiler as a syntax-oriented check. Android, Compose, generated AIDL, coroutines, and `org.json` references cannot resolve without the Android/Gradle classpath, so that is not a substitute for an Android Studio build.

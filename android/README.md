# PaperOS Companion (Android)

Companion app for the PaperOS inbox. Runs on the user's phone (Pixel 9 Pro, Android 17), reads
WhatsApp and Proton Mail notifications, and will later send them end to end encrypted through Home
Assistant to the PaperS3 (HA relays ciphertext only). Decision and open questions: `docs/DECISIONS.md`
and the P6 cards in `docs/ROADMAP.md`.

## v0 (P6-C0)

- `CaptureListener` (`NotificationListenerService`) keeps notifications from `com.whatsapp`,
  `com.whatsapp.w4b` and `ch.protonmail.android` only: app, title, text, post time, notification key,
  and whether a reply action (RemoteInput) exists. Group summaries are skipped.
- Items live in memory and in app-private storage: `files/items.json` (contents, at most 200, latest per
  live notification key) and `files/counts.json` (numbers only per app: posted, updated, removed, held,
  plus listener connected). Removals (read or dismissed on the phone) drop the item; on reconnect, items
  whose notification went away meanwhile are dropped too.
- One screen: access granted or not, a button to the notification-access settings, App info (for
  "Allow restricted settings"), counts per app, phone key, upload queue, pairing. It never shows contents.
- No network permission, no HA, no accessibility service. Backup and device transfer are off.

## v1 groundwork (spec-independent; the wire format comes from `docs/COMPANION.md`)

- `KeyVault`: the phone's X25519 key pair. Generated inside the Android Keystore (`XDH`,
  `PURPOSE_AGREE_KEY`); agreement runs there, the private key never leaves it. Measured on the Pixel 9
  Pro, API 37: Keystore X25519 works, security level TEE. Fallback when a phone cannot do that: a
  software key, PKCS#8 encrypted with a Keystore AES-256-GCM key, in `noBackupFilesDir`. All-zero
  shared secrets are rejected. Alias `paperos_phone_x25519`.
- `Hkdf`: HKDF-SHA256 (RFC 5869). ChaCha20-Poly1305 comes from the platform (Conscrypt,
  `ChaCha20/Poly1305/NoPadding`); no crypto library needed.
- `ScanActivity` + `QrDecode`: QR scanner, CameraX frames decoded with ZXing core. No Google Play
  services. Returns the text in memory only; pairing does nothing with it yet.
- `Outbox` + `Drain` + `UploadWorker`: durable FIFO of sealed payloads in `noBackupFilesDir/outbox`
  (cap 1000 items, 64 KiB each, oldest dropped), drained by WorkManager when the network is up, with
  exponential backoff. `Relay` is a stub (`NOT_CONFIGURED`) until the HA webhook relay is specified.
  Only sealed payloads may go in; nothing feeds it yet.

## Tests

```bash
cd android && ./gradlew :app:testDebugUnitTest
```

JVM: HKDF RFC 5869 vectors, QR decode (padded rows, short last row, inverted), outbox order and cap,
drain outcomes. On the phone (Keystore X25519, wrapped fallback, low-order peer, RFC 7748 vector,
ChaCha20-Poly1305, HKDF): build both APKs, install, run with `am instrument`, then remove the test APK.
Do not use `connectedAndroidTest`: it uninstalls the app afterwards, which drops the notification-access
grant and the phone key.

```bash
cd android && ./gradlew :app:assembleDebug :app:assembleDebugAndroidTest
```

```bash
adb install -r android/app/build/outputs/apk/debug/app-debug.apk && adb install -r android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
```

```bash
adb shell am instrument -w nl.paperos.companion.test/androidx.test.runner.AndroidJUnitRunner && adb uninstall nl.paperos.companion.test
```

Notification contents are the user's private messages: never log, print or commit them. To check the
app works, read `counts.json` only:

```bash
adb shell run-as nl.paperos.companion cat files/counts.json
```

## Build and install

Needs JDK 17 and the Android SDK (`~/Library/Android/sdk`; `local.properties` points at it and is not
committed).

```bash
cd android && ./gradlew :app:assembleDebug
```

```bash
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

```bash
adb shell am start -a android.settings.ACTION_NOTIFICATION_LISTENER_SETTINGS
```

Sideloaded apps on Android 13+ get "restricted settings": if the access switch is greyed out, open
App info > ⋮ > Allow restricted settings first, then grant notification access.

Check the listener is bound:

```bash
adb shell dumpsys notification | grep -A3 -i "nl.paperos.companion"
```

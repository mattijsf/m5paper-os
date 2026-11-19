# PaperOS Companion (Android)

Companion app for the PaperOS inbox. Runs on the user's phone (Pixel 9 Pro, Android 17), reads
WhatsApp and Proton Mail notifications, and will later send them end to end encrypted through Home
Assistant to the PaperS3 (HA relays ciphertext only). Decision and open questions: `docs/DECISIONS.md`
and the P6 cards in `docs/ROADMAP.md`.

## What it does (v1, protocol `docs/COMPANION.md`)

- `CaptureListener` (`NotificationListenerService`) keeps notifications from `com.whatsapp`,
  `com.whatsapp.w4b` and `ch.protonmail.android` only; group summaries are skipped. While paired, every
  post or update goes to the Paper as an encrypted `post` (WhatsApp: MessagingStyle messages, at most 6;
  Proton Mail: title = sender, text = subject, big text = preview), every removal as `remove`
  (click or app cancel = `read`, user swipe = `dismissed`), and a `sync` of the live keys after every
  (re)connect and hourly. Contents go straight into envelopes; nothing keeps titles or texts.
- `proto/`: the protocol in plain JVM code (key schedule, envelopes, QR payload, receive policy,
  messages, reply guards), tested byte for byte against `tests/fixtures/companion/vectors.json`.
- `KeyVault`: the phone's X25519 key, made inside the Android Keystore (TEE on the Pixel 9 Pro, API 37)
  per pairing; agreement runs there. Session keys are derived again in memory after a restart.
- `TokenStore`: the Home Assistant long-lived token of a dedicated non-admin user, pasted by the user,
  AES-256-GCM encrypted with a Keystore key in `noBackupFilesDir`. Never logged, never shown.
- `Link`: pairing state and counters (`noBackupFilesDir/pairing.json`, no secrets). Pairing: scan the
  Paper's QR (`ScanActivity`, CameraX + ZXing, no Play services), the hello goes to the `pairing`
  queue, both screens show the 6-digit code, the user taps "The codes match", and the app becomes active
  only after that **and** a valid `paired` envelope. HTTPS only (cleartext traffic is off).
- `RelayService`: foreground service (type `remoteMessaging`). Drains the `Outbox` (pushes in counter
  order, back-off 30 s doubling to 15 min), reads `sensor.paperos_relay_to_phone` every 3 s while
  pairing and every minute when paired, acks up to `rx`. Writes `files/status.json` (numbers only).
- Maintenance (D40, COMPANION.md section 10): `FirmwareActivity` sends a firmware package signed on the Mac
  (`make ota-phone` pushes `paperos.posota` to Downloads; picked with the system file picker) to the Paper as `make ota`
  does; the Paper checks the signature. `RemoteActivity`, Remote control: the Paper's screen, and taps, holds and
  drags as its touch, over the home Wi-Fi while the Paper's wireless development is on. A plain TCP socket to port
  8266 (not HTTP: the cleartext switch does not apply), signed in with `HKDF-Expand(k_p2d, "PaperOS remote")`; the
  Paper allows the screen and touch only. The address comes from the Paper's `remote` message or is typed.
- Replies from the Paper go through the notification's RemoteInput action, after the guards of section
  7.2 (live key with reply action, 6 h age, 1..900 characters, 10 s gap, 20 per hour; `rx` is stored
  before the send, so never two sends per counter). **Replies are off until the user turns on "Allow
  replies from the Paper" in the app**; while off, a reply is answered with `result` `why: "failed"`.
  `dismiss` does nothing on the phone (v1).
- Agenda (D28, `AgendaSync`, `proto/Agenda`): with "Share the agenda" on (off by default; asks for `READ_CALENDAR`), the
  visible calendars' next 14 days go to the Paper as `agenda` runs, one way: a changed day's run a minute after the change, the
  whole window daily and after pairing; switching off sends one empty run. Only fingerprints are kept (`agenda.json`).
- Clipboard (D23, `ClipSync`): a `clip` from the Paper replaces the phone's clipboard (label `PaperOS`). The other way only on
  the user's action, since Android lets an app read the clipboard only while it has focus: the share sheet target "Send to
  Paper" (`ShareToPaperActivity`), the Quick Settings tile "Clipboard to Paper" (`ClipTileService` opens `ClipToPaperActivity`,
  which reads on window focus) and the app's button. A clipboard marked sensitive is never sent. Switch "Share the clipboard with
  the Paper" (default on, `settings.json` `clipboard`).
- Look: the PaperOS "Ink" style (`Ink.kt`, docs/UI.md): the panel's 16 grays from its warm paper (`#E4E4DF`, measured on photos
  of the PaperS3) to its ink (`#1E1E1E`), Inter and Lucide icons (`res/font`, licences in `assets/licenses`), list rows with
  dividers, ink and outline buttons, the Paper's pill toggle; light only, edge to edge with the system-bar insets.
- Launcher icon: the PaperOS mark, a P whose bowl is the Paper (a sheet with a folded corner) and whose counter is its page of
  text, ink on paper. Adaptive icon from vector drawables only (`res/mipmap-anydpi/ic_launcher.xml`,
  `res/drawable/ic_launcher_foreground.xml`, `ic_launcher_monochrome.xml` for themed icons, background `ic_launcher_background`
  in `res/values/colors.xml`); the mark is 34 x 52 of the 108 box, inside the 66 dp safe zone.
- Backup and device transfer are off. No accessibility service.

Notification contents are the user's private messages: never log, print or commit them. Check the app
with numbers only:

```bash
adb shell run-as nl.paperos.companion cat files/counts.json
```

```bash
adb shell run-as nl.paperos.companion cat files/status.json
```

`status.json`: `stage`, `outbox` (waiting), `pushed`, `accepted`, `dropped`, last HTTP codes, and the
shape of the relay queues after a push (`to_paper.n`, `max_len`, `max_id`; `pairing.n`, `len`).

## Tests

```bash
cd android && ./gradlew :app:testDebugUnitTest
```

JVM: the shared companion vectors (keys, code, hello, post, reply, rejects), receive policy, messages and
their size limits, reply guards, HKDF RFC 5869, QR decode, outbox and drain. On the phone (Keystore
X25519, wrapped fallback, low-order peer, RFC 7748, ChaCha20-Poly1305, the companion vectors through
Conscrypt; `RemoteDeviceTest`: remote control over the real Wi-Fi against the Mac's simulator with the vectors' pairing,
`-e remoteSim <mac address>:8266`): build both APKs, install, run with `am instrument`, then remove the test APK.
`RemoteTest` also runs against a simulator on the Mac: `./gradlew :app:testDebugUnitTest -Premote.sim=localhost:8266`.
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

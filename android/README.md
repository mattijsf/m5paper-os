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
  key) and `files/counts.json` (numbers only: posted per app, held per app, listener connected).
- One screen: access granted or not, a button to the notification-access settings, App info (for
  "Allow restricted settings"), counts per app. It never shows contents.
- No network permission, no HA, no crypto, no accessibility service. Backup and device transfer are off.

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

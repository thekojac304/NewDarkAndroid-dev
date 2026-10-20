# Signing strategy

## Today

Only **debug** APKs are built. They are signed with the machine's standard Android debug key
(`CN=Android Debug`; certificate SHA-256 `6dc6baad…d9924` on the maintainer's PC), v2 scheme only (enough for
`minSdk 26`). There is **no release build type, no signing configuration, and no private key anywhere in the repository
or its history** (scanned: no keystore/`.jks`/`.pem` files, no `storePassword`/`PRIVATE KEY` strings).

Debug-signed APKs update in place only when signed by the same debug key; moving to another PC, or later to a release key,
means **uninstalling first, which deletes the app's saves**. See "Upgrade path" below.

## Recommendation for v0.1.0-beta.1

**Publish source only, so nothing needs a signing key now.** Each player builds and debug-signs their own APK; this
also matches the rights position (nobody may distribute the generated game code).

## If a signed build is ever authorized

Only relevant for an APK that is legally redistributable (for example a launcher-only build, or a build the rights
holders authorize). Then:

1. Generate a **dedicated upload/release keystore offline**, by the maintainer, never inside the repo or a CI log:
   `keytool -genkeypair -v -keystore newdarkandroid-release.jks -alias release -keyalg RSA -keysize 4096 -validity 10000`
   (or an EC P-256 key); back it up in two places (password manager + offline media). Losing it means users must
   uninstall to update.
2. Keep key and passwords **outside** the working tree; reference them from `~/.gradle/gradle.properties` or from
   environment variables (`NEWDARK_KEYSTORE`, `NEWDARK_KEYSTORE_PASSWORD`, `NEWDARK_KEY_ALIAS`, `NEWDARK_KEY_PASSWORD`)
   in a `signingConfigs.release` block that is skipped when they are absent. `.gitignore` already blocks `*.jks`,
   `*.keystore`, `*.p12`, `*.pem`, `*.key`, `keystore.properties`.
3. Add a `release` build type (`minifyEnabled false`; there is no Java code worth shrinking) with `debuggable false`;
   build `assemblePlayRelease`; verify with `apksigner verify --print-certs -v` and `zipalign -c -P 16 -v 4`.
4. Publish the **certificate SHA-256** in the release notes so players can verify, next to the file's SHA-256.
5. Use Android's v3 key-rotation lineage if the key must ever change; do not re-sign with a new unrelated key.
6. Consider a separate application ID for any public build (see below).

## Upgrade path and saves

- Same application ID and the same signing key: updating in place keeps `files/`, `shared_prefs/` and external
  `Android/data/.../files/` (verified for every phase on the test phone: 345 save files byte-identical after the 3G
  update).
- `allowBackup="false"`: Android's cloud backup/auto restore does not copy saves.
- Changing the signing key (debug to release) **requires uninstall/reinstall** and therefore loses the saves unless they
  are copied out first (adb pull of `.../files/profiles`, or `run-as` on debug builds).
- `applicationId` is currently `local.newdarkandroid.ss2`, a placeholder-style ID. Before any wider distribution decide
  whether to keep it (existing testers keep their data) or move to a domain you control. Changing it is a new app.
- `versionCode 17`, `versionName 0.1.0-beta.1` (was 16 / `0.9.3-polish` during development). versionCode must keep rising: a
  lower one is refused as a downgrade on any phone that has an earlier build.

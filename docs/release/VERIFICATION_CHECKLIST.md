# Release verification checklist (v0.1.0-beta.1)

Run before anyone pushes or publishes. Items 1 to 8 are repeatable from the candidate folder; the maintainer ticks them.

1. [ ] `release-candidate/SHA256SUMS.txt` verifies (`sha256sum -c`), and `CANDIDATE.json` names the commit you intend to push.
2. [ ] `git log --stat origin/main..public/v0.1.0-beta.1` shows **one** commit; its tree has no path from `PUBLICATION_DENYLIST.txt`.
3. [ ] `python tools/release/make_candidate.py` passes its scans (no private strings, keys, binaries, files over 1 MB).
4. [ ] Search the extracted source archive for: your user name, device serial, `C:\Users`, `.apk`, `.so`, `.exe`, `.osm`,
       `.crf`, `gen/`. Expect none (the Gradle wrapper jar is the only binary).
5. [ ] `README.md`, `LICENSING.md`, `THIRD_PARTY_NOTICES.md`, `LICENSE`, `LICENSES/GPL-3.0.txt`,
       `video/ffmpeg/COPYING.LGPLv2.1` are present.
6. [ ] A fresh clone of the public commit builds an APK against your own game (see the readiness report for the last run).
7. [ ] No screenshot shows game footage or the game's UI. (`docs/images/launcher.png` shows only the app's launcher.)
8. [ ] The GitHub description, topics and release notes carry the "unofficial, not affiliated" statement; the release
       has **no attached APK or executable**.
9. [ ] Owner decisions recorded: license of the original app icon; contributions under MIT confirmed; GPL-3.0-or-later for
       `port/backends/android/touch/`; repository name; whether to keep application ID `local.newdarkandroid.ss2`.
10. [ ] Push only the **public branch** (`public/v0.1.0-beta.1:main` of the new repo). Never push `android-port`,
        `android-port-audit` or the release-prep branch: their history contains private identifiers and game screenshots.

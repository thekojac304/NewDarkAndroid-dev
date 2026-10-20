#!/usr/bin/env python3
"""Build the local public-release candidate. Read-only on the working tree; pushes nothing; uploads nothing.

  python tools/release/make_candidate.py [--rev HEAD] [--base origin/main] [--branch public/v0.1.0-beta.1] [--out release-candidate]

1. Takes the tree at --rev, removes everything on the DENY list (development evidence, anything that could carry
   private data or game-derived material), and checks the result against the safety scans below.
2. Writes that tree as ONE commit whose parent is --base (the already-public upstream history), on a LOCAL branch.
   The development history (android-port) is never part of it: it contains device serials, local paths and game
   screenshots. Nothing is rewritten; the old branches are untouched.
3. Writes the candidate folder: source zip + tar of the public tree, docs, license notices, checksum manifest.
Exit status is non-zero if a scan fails; no branch or folder is written then.
"""
import argparse, hashlib, io, os, re, subprocess, sys, tarfile, zipfile, json, tempfile, time

DENY = [  # regexes on repo-relative paths, matched with re.search
    r'^docs/android-audit/[^/]+/',               # device evidence: game screenshots, logs, save manifests, driver scripts
    r'(^|/)build/', r'(^|/)out/', r'(^|/)gen/',  # generated, game-derived
    r'\.(apk|aab|so|a|o|exe|dll|osm|crf|mis|gam|sav|avi|mpg|7z|zip|jks|keystore|p12|pem|key)$',
    r'(^|/)local\.properties$', r'(^|/)keystore\.properties$',
]
ALLOW_BINARY = {'android/gradle/wrapper/gradle-wrapper.jar'}   # Apache-2.0 Gradle wrapper
BS = chr(92)   # a backslash, spelled out so this file does not match its own scan
PRIVATE = re.compile('|'.join(['law' + 'ma', 'R3GL' + '104LWJK', 'thekoj' + 'ac', '[A-Za-z]:' + BS * 2 + '(Users|Dev)' + BS * 2,
                               '/Users/[a-z]', 'Husky' + 'Box', 'Launchbox[' + BS * 2 + '/]']), re.I)
SECRET = re.compile(r'BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY|(store|key)Password\s*[=:]\s*\S|AKIA[0-9A-Z]{16}')
TEXT_EXT = ('.md', '.txt', '.py', '.c', '.h', '.java', '.gradle', '.xml', '.json', '.sh', '.bat', '.properties', '.cmake',
            '.inc', '.s', '.sym', '.sum', '.sha256', '.gitignore')
MAX_FILE = 1_000_000


def git(*a, inp=None, env=None):
    r = subprocess.run(['git', *a], input=inp, capture_output=True, env=env)
    if r.returncode: sys.exit('git %s: %s' % (' '.join(a), r.stderr.decode(errors='replace')))
    return r.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rev', default='HEAD'); ap.add_argument('--base', default='origin/main')
    ap.add_argument('--branch', default='public/v0.1.0-beta.1'); ap.add_argument('--out', default='release-candidate')
    ap.add_argument('--message', default='NewDarkAndroid v0.1.0-beta.1: source snapshot')
    a = ap.parse_args()
    root = git('rev-parse', '--show-toplevel').decode().strip(); os.chdir(root)
    rev = git('rev-parse', a.rev).decode().strip(); base = git('rev-parse', a.base).decode().strip()
    paths = git('ls-tree', '-r', '-z', '--name-only', rev).decode().split('\0')[:-1]
    keep, denied, problems = [], [], []
    for p in paths:
        if p not in ALLOW_BINARY and any(re.search(d, p) for d in DENY): denied.append(p); continue
        keep.append(p)
    for p in keep:
        blob = git('cat-file', 'blob', '%s:%s' % (rev, p))
        if len(blob) > MAX_FILE and p not in ALLOW_BINARY: problems.append('%s: %d bytes' % (p, len(blob)))
        if p.lower().endswith(TEXT_EXT) or p.startswith('LICENSE'):
            t = blob.decode('utf-8', 'replace')
            if m := PRIVATE.search(t): problems.append('%s: private string %r' % (p, m.group(0)))
            if m := SECRET.search(t): problems.append('%s: secret-like %r' % (p, m.group(0)))
        elif b'\0' in blob[:4096] and not p.lower().endswith(('.png', '.jpg')) and p not in ALLOW_BINARY:
            problems.append('%s: unexpected binary' % p)
    if problems:
        print('SCAN FAILED:'); [print('  ', x) for x in problems]; sys.exit(1)
    # temporary index -> tree -> commit (the working tree and the real index are never touched)
    with tempfile.TemporaryDirectory() as td:
        env = dict(os.environ, GIT_INDEX_FILE=os.path.join(td, 'idx'))
        git('read-tree', rev, env=env)
        if denied:   # --cached: only the temporary index changes
            git('rm', '--cached', '-q', '--ignore-unmatch', '--pathspec-from-file=-', '--pathspec-file-nul',
                inp=('\0'.join(denied) + '\0').encode(), env=env)
        tree = git('write-tree', env=env).decode().strip()
    commit = git('commit-tree', tree, '-p', base, '-m', a.message + '\n\nCo-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>').decode().strip()
    git('branch', '-f', a.branch, commit)
    # candidate folder
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    short = commit[:12]
    tarp = os.path.join(out, 'NewDarkAndroid-v0.1.0-beta.1-source.tar.gz'); zipp = os.path.join(out, 'NewDarkAndroid-v0.1.0-beta.1-source.zip')
    git('archive', '--format=tar.gz', '--prefix=NewDarkAndroid-v0.1.0-beta.1/', '-o', tarp, commit)
    git('archive', '--format=zip', '--prefix=NewDarkAndroid-v0.1.0-beta.1/', '-o', zipp, commit)
    docs = os.path.join(out, 'docs'); os.makedirs(docs, exist_ok=True)
    for p in ['README.md', 'LICENSE', 'LICENSING.md', 'THIRD_PARTY_NOTICES.md', 'LICENSES/GPL-3.0.txt',
              'docs/android/BUILDING.md', 'docs/android/CONTROLS.md', 'docs/android/GAME_FILES.md', 'docs/android/MODS.md',
              'docs/android/TROUBLESHOOTING.md', 'docs/android/KNOWN_LIMITATIONS.md', 'docs/release/RELEASE_NOTES_v0.1.0-beta.1.md',
              'docs/release/VERIFICATION_CHECKLIST.md', 'docs/release/GITHUB_TEXT.md', 'docs/release/SIGNING.md',
              'docs/release/V0.1.0_BETA1_RELEASE_READINESS.md', 'docs/release/PUBLICATION_ALLOWLIST.txt', 'docs/release/PUBLICATION_DENYLIST.txt']:
        try: data = git('cat-file', 'blob', '%s:%s' % (commit, p))
        except SystemExit: print('  (missing in tree: %s)' % p); continue
        dst = os.path.join(docs, p.replace('/', '__')); open(dst, 'wb').write(data)
    lines = []
    for f in sorted(os.listdir(out)):
        fp = os.path.join(out, f)
        if os.path.isdir(fp):
            for g in sorted(os.listdir(fp)): lines.append((os.path.join(f, g), os.path.join(fp, g)))
        elif f not in ('SHA256SUMS.txt', 'CANDIDATE.json'): lines.append((f, fp))
    with open(os.path.join(out, 'SHA256SUMS.txt'), 'w', newline='\n') as m:
        for rel, fp in lines: m.write('%s *%s\n' % (hashlib.sha256(open(fp, 'rb').read()).hexdigest(), rel.replace(os.sep, '/')))
    info = {'source_rev': rev, 'public_commit': commit, 'public_tree': tree, 'parent': base, 'branch': a.branch,
            'files_kept': len(keep), 'files_withheld': len(denied)}
    json.dump(info, open(os.path.join(out, 'CANDIDATE.json'), 'w'), indent=1)
    print(json.dumps(info, indent=1))


if __name__ == '__main__':
    main()

# Agent Notes (notepad-star)

## Repository hygiene (keep in mind for every change)

This is a personal project published at `github.com/T-Gojo/notepad-star`.

- **Commit identity:** commit as `Prasanna Parida <myselfprasannaparida@gmail.com>`
  (set in this repo's local git config). Run `git config user.email` before
  committing; never commit or push with a work/employer identity.
- **Pushing:** authenticate as the `T-Gojo` GitHub account. Don't commit, push,
  rewrite history or force-push unless explicitly asked.
- **Public package registry only:** dependencies must resolve from
  `https://registry.npmjs.org/`. Don't commit an `.npmrc`/`.yarnrc` that points
  to a private or employer feed. A machine-level registry/proxy setting can leak
  into lockfiles during install, so check lockfile changes before committing:
  - `package-lock.json`: every `"resolved"` URL starts with
    `https://registry.npmjs.org/`. This must print nothing:
    `git grep -n '"resolved": "http' -- '*package-lock.json' | grep -v registry.npmjs.org`
    (PowerShell: replace `grep -v` with `Select-String -NotMatch`).
  - `bun.lock`: the tarball URL of each package entry stays `""` (use the
    configured registry); never commit full private-feed URLs.
  - To fix, rewrite the URL prefix to the public registry; integrity hashes
    stay the same.
- **No machine-local data:** don't commit files containing local usernames or
  absolute user paths, emulator state (e.g. `__azurite_*`), `.env*` files,
  tokens or other credentials.
- **History note:** history was rewritten on 2026-10-04 to normalize author
  identity and lockfile registry URLs. Re-sync older clones (save local work,
  then `git fetch origin` and `git reset --hard origin/<branch>`), and never
  push old history back from stale clones or backup bundles.

# automation

This branch holds no Special K code. It holds the GitHub Action that builds
the **`combined`** branch: upstream
[SpecialKO/SpecialK](https://github.com/SpecialKO/SpecialK) `main` with every
branch of this fork merged on top, in alphabetical order, except the ones
excluded in [`exclude.txt`](exclude.txt).

`combined` is rebuilt from scratch and force-pushed every time, so never
commit to it. Fix things on the source branch they belong to.

## When it runs

- **Every hour.** The run only fingerprints its inputs: upstream `main`, each
  listed branch and this branch. It rebuilds only when one of them has
  changed, and it pushes only when the resulting content differs from
  `combined`.
- **On demand.** Go to *Actions → Rebuild combined → Run workflow*. Tick
  *force* to rebuild even when nothing changed.

You do not need to sync `main` for `combined` to pick up upstream changes,
because the Action fetches upstream directly.

Every time `combined` is pushed, or on a forced run, the same workflow then
compiles it (Release, x64 and Win32) and attaches the DLLs to the run as
`SpecialK_combined_<version>_<sha>`, plus a matching `_debug_symbols` artifact.
The fork's own *Builds* workflow does not run for `combined`, because pushes
made by an Action never trigger other workflows.

## Which branches are included

All of them by default: a new branch is included on the next run (within the
hour). To keep a branch out, add its name or a glob pattern to
[`exclude.txt`](exclude.txt) on this branch. `*` also matches `/`, so
`wip/*` excludes a whole folder. `automation` and `combined` are always
excluded.

Each run's summary lists the branches it merged. To preview locally which
branches would be included, run `bash rebuild.sh --list`.

Keep in mind that every branch you push gets merged. A branch that conflicts
with another one, or doesn't compile, fails the run until you exclude it or
fix it, and `combined` keeps its last good state in the meantime.

## Merge conflicts

Known conflicts are resolved automatically from [`rr-cache/`](rr-cache), using
git's `rerere`. A new conflict makes the run fail. The run's summary shows the
conflicting diff, and GitHub emails you about the failure. The same inputs are
not retried every hour.

To fix a conflict:

- **Fix it on a source branch.** For example, rebase one branch so the two no
  longer clash. Or:
- **Record a resolution.** Run `rebuild.sh` locally (Git Bash works), from a
  clone with `core.autocrlf=false`:

  ```sh
  git clone -c core.autocrlf=false https://github.com/Aemony/SpecialK
  cd SpecialK
  git worktree add ../automation automation
  cd ../automation
  bash rebuild.sh           # stops at the conflict and prints where
  # resolve it in that folder, then: git add -A && git commit --no-edit
  bash rebuild.sh           # replays the resolution and saves it into rr-cache/
  git add rr-cache && git commit -m "Record resolution" && git push
  ```

  `PUSH=1 bash rebuild.sh` also pushes the result, so you don't have to wait
  for the Action.

## Recorded resolutions

| Branches | File | Resolution |
|---|---|---|
| `fix/winevent-hook-on-window-thread` + `fix/yakuza-kiwami` | `src/window.cpp`, start of `SK_DetourWindowProc` | Keep both: the deferred WinEvent hook install, then the Yakuza `WM_SIZE` filter |
| `fix/edf41-radial-menu-click` + `fix/pop-2008-hdr-startup-crash` | `include/SpecialK/config.h` (`SK_GAME_ID`), `src/config.cpp` (exe → game ID table) | Keep both new game IDs, EDF41 first (with a comma added after its table entry) |

When resolving by hand on Windows, edit with something that keeps the files'
CRLF line endings. Git Bash's `sed -i` silently converts them to LF unless
you pass `-b`.

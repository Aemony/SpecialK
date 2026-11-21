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

## Releases

Every build of `combined` is compiled (Release, x64 and Win32) and published as
a GitHub **Release** tagged `combined-<sha8>`, marked as the latest release.
It has two downloads: `SpecialK_combined_<version>_<sha8>.zip` with the DLLs,
and `..._debug_symbols.zip` with the PDBs. The release notes list the upstream
commit it's based on and every merged branch with its commit. The same files
are also attached to the workflow run as artifacts.

- **Already released:** when a run finds that the current `combined` already
  has a release, it builds nothing. The run instead shows a notice and a
  summary that link to that release.
- **No release, nothing changed:** if the current `combined` has no release
  and no input changed since the last attempt, the last compile or publish
  must have failed. The run only warns, so you don't get an email every hour.
  Run the workflow with *force* to try again.
- **Forced run:** compiles again and replaces the assets and notes of the
  existing release.

The tags keep each released `combined` commit available, even after `combined`
itself has been rebuilt. Delete old releases and their tags whenever you like.
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
| `feat/d3d9-texmod` + `improve/d3d9-skip-dxgi-hooks` | `src/diagnostics/load_library.cpp`, `SK_TraceLoadLibrary` before the `hCallingMod != SK_GetDLL ()` block | Keep both: the `SK_D3D9_HookD3DX9 (true)` call, then the DXGI application-load check |

When resolving by hand on Windows, edit with something that keeps the files'
CRLF line endings. Git Bash's `sed -i` silently converts them to LF unless
you pass `-b`.

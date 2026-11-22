# Frosted K

**Frosted K** is an unofficial build of
[Special K](https://github.com/SpecialKO/SpecialK): the latest Special K with
this fork's own fixes and features on top, some of which may never be merged
into Special K itself. Special K, with frosting.

## Download

Get the latest build from [**Releases**](../../releases/latest). Each release
has two files:

- `FrostedK_<version>_<build>.zip`: `SpecialK64.dll` and `SpecialK32.dll`.
- `FrostedK_<version>_<build>_debug_symbols.zip`: the matching PDBs, only
  needed for debugging crashes.

Each release's notes list the Special K version it's based on and every change
it adds. A new build is published automatically, usually within an hour,
whenever Special K or one of the changes is updated.

## Installing

The DLLs work like Special K's own, and keep the same file names, so use them
however you use Special K. Local injection is the simplest: copy the DLL that
matches the game into the game's folder, renamed to the API it hooks (for
example `dxgi.dll` or `d3d11.dll`).

If you replace the DLLs of a global (SKIF) install instead, keep in mind that
SKIF's updater may replace them with an official build once a newer Special K
version comes out.

## Telling builds apart

Frosted K identifies itself everywhere Special K shows its version: the
overlay, the control panel, `SpecialK.log` and the INI. For example:

```
Special K v 26.11.4.1 (Frosted K 491bf1c7)
```

The first part is the Special K version it's based on, and the last part is
the build. The DLL's *Details* tab in Explorer shows the same version, and the
description *Special K Modding System (Frosted K)*.

---

# How Frosted K is built

The rest of this page is for the maintainer.

This branch, `automation`, holds no Special K code. It holds the GitHub Action
that builds the **`combined`** branch, which is upstream
[SpecialKO/SpecialK](https://github.com/SpecialKO/SpecialK) `main` with every
branch of this fork merged on top, in alphabetical order, except the ones
excluded in [`exclude.txt`](exclude.txt). The Action then compiles `combined`
and publishes it as Frosted K.

`combined` is rebuilt from scratch and force-pushed every time, so never
commit to it. Fix things on the source branch they belong to.

## When it runs

- **Every hour.** The run only fingerprints its inputs: upstream `main`, each
  included branch and this branch. It rebuilds only when one of them has
  changed, and it pushes only when the resulting content differs from
  `combined`.
- **On demand.** Go to *Actions → Rebuild combined → Run workflow*. Tick
  *force* to rebuild even when nothing changed.

You do not need to sync `main` for `combined` to pick up upstream changes,
because the Action fetches upstream directly.

## Releases

Every build of `combined` is compiled (Release, x64 and Win32) and published as
a GitHub **Release** tagged `frostedk-<sha8>`, titled *Frosted K
&lt;version&gt; (&lt;sha8&gt;)* and marked as the latest release. The same
files are also attached to the workflow run as artifacts.

The name is set at the top of the
[workflow](.github/workflows/rebuild-combined.yml), with `PRODUCT` (titles and
the version string), `SLUG` (file names) and `TAG_PREFIX` (tags).

- **Version stamp:** before compiling, the Action appends
  ` (Frosted K <sha8>)` to `SK_VERSION_STR_A` in
  `include/SpecialK/DLL_VERSION.H`, and adds ` (Frosted K)` to `FileDescription`
  in `resource/Resource.rc`. It changes only the build's working
  copy, never `combined` or any branch. The numeric file version and the
  `ProductName` ("Special K") stay unchanged, because SKIF only recognizes a
  DLL whose product name contains "Special K". The suffix also must not contain
  a dot, because SKIF compares versions by the leading digits of each
  dot-separated part. If upstream ever stops defining `SK_VERSION_STR_A` that
  way, the build fails at this step instead of shipping an unmarked DLL.
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
Releases tagged `combined-<sha8>` are from before the Frosted K name.

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

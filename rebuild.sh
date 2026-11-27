#!/usr/bin/env bash
# Rebuilds the "combined" branch from scratch: upstream main + every branch of
# origin that does not match a pattern in exclude.txt, merged alphabetically.
#
# Runs in GitHub Actions (.github/workflows/rebuild-combined.yml) and locally
# from a checkout of the automation branch.
#   bash rebuild.sh          build combined (PUSH=1 also pushes it)
#   bash rebuild.sh --list   print "<sha> <ref>" for upstream and every included
#                            branch, without fetching anything; the workflow
#                            fingerprints this to skip runs with no changes
# Environment:
#   PUSH=1           push the result (default: only build it)
#   UPSTREAM_URL     default https://github.com/SpecialKO/SpecialK.git
#   UPSTREAM_BRANCH  default main
#   TARGET           default combined
#
# Merge conflicts are resolved with git rerere, using the resolutions stored in
# rr-cache/ next to this script. See README.md for recording a new one.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
UPSTREAM_URL=${UPSTREAM_URL:-https://github.com/SpecialKO/SpecialK.git}
UPSTREAM_BRANCH=${UPSTREAM_BRANCH:-main}
TARGET=${TARGET:-combined}
PUSH=${PUSH:-0}

# --- Pick branches -------------------------------------------------------------
mapfile -t patterns < <(sed -e 's/\r$//' -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' "$here/exclude.txt" | grep -v '^$' || true)
patterns+=(automation "$TARGET")

excluded () {
  local p
  for p in "${patterns[@]}"; do
    # shellcheck disable=SC2254  # unquoted on purpose: $p is a glob
    case "$1" in $p) return 0 ;; esac
  done
  return 1
}

heads=$(git -C "$here" ls-remote --heads origin | LC_ALL=C sort -k2)
branches=()
included_heads=""
while read -r sha ref; do
  [ -n "$ref" ] || continue
  b=${ref#refs/heads/}
  excluded "$b" && continue
  branches+=("$b")
  included_heads+="$sha $ref"$'\n'
done <<< "$heads"

if [ "${1:-}" = "--list" ]; then
  git ls-remote "$UPSTREAM_URL" "refs/heads/$UPSTREAM_BRANCH"
  printf '%s' "$included_heads"
  exit 0
fi

if [ ${#branches[@]} -eq 0 ]; then
  echo "::error::No branches left after applying exclude.txt"
  exit 1
fi
printf 'Including %d branches:\n' ${#branches[@]}; printf '  %s\n' "${branches[@]}"

git_dir=$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)

summary () { [ -n "${GITHUB_STEP_SUMMARY:-}" ] && echo "$*" >> "$GITHUB_STEP_SUMMARY" || true; }
output  () { [ -n "${GITHUB_OUTPUT:-}"       ] && echo "$1=$2" >> "$GITHUB_OUTPUT"  || true; }

# --- Fetch inputs --------------------------------------------------------------
echo "Fetching $UPSTREAM_URL $UPSTREAM_BRANCH"
git -C "$here" fetch -q --no-tags "$UPSTREAM_URL" "+refs/heads/$UPSTREAM_BRANCH:refs/remotes/upstream/$UPSTREAM_BRANCH"

specs=()
for b in "${branches[@]}"; do specs+=("+refs/heads/$b:refs/remotes/origin/$b"); done
echo "Fetching ${#branches[@]} branches from origin"
git -C "$here" fetch -q --no-tags origin "${specs[@]}"

old=""
if git -C "$here" fetch -q --no-tags origin "+refs/heads/$TARGET:refs/remotes/origin/$TARGET" 2>/dev/null; then
  old=$(git -C "$here" rev-parse "refs/remotes/origin/$TARGET")
fi

# --- Restore recorded conflict resolutions ---------------------------------------
mkdir -p "$git_dir/rr-cache"
if [ -d "$here/rr-cache" ]; then cp -R "$here/rr-cache/." "$git_dir/rr-cache/"; fi

# --- Merge ---------------------------------------------------------------------
wt=$(mktemp -d)
git -C "$here" worktree add -q --detach "$wt" "refs/remotes/upstream/$UPSTREAM_BRANCH"
keep_wt=0
cleanup () { [ "$keep_wt" = 1 ] || git -C "$here" worktree remove --force "$wt"; }
trap cleanup EXIT

cd "$wt"
gitr () { git -c rerere.enabled=true -c rerere.autoupdate=true "$@"; }

summary "## Rebuilding \`$TARGET\`"
summary "Base: \`$UPSTREAM_BRANCH\` of $UPSTREAM_URL at \`$(git rev-parse --short HEAD)\`"
summary ""

upstream_ref="refs/remotes/upstream/$UPSTREAM_BRANCH"
for b in "${branches[@]}"; do
  # A branch that's already merged has nothing left to add: git would report
  # "Already up to date" without a merge commit, so it wouldn't be in combined's
  # history (or the release notes) as a merge of its own. Say so instead.
  if git merge-base --is-ancestor "refs/remotes/origin/$b" HEAD; then
    if git merge-base --is-ancestor "refs/remotes/origin/$b" "$upstream_ref"; then
      echo "::notice title=Already upstream::$b is already part of upstream $UPSTREAM_BRANCH; it can be deleted or excluded."
      summary "- ⏭️ \`$b\`: already part of upstream \`$UPSTREAM_BRANCH\`, nothing to merge (delete or exclude it)"
    else
      echo "$b is already contained in an earlier branch; nothing to merge."
      summary "- ⏭️ \`$b\`: already contained in an earlier branch, nothing to merge"
    fi
    continue
  fi

  echo "::group::Merge $b"
  if gitr merge --no-ff --no-edit -m "Merge branch '$b' into $TARGET" "refs/remotes/origin/$b"; then
    echo "::endgroup::"; summary "- ✅ \`$b\`"; continue
  fi

  unresolved=$(git diff --name-only --diff-filter=U)
  if [ -z "$unresolved" ]; then
    echo "All conflicts resolved from rr-cache"
    gitr commit -q --no-edit
    echo "::endgroup::"; summary "- ✅ \`$b\` (conflict resolved from rr-cache)"; continue
  fi
  echo "::endgroup::"

  echo "::error::Unresolved conflict merging $b: $(echo $unresolved)"
  summary "- ❌ \`$b\`: unresolved conflict in $(echo $unresolved)"
  summary ""
  summary '```diff'
  summary "$(git diff | head -200)"
  summary '```'
  if [ -z "${GITHUB_ACTIONS:-}" ]; then
    keep_wt=1
    cat <<EOF

Conflict left in: $wt
Resolve it there, then:  git add -A && git commit --no-edit
Then run this script again; rerere replays your resolution and saves it into
$here/rr-cache for you to commit.
EOF
  fi
  exit 1
done

# --- Save resolutions (only ones with a recorded result) -------------------------
for d in "$git_dir"/rr-cache/*/; do
  ls "$d" | grep -q '^postimage' || continue
  mkdir -p "$here/rr-cache/$(basename "$d")"
  cp "$d"/preimage* "$d"/postimage* "$here/rr-cache/$(basename "$d")/"
done

# --- Publish -------------------------------------------------------------------
new=$(git rev-parse HEAD)
if [ -n "$old" ] && [ "$(git rev-parse "$old^{tree}")" = "$(git rev-parse "$new^{tree}")" ]; then
  echo "$TARGET already has this exact content; nothing to push."
  summary ""; summary "No change in content; \`$TARGET\` left as is."
  output sha "$old"; output pushed false
  exit 0
fi

if [ "$PUSH" = 1 ]; then
  git push --force-with-lease="refs/heads/$TARGET:$old" origin "$new:refs/heads/$TARGET"
  summary ""; summary "Pushed \`$TARGET\` at \`$(git rev-parse --short "$new")\`."
  output sha "$new"; output pushed true
else
  echo "Built $new (not pushed; set PUSH=1 to push it)"
fi

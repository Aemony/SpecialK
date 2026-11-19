#!/usr/bin/env bash
# Rebuilds the "combined" branch from scratch: upstream main + every branch
# listed in branches.txt, merged in order.
#
# Runs in GitHub Actions (.github/workflows/rebuild-combined.yml) and locally
# from a checkout of the automation branch. Environment:
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

mapfile -t branches < <(sed -e 's/\r$//' -e 's/#.*//' -e 's/[[:space:]]*$//' "$here/branches.txt" | grep -v '^$')

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

for b in "${branches[@]}"; do
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

#!/usr/bin/env bash
set -euo pipefail

CMAKELISTS="CMakeLists.txt"

usage() {
  echo "Usage: $0 <version>"
  echo "  version: patch | minor | major | x.y.z | vx.y.z"
  exit 1
}

[[ $# -ne 1 ]] && usage

# Read current version from CMakeLists.txt
current=$(sed -n 's/project(onpoint VERSION \([0-9][0-9.]*\) .*/\1/p' "$CMAKELISTS")
[[ -z "$current" ]] && { echo "Could not read version from $CMAKELISTS"; exit 1; }

# Normalize to three parts (pad with zeros if needed)
IFS='.' read -r -a _parts <<< "$current"
v_major=${_parts[0]:-0}
v_minor=${_parts[1]:-0}
v_patch=${_parts[2]:-0}

arg="${1#v}"  # strip leading 'v'

case "$arg" in
  patch) v_patch=$((v_patch + 1)) ;;
  minor) v_minor=$((v_minor + 1)); v_patch=0 ;;
  major) v_major=$((v_major + 1)); v_minor=0; v_patch=0 ;;
  [0-9]*.[0-9]*.[0-9]*)
    IFS='.' read -r v_major v_minor v_patch <<< "$arg" ;;
  *)
    echo "Invalid version argument: $1"; usage ;;
esac

new_version="${v_major}.${v_minor}.${v_patch}"
tag="v${new_version}"

echo "Bumping ${current} → ${new_version}"

# Update CMakeLists.txt (works on macOS and Linux)
sed -i.bak "s/project(onpoint VERSION [0-9][0-9.]*/project(onpoint VERSION ${new_version}/" "$CMAKELISTS"
rm -f "${CMAKELISTS}.bak"

# Verify the write worked
updated=$(sed -n 's/project(onpoint VERSION \([0-9][0-9.]*\) .*/\1/p' "$CMAKELISTS")
[[ "$updated" != "$new_version" ]] && { echo "Version update failed (got '${updated}')"; exit 1; }

# Commit and tag
git add "$CMAKELISTS"
git commit -m "chore: bump version to ${tag}"
git tag "$tag"

echo "Created commit and tag ${tag}"
echo "Pushing..."
git push
git push origin "$tag"

echo "Done. Released ${tag}"

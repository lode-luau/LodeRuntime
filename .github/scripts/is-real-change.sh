#!/usr/bin/env bash
# Decides whether a revision range contains a change that matters to the
# built runtime. Exit codes: 0 = real change, 1 = noise, 2 = usage error.
#
# Classification layers:
#   1. Documentation-only paths are always noise (markdown, txt, license,
#      docs/ and rfcs/ trees, git metadata) — except CMakeLists.txt, which
#      drives compilation despite the suffix. Keep the pattern list in sync
#      with the paths-ignore block in .github/workflows/ci.yml.
#   2. Source files (.cpp/.cc/.hpp/.h/.luau/...) are compared with git
#      -w --ignore-blank-lines plus -I patterns that drop hunks whose
#      changed lines are full-line //, --, * , /* or */ comment lines.
#   3. A trailing-comment rescue then compares removed vs added lines
#      after cutting an end-of-line // or -- comment outside strings, so
#      "int x = 1; // note" edits count as noise too. Block-comment
#      interiors without leading markers stay fail-safe (reported real).
#
# Note: Difftastic was evaluated as an oracle and rejected on purpose —
# comments are AST nodes there, so ADDING a comment counts as a change,
# which is the opposite of this gate's rule.
#
# Usage: is-real-change.sh <old-rev> <new-rev>
set -uo pipefail


strip_trailing_comment() {
  awk '
    {
      line = $0; out = ""; i = 1; n = length(line); in_str = 0; q = ""
      apos = sprintf("%c", 39)
      while (i <= n) {
        c = substr(line, i, 1)
        two = substr(line, i, 2)
        if (in_str) {
          out = out c
          if (c == "\\\\") { i++; if (i <= n) out = out substr(line, i, 1) }
          else if (c == q) in_str = 0
          i++; continue
        }
        if (two == "//" || two == "--") break
        if (c == "\"") { in_str = 1; q = "\""; out = out c; i++; continue }
        if (c == apos) { in_str = 1; q = apos; out = out c; i++; continue }
        out = out c
        i++
      }
      gsub(/[ 	]+/, " ", out)
      sub(/^ /, "", out)
      sub(/ $/, "", out)
      print out
    }'
}

fallback_real() {
  local file="$1"
  case "$file" in
    *.md|*.txt|.gitignore|.gitattributes|LICENSE|LICENSE.*|docs/*|rfcs/*)
      return 1 ;;
  esac

  if git diff --quiet -w --ignore-blank-lines \
        -I '^[[:space:]]*//' \
        -I '^[[:space:]]*--' \
        -I '^[[:space:]]*\*' \
        -I '^[[:space:]]*/\*' \
        -I '^[[:space:]]*\*/' \
        "$OLD" "$NEW" -- "$file"; then
    return 1
  fi

  local removed added
  removed=$(git diff "$OLD" "$NEW" -- "$file" \
    | grep -E '^-[^-]' | sed 's/^-//' \
    | strip_trailing_comment | grep -v '^$' | sort)
  added=$(git diff "$OLD" "$NEW" -- "$file" \
    | grep -E '^\+[^+]' | sed 's/^+//' \
    | strip_trailing_comment | grep -v '^$' | sort)
  [ "$removed" = "$added" ] && return 1
  return 0
}

classify_file() {
  local file="$1"
  case "$file" in
    CMakeLists.txt)
      # Drives compilation despite the .txt suffix.
      return 0 ;;
    *.md|*.txt|.gitignore|.gitattributes|LICENSE|LICENSE.*|docs/*|rfcs/*)
      return 1 ;;
  esac

  # Difftastic was evaluated as the oracle here and rejected on purpose:
  # it counts an ADDED COMMENT as a syntactic change (comments are AST
  # nodes), while this gate must treat comment-only and whitespace-only
  # edits as noise. The git-native filters below implement that rule.
  fallback_real "$file"
}

[ $# -eq 2 ] || { echo "usage: $0 <old-rev> <new-rev>" >&2; exit 2; }
OLD="$1"
NEW="$2"

FILES=$(git diff --name-only "$OLD" "$NEW")
rc=1
while IFS= read -r file; do
  [ -n "$file" ] || continue
  if classify_file "$file"; then
    echo "real: $file"
    rc=0
  fi
done <<< "$FILES"

if [ "$rc" -ne 0 ]; then
  echo "no real change"
fi
exit "$rc"

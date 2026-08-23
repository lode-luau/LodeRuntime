#!/usr/bin/env bash
# Decides whether a revision range contains a change that matters to the
# built runtime. Exit codes: 0 = real change, 1 = noise, 2 = usage error.
#
# Classification layers:
#   1. Documentation-only paths are always noise (markdown, txt, license,
#      docs/ and rfcs/ trees, git metadata) — except CMakeLists.txt, which
#      drives compilation despite the suffix. Keep the pattern list in sync
#      with the paths-ignore block in .github/workflows/ci.yml.
#
#   2. PRECISION LAYER (preferred): when "shush" is on PATH, both blob
#      versions are stripped of comments with it (string-aware, per-language,
#      https://github.com/carlosarraes/shush — pin: v0.3.3) and the stripped
#      files are compared with git diff --no-index -w. Install:
#        go install github.com/carlosarraes/shush/cmd/shush@v0.3.3
#      .luau blobs are renamed to .lua for shush (same comment syntax).
#      A .shush.toml with preserve patterns would weaken determinism; do not
#      add one to this repository.
#
#   3. FALLBACK LAYER (no shush): git -w --ignore-blank-lines plus -I
#      patterns and a trailing-comment rescue, strictly per language so a
#      token that is an operator somewhere is never treated as a comment:
#        cpp family : // /* */ *    (never "--": that is the decrement op)
#        luau/lua   : --
#        python/rb/sh/ps1/toml/yaml/conf/ini : #
#        php        : // # /* */
#        sql        : -- /* */
#        generic    : whitespace only (fail-safe)
#      Block-comment interiors without leading markers stay fail-safe.
#
# Note: Difftastic was evaluated as an oracle and rejected on purpose —
# comments are AST nodes there, so ADDING a comment counts as a change,
# which is the opposite of this gate's rule.
#
# Usage: is-real-change.sh <old-rev> <new-rev>
set -uo pipefail

SHUSH_BIN="$(command -v shush || true)"
TMPROOT=""
cleanup() { [ -n "$TMPROOT" ] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

# Language dispatch for the fallback layer. Echoes a single token.
lang_of() {
  case "$1" in
    *.cpp|*.cc|*.cxx|*.c|*.hpp|*.hh|*.h|*.hxx|*.inl) echo cpp ;;
    *.luau|*.lua)                                    echo luau ;;
    *.py|*.pyi|*.rb|*.sh|*.bash|*.ps1|*.toml|*.yml|*.yaml|*.ini|*.conf) echo hash ;;
    *.php)                                           echo php ;;
    *.sql)                                           echo sql ;;
    *)                                               echo generic ;;
  esac
}

# Trailing-comment stripper for ONE fixed token, quote-aware.
strip_trailing() {
  local tok="$1"
  awk -v tok="$tok" '
    {
      line = $0; out = ""; i = 1; n = length(line); in_str = 0; q = ""
      apos = sprintf("%c", 39)
      tlen = length(tok)
      while (i <= n) {
        c = substr(line, i, 1)
        if (in_str) {
          out = out c
          if (c == "\\\\") { i++; if (i <= n) out = out substr(line, i, 1) }
          else if (c == q) in_str = 0
          i++; continue
        }
        if (tlen > 0 && substr(line, i, tlen) == tok) break
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

# Fallback classification for one file using per-language git filters.
fallback_real() {
  local file="$1" lang ignore1="" ignore2="" trail1="" trail2=""
  lang=$(lang_of "$file")

  # Layer-1 full-line comment patterns (-I) per language.
  local -a ignores=()
  case "$lang" in
    cpp)
      ignores=('-I' '^[[:space:]]*//' '-I' '^[[:space:]]*/\*' '-I' '^[[:space:]]*\*' '-I' '^[[:space:]]*\*/')
      trail1="//" ;;
    luau)
      ignores=('-I' '^[[:space:]]*--')
      trail1="--" ;;
    hash)
      ignores=('-I' '^[[:space:]]*#')
      trail1="#" ;;
    php)
      ignores=('-I' '^[[:space:]]*//' '-I' '^[[:space:]]*#' '-I' '^[[:space:]]*/\*' '-I' '^[[:space:]]*\*' '-I' '^[[:space:]]*\*/')
      trail1="//"; trail2="#" ;;
    sql)
      ignores=('-I' '^[[:space:]]*--' '-I' '^[[:space:]]*/\*' '-I' '^[[:space:]]*\*' '-I' '^[[:space:]]*\*/')
      trail1="--" ;;
    *)
      ignores=() ;;
  esac

  # bash >= 4.4 expands empty arrays safely under set -u; runners ship 5.x.
  if git diff --quiet -w --ignore-blank-lines "${ignores[@]}" \
        "$OLD" "$NEW" -- "$file"; then
    return 1
  fi

  if [ -z "$trail1$trail2" ]; then
    return 0   # no comment knowledge for this language: whitespace only
  fi

  local removed added
  removed=$(git diff "$OLD" "$NEW" -- "$file" \
    | grep -E '^-[^-]' | sed 's/^-//' \
    | strip_trailing "$trail1" \
    | { [ -n "$trail2" ] && strip_trailing "$trail2" || cat; } \
    | grep -v '^$' | sort)
  added=$(git diff "$OLD" "$NEW" -- "$file" \
    | grep -E '^\+[^+]' | sed 's/^+//' \
    | strip_trailing "$trail1" \
    | { [ -n "$trail2" ] && strip_trailing "$trail2" || cat; } \
    | grep -v '^$' | sort)
  [ "$removed" = "$added" ] && return 1
  return 0
}

# True when shush natively understands the extension (after .luau rename).
shush_candidate() {
  case "$1" in
    *.cpp|*.cc|*.cxx|*.c|*.hpp|*.hh|*.h|*.hxx|*.inl|*.lua|*.py|*.pyi|*.php|*.sql|*.ps1|*.sh|*.bash|*.toml|*.yml|*.yaml|*.rs|*.go|*.java|*.js|*.jsx|*.ts|*.tsx|*.kt|*.swift|*.dart|*.cs|*.rb|*.pl) return 0 ;;
    *) return 1 ;;
  esac
}

# Strip comments from both blobs with shush; echoes nothing, returns 0 on
# success. Uses $WORK_A/$WORK_B prepared by the caller.
shush_strip_pair() {
  local file="$1"
  local ext_old="a/$file" ext_new="b/$file"
  case "$file" in
    *.luau) ext_old="a/${file%.luau}.lua"; ext_new="b/${file%.luau}.lua" ;;
  esac
  mkdir -p "$(dirname "$TMPROOT/$ext_old")" "$(dirname "$TMPROOT/$ext_new")" 2>/dev/null || return 2
  # Added or deleted source files always count as real changes.
  git show "$OLD:$file" > "$TMPROOT/$ext_old" 2>/dev/null || return 0
  git show "$NEW:$file" > "$TMPROOT/$ext_new" 2>/dev/null || return 0
  "$SHUSH_BIN" "$TMPROOT/$ext_old" >/dev/null 2>&1 || return 2
  "$SHUSH_BIN" "$TMPROOT/$ext_new" >/dev/null 2>&1 || return 2
  if git diff --quiet --no-index -w "$TMPROOT/$ext_old" "$TMPROOT/$ext_new" >/dev/null 2>&1; then
    return 1   # identical after stripping: noise
  fi
  return 0     # real difference survives stripping
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

  if [ -n "$SHUSH_BIN" ] && shush_candidate "$file"; then
    local rc=2
    shush_strip_pair "$file"; rc=$?
    case $rc in
      1) return 1 ;;   # noise
      0) return 0 ;;   # real
      *) echo "warning: shush failed on $file; using fallback" >&2 ;;
    esac
  fi

  fallback_real "$file"
}

[ $# -eq 2 ] || { echo "usage: $0 <old-rev> <new-rev>" >&2; exit 2; }
OLD="$1"
NEW="$2"

TMPROOT="$(mktemp -d)"

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

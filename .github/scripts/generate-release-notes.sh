#!/usr/bin/env bash
# Generates the markdown notes for the nightly release.
#
# Env:
#   GITHUB_REPOSITORY  owner/name (provided by Actions)
#   NIGHTLY_VERSION    version being published (for the compare link)
#   GH_TOKEN           gh auth token (provided by Actions)
#   RUNNER_TEMP        scratch directory (falls back to TMPDIR or /tmp)
#   PREV_TAG_IN        optional override of the previous tag (local dry-runs)
#
# One GraphQL request maps mergeCommit oid -> PR for up to 100 recently
# updated merged PRs; a local --first-parent walk then joins each range entry
# to its PR without any per-commit API calls. Direct pushes render as
# short-sha lines. New contributors are resolved with one small search per
# distinct author.
set -uo pipefail

REPO="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}"
NIGHTLY_VERSION="${NIGHTLY_VERSION:-}"
WORKDIR="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
NOTES_FILE="${NOTES_FILE:-$WORKDIR/release-notes.md}"
OWNER="${REPO%%/*}"
NAME="${REPO#*/}"

# Newest nightly tag bounds the diff; PREV_TAG_IN exists only for dry-runs.
# Sort by name: lightweight tags fall back to commit dates under creatordate.
PREV_TAG="${PREV_TAG_IN:-$(git tag --list '1.0.0-nightly.*' | sort -rV | head -n 1)}"
RANGE="${PREV_TAG:+$PREV_TAG..}HEAD"

# --- One batched lookup: recent merged PRs keyed by merge commit oid ---
declare -A OID2META=()
gh api graphql \
  -f query='query($owner:String!,$name:String!){
    repository(owner:$owner,name:$name){
      pullRequests(states:MERGED,first:100,orderBy:{field:UPDATED_AT,direction:DESC}){
        nodes{ number title author{login} mergeCommit{oid} }
      }
    }
  }' \
  -f owner="$OWNER" -f name="$NAME" \
  --jq '.data.repository.pullRequests.nodes[]
        | select(.mergeCommit != null)
        | [.mergeCommit.oid, (.number|tostring), .title, (.author.login // "")]
        | @tsv' > "$WORKDIR/_prmap.tsv" 2>/dev/null || true

while IFS=$'\t' read -r oid num title login; do
  [ -n "$oid" ] || continue
  OID2META["$oid"]="$num"$'\t'"$title"$'\t'"$login"
done < "$WORKDIR/_prmap.tsv"
rm -f "$WORKDIR/_prmap.tsv"

# --- First-parent walk: PR merges in order, direct pushes as fallbacks ---
declare -a ORDER=() NEW_LINES=()
declare -A SEEN=() TITLE=() LOGIN=()

while IFS=$'\t' read -r full short subject; do
  re='^Merge pull request #([0-9]+)'
  if [[ "$subject" =~ $re ]]; then
    num="${BASH_REMATCH[1]}"
    key="pr:$num"
    [ -n "${SEEN[$key]:-}" ] && continue
    SEEN["$key"]=1
    ORDER+=("$key")
    meta="${OID2META[$full]:-}"
    if [ -n "$meta" ]; then
      TITLE["$key"]="${meta#*$'\t'}"
      TITLE["$key"]="${TITLE[$key]%%$'\t'*}"
      LOGIN["$key"]="${meta##*$'\t'}"
    else
      # Outside the 100-PR GraphQL window: one targeted fallback call.
      meta="$(gh api "repos/$REPO/pulls/$num" --jq '[.title, (.user.login // "")] | @tsv' 2>/dev/null || true)"
      if [ -n "$meta" ]; then
        TITLE["$key"]="${meta%%$'\t'*}"
        LOGIN["$key"]="${meta##*$'\t'}"
      else
        TITLE["$key"]="pull request #$num"
        LOGIN["$key"]=""
      fi
    fi
  else
    key="sha:$short"
    [ -n "${SEEN[$key]:-}" ] && continue
    SEEN["$key"]=1
    ORDER+=("$key")
    TITLE["$key"]="$subject"
    LOGIN["$key"]=""
  fi
done < <(git log --first-parent --reverse --format='%H%x09%h%x09%s' "$RANGE")

# --- Render ---
{
  echo "Automated nightly build."

  if [ "${#ORDER[@]}" -gt 0 ]; then
    echo
    echo "## What's Changed"
    echo
    for key in "${ORDER[@]}"; do
      case "$key" in
        pr:*)
          num="${key#pr:}"
          echo "* ${TITLE[$key]} by @${LOGIN[$key]} in #$num"
          ;;
        *)
          echo "* ${TITLE[$key]} (${key#sha:})"
          ;;
      esac
    done
    echo

    declare -A NEW_SEEN=()
    for key in "${ORDER[@]}"; do
      case "$key" in pr:*) ;; *) continue ;; esac
      login="${LOGIN[$key]}"
      num="${key#pr:}"
      [ -n "$login" ] || continue
      [ -z "${NEW_SEEN[$login]:-}" ] || continue
      NEW_SEEN["$login"]=1
      first_pr="$(gh api -X GET search/issues \
        -f q="repo:$REPO type:pr is:merged author:$login" \
        -f sort=created -f order=asc -f per_page=1 \
        --jq '.items[0].number // empty' 2>/dev/null || true)"
      if [ -n "$first_pr" ] && [ "$first_pr" = "$num" ]; then
        NEW_LINES+=("* @$login made their first contribution in #$num")
      fi
    done
    if [ "${#NEW_LINES[@]}" -gt 0 ]; then
      echo "## New Contributors"
      echo
      printf '%s\n' "${NEW_LINES[@]}"
      echo
    fi

    if [ -n "$PREV_TAG" ]; then
      echo "**Full Changelog**: https://github.com/$REPO/compare/$PREV_TAG...$NIGHTLY_VERSION"
    fi
  fi
} > "$NOTES_FILE"

cat "$NOTES_FILE"

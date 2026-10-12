#!/usr/bin/env bash
# Copies the reverse-engineering reports into docs/re/, scrubbed of machine
# paths. Usage: tools/dev/archive_re.sh REPORT_DIR
#   REPORT_DIR  folder holding the agents' *.md reports
# Paths are taken from the environment at run time and replaced by
# placeholders: $PES21_ROOT, <repo>, <scratch> (/tmp), ~ ($HOME). The result
# must pass tools/check_pii.sh; personal data the scrub cannot know about
# (save-file ids, names read from a user's save) is removed by review.
set -euo pipefail
src=${1:?usage: archive_re.sh REPORT_DIR}
repo=$(git rev-parse --show-toplevel)
out=$repo/docs/re
mkdir -p "$out"
for f in "$src"/pes-*.md; do
  [ -e "$f" ] || continue
  sed -e "s#${PES21_ROOT:-/nonexistent-pes-root}#\$PES21_ROOT#g" \
      -e "s#${repo}#<repo>#g" \
      -e "s#${HOME}#~#g" \
      -e "s#/tmp/#<scratch>/#g" \
      -e "s#/proc/[0-9]\{3,\}/#/proc/<pid>/#g" \
      -e "s#pid [0-9]\{5,\}#pid <pid>#g" \
      -e "s#\(save/\|SEASON UPDATE/\)[0-9]\{6,\}#\1<steamid>#g" \
      "$f" >"$out/$(basename "$f")"
done
bash "$repo/tools/check_pii.sh" >/dev/null || { echo "archive_re: docs/re fails the PII scan" >&2; exit 1; }
ls "$out"

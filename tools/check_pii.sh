#!/usr/bin/env bash
# Fails (exit 1) if any tracked file carries personal data, a machine path or a
# PES file. Patterns are written so this file does not match itself; the
# user-specific ones are built from the environment and never stored.
set -u
cd "$(git rev-parse --show-toplevel)" || exit 2

ALLOWLIST=tools/pii_allowlist.txt
fail=0

hit() { echo "PII: $1"; fail=1; }

path_patterns=(
  '/hom[e]/' '/mn[t]/' '/run/medi[a]/' '/User[s]/' '[A-Za-z]:\\'
  '\.claud[e]' 'claude-[0-9]'
)
user_patterns=()
[ -n "${USER:-}" ] && user_patterns+=("/${USER}/" "-${USER}-")
host=$(hostname 2>/dev/null || true)
[ -n "$host" ] && user_patterns+=("$host")
email='[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}'
pes_ext='\.(cpk|fpk|fpkd|fmdl|ftex|gani|mtar|ask|skl|frig|dds)$'

mapfile -d '' files < <(git ls-files -z)
[ "${#files[@]}" -eq 0 ] && exit 0

for f in "${files[@]}"; do
  [[ "$f" =~ $pes_ext ]] && hit "PES file tracked: $f"
done

scan() {  # $1 = label, $2 = grep flags, $3 = pattern
  local out
  out=$(printf '%s\0' "${files[@]}" | xargs -0 grep -nH $2 -e "$3" -- 2>/dev/null)
  [ -n "$out" ] && while IFS= read -r line; do hit "$1: $line"; done <<<"$out"
}
for p in "${path_patterns[@]}"; do scan "machine path" -E "$p"; done
for p in "${user_patterns[@]}"; do scan "user/host" -F "$p"; done

emails=$(printf '%s\0' "${files[@]}" | xargs -0 grep -nHoE -e "$email" -- 2>/dev/null)
if [ -n "$emails" ]; then
  while IFS= read -r line; do
    file=${line%%:*}
    allowed=0
    if [ -f "$ALLOWLIST" ]; then
      while IFS=$'\t' read -r apath aregex; do
        [ -z "$apath" ] && continue
        [ "$apath" = "$file" ] && [[ "$line" =~ $aregex ]] && { allowed=1; break; }
      done <"$ALLOWLIST"
    fi
    [ "$allowed" -eq 0 ] && hit "email: $line"
  done <<<"$emails"
fi

exit "$fail"

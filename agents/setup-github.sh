#!/usr/bin/env bash
# Idempotent: creates labels, milestones and the `evidence` branch.
set -euo pipefail
cd "$(dirname "$0")/.."
REPO=malespiaut/vibe-shooter-3d-II

mk() { gh label create "$1" --force --color "$2" --description "$3" -R "$REPO" >/dev/null; }
for l in engine render audio tools editor art level gameplay ui; do mk "$l" 1d76db "area"; done
for l in ready in-progress stuck blocked; do mk "$l" fbca04 "state"; done
for l in feature bug polish; do mk "$l" 0e8a16 "kind"; done
mk review:approved 2ea44f "Reviewer approved"
mk review:changes-requested d93f0b "Reviewer requested changes"
mk art:approved 8250df "Art Director approved"

existing=$(gh api "repos/$REPO/milestones?state=all&per_page=100" --jq '.[].title')
for m in "M1 Engine core" "M2 Asset pipeline & editor" "M3 Gameplay systems" \
         "M4 Content: levels 1–8" "M5 Polish & audio" "Episode 1"; do
  grep -qxF "$m" <<<"$existing" || gh api "repos/$REPO/milestones" -f title="$m" >/dev/null
done

if ! git ls-remote --exit-code --heads origin evidence >/dev/null 2>&1; then
  d=$(mktemp -d)
  git clone -q "$(git remote get-url origin)" "$d"
  git -C "$d" checkout -q --orphan evidence
  git -C "$d" rm -rfq . 
  printf '# Evidence\n\nScreenshots and demo output, under `<pr-number>/`. Never merge into main.\n' > "$d/README.md"
  git -C "$d" add README.md
  git -C "$d" commit -qm "evidence: init

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
  git -C "$d" push -q origin evidence
  rm -rf "$d"
fi

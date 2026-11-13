#!/usr/bin/env bash
# Fails if the current branch (vs BASE, default main) or the working tree
# touches a file the agents must not edit without an explicit task-brief grant.
# Usage: bash Code_IGNIS_v5/IGNIS_v5_FC/tools/check_protected_paths.sh [BASE]
# Read-only: it only runs git diff.
set -u
BASE="${1:-main}"
FW="Code_IGNIS_v5/IGNIS_v5_FC"
PROTECTED_RE="^(${FW}/src/core/(pyro_sequencer\.h|flight_fsm\.(cpp|h)|boot_policy\.(cpp|h)|telemetry_packet\.(cpp|h))|${FW}/src/hal/(pyro_hw|nvs_state)\.(cpp|h)|${FW}/src/config/(pins_v5|config_v5)\.h|PCB_Project_IGNIS-v5/.*\.(SchDoc|PcbDoc|PrjPcb|PcbLib|SchLib|LibPkg|IntLib|NET)|Project_Group_IGNIS-v5\.DsnWrk|Code_IGNIS_v5/IGNIS_FC_heavy_13/.*)$"

root="$(git rev-parse --show-toplevel)" || exit 2
cd "$root" || exit 2
changed="$( { git diff --name-only "${BASE}...HEAD"; git diff --name-only; git diff --name-only --cached; } 2>/dev/null | sort -u )"
hits="$(printf '%s\n' "$changed" | grep -E "$PROTECTED_RE" || true)"
if [ -n "$hits" ]; then
  echo "PROTECTED FILES CHANGED (needs the task brief to name them, and Emilio's sign-off):"
  printf '  %s\n' $hits
  exit 1
fi
echo "OK: no protected file changed vs ${BASE}"

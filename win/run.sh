#!/usr/bin/env bash
# Run one of the repo's bash tools on Windows (Git Bash + Docker Desktop), inside a Linux workbench container:
#   win/run.sh tools/test_port.sh poc/inputprobe/vst.json
#   win/run.sh tools/build_port.sh ports/one-ish/vst.json all
# The repo is mounted at the path Docker Desktop's own VM sees the Windows drive under
# (/run/desktop/mnt/host/<drive>/...), so the `docker run -v "$ROOT":...` calls the tools make from
# inside the workbench resolve to this same folder.
set -euo pipefail
REPO_WIN="$(cd "$(dirname "$0")/.." && pwd -W)"   # e.g. H:/sugarbank Dropbox/.../MPC_KEY37_G2
DRIVE="$(printf '%s' "${REPO_WIN:0:1}" | tr '[:upper:]' '[:lower:]')"
VM_PATH="/run/desktop/mnt/host/$DRIVE${REPO_WIN:2}"
export MSYS_NO_PATHCONV=1
docker build -q -t one-ish-workbench "$REPO_WIN/win" >/dev/null
docker run --rm -v /var/run/docker.sock:/var/run/docker.sock -v "$REPO_WIN:$VM_PATH" -w "$VM_PATH" \
  one-ish-workbench "$@"

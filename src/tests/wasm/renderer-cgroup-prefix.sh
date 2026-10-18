#!/bin/sh
set -eu

: "${EKA2L1_RENDERER_CGROUP:?}"
printf '%s\n' "$$" > "$EKA2L1_RENDERER_CGROUP/cgroup.procs"
if [ -n "${EKA2L1_RENDERER_PID_LOG:-}" ]; then
    printf '%s\n' "$$" >> "$EKA2L1_RENDERER_PID_LOG"
fi
exec "$@"

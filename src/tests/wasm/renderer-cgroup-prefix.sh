#!/bin/sh
set -eu

: "${EKA2L1_RENDERER_CGROUP:?}"
if [ -n "${EKA2L1_RENDERER_PID_LOG:-}" ]; then
    printf '%s\n' "$$" >> "$EKA2L1_RENDERER_PID_LOG"
fi
selected=1
if [ -n "${EKA2L1_TAB_RENDERER_CLIENT_ID:-}" ]; then
    selected=0
    for arg in "$@"; do
        if [ "$arg" = "--renderer-client-id=$EKA2L1_TAB_RENDERER_CLIENT_ID" ]; then
            selected=1
            break
        fi
    done
fi
if [ "$selected" -eq 1 ]; then
    printf '%s\n' "$$" > "$EKA2L1_RENDERER_CGROUP/cgroup.procs"
fi
exec "$@"

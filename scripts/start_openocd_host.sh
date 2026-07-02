#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Start host-side OpenOCD for RP2040 (CMSIS-DAP), for use with the devcontainer.

Usage:
  ./scripts/start_openocd_host.sh [options] [-- <extra-openocd-args...>]

Options:
  --interface <cfg>     OpenOCD interface cfg (default: interface/cmsis-dap.cfg)
  --target <cfg>        OpenOCD target cfg (default: target/rp2040.cfg)
  --speed <khz>         Adapter speed in kHz (default: 5000)
  --gdb-port <port>     GDB port (default: 3333)
  --gdb-max <count>     Max GDB connections (default: 3)
  --gdb-targets <names> OpenOCD target names for GDB max setting
                        (default: "rp2040.core0 rp2040.core1")
  --telnet-port <port>  Telnet port (default: 4444)
  --tcl-port <port>     TCL port (default: 6666)

Environment (equivalent defaults):
  OPENOCD_INTERFACE_CFG, OPENOCD_TARGET_CFG, OPENOCD_ADAPTER_SPEED,
  OPENOCD_GDB_PORT, OPENOCD_GDB_MAX_CONNECTIONS, OPENOCD_GDB_TARGETS,
  OPENOCD_TELNET_PORT, OPENOCD_TCL_PORT

Examples:
  ./scripts/start_openocd_host.sh
  ./scripts/start_openocd_host.sh --speed 2000
  ./scripts/start_openocd_host.sh -- --log_output openocd.log

Then from inside the container:
  make flash
EOF
}

if ! command -v openocd >/dev/null 2>&1; then
  echo "Error: openocd not found on PATH. Install OpenOCD on the host." >&2
  exit 127
fi

interface_cfg="${OPENOCD_INTERFACE_CFG:-interface/cmsis-dap.cfg}"
target_cfg="${OPENOCD_TARGET_CFG:-target/rp2040.cfg}"
adapter_speed="${OPENOCD_ADAPTER_SPEED:-5000}"
gdb_port="${OPENOCD_GDB_PORT:-3333}"
gdb_max_connections="${OPENOCD_GDB_MAX_CONNECTIONS:-3}"
gdb_targets="${OPENOCD_GDB_TARGETS:-rp2040.core0 rp2040.core1}"
telnet_port="${OPENOCD_TELNET_PORT:-4444}"
tcl_port="${OPENOCD_TCL_PORT:-6666}"

extra_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --interface)
      interface_cfg="$2"; shift 2
      ;;
    --target)
      target_cfg="$2"; shift 2
      ;;
    --speed)
      adapter_speed="$2"; shift 2
      ;;
    --gdb-port)
      gdb_port="$2"; shift 2
      ;;
    --gdb-max)
      gdb_max_connections="$2"; shift 2
      ;;
    --gdb-targets)
      gdb_targets="$2"; shift 2
      ;;
    --telnet-port)
      telnet_port="$2"; shift 2
      ;;
    --tcl-port)
      tcl_port="$2"; shift 2
      ;;
    --)
      shift
      extra_args+=("$@")
      break
      ;;
    *)
      extra_args+=("$1")
      shift
      ;;
  esac
done

echo "Starting OpenOCD (host)..."
echo "  interface:   $interface_cfg"
echo "  target:      $target_cfg"
echo "  speed (kHz): $adapter_speed"
echo "  gdb_port:    $gdb_port"
echo "  gdb_max:     $gdb_max_connections"
echo "  gdb_targets: $gdb_targets"
echo "  telnet_port: $telnet_port"
echo "  tcl_port:    $tcl_port"

gdb_max_args=()
for target_name in $gdb_targets; do
  gdb_max_args+=(-c "$target_name configure -gdb-max-connections $gdb_max_connections")
done

if [[ ${#extra_args[@]} -gt 0 ]]; then
  exec openocd \
    -f "$interface_cfg" \
    -f "$target_cfg" \
    -c "adapter speed $adapter_speed" \
    "${gdb_max_args[@]}" \
    -c "gdb_port $gdb_port" \
    -c "telnet_port $telnet_port" \
    -c "tcl_port $tcl_port" \
    "${extra_args[@]}"
else
  exec openocd \
    -f "$interface_cfg" \
    -f "$target_cfg" \
    -c "adapter speed $adapter_speed" \
    "${gdb_max_args[@]}" \
    -c "gdb_port $gdb_port" \
    -c "telnet_port $telnet_port" \
    -c "tcl_port $tcl_port"
fi

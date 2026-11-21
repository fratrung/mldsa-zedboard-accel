# Load the AMD 2026.1 tools (Vivado, Vitis, Vitis HLS, xsdb, ARM GCC).
# Usage:  export XILINX_ROOT=/path/to/Xilinx/2026.1   # e.g. /tools/Xilinx/2026.1
#         source scripts/env.sh
if [ -z "${XILINX_ROOT:-}" ]; then
    echo "ERROR: set XILINX_ROOT to the AMD 2026.1 install directory (it contains Vitis/ and Vivado/)." >&2
    return 1 2>/dev/null || exit 1
fi
if [ ! -f "$XILINX_ROOT/Vitis/settings64.sh" ]; then
    echo "ERROR: $XILINX_ROOT/Vitis/settings64.sh not found." >&2
    return 1 2>/dev/null || exit 1
fi
export XILINX_ROOT
source "$XILINX_ROOT/Vitis/settings64.sh"
echo "AMD tools loaded: $(vivado -version 2>/dev/null | head -1)"

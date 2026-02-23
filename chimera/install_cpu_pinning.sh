#!/usr/bin/env bash
set -e

ROOT="/home/trader/Chimera"
BIN="$ROOT/build/chimera"
RUN="$ROOT/run_chimera_pinned.sh"
ENV="$ROOT/cpu_pinning.env"
INFO="$ROOT/cpu_layout.txt"

if [ ! -x "$BIN" ]; then
  echo "[CPU PIN] chimera binary not found at $BIN"
  exit 1
fi

############################################
# 1. DETECT CPU
############################################
CPU_COUNT=$(nproc)
NUMA_COUNT=$(lscpu | awk '/NUMA node\(s\)/{print $3}')
HT=$(lscpu | awk '/Thread\(s\) per core/{print $4}')

echo "[CPU PIN] CPUs=$CPU_COUNT NUMA=$NUMA_COUNT HT=$HT"

############################################
# 2. ASSIGN CORES (SAFE)
############################################
FIX_TRADE=0
FIX_QUOTE=0
ENGINES=1
ATTRIB=""
GUI=1

if [ "$CPU_COUNT" -ge 3 ]; then
  FIX_TRADE=0
  FIX_QUOTE=1
  ENGINES=2
  GUI=2
fi

if [ "$CPU_COUNT" -ge 4 ]; then
  FIX_TRADE=0
  FIX_QUOTE=1
  ENGINES=2
  GUI=3
fi

if [ "$CPU_COUNT" -ge 5 ]; then
  ATTRIB=3
  GUI=4
fi

############################################
# 3. WRITE ENV FILE (SOURCEABLE)
############################################
cat << EOV > "$ENV"
FIX_TRADE=$FIX_TRADE
FIX_QUOTE=$FIX_QUOTE
ENGINES=$ENGINES
ATTRIB=$ATTRIB
GUI=$GUI
EOV

############################################
# 4. WRITE INFO FILE (HUMAN)
############################################
cat << EOI > "$INFO"
CPU COUNT     : $CPU_COUNT
NUMA NODES    : $NUMA_COUNT
HYPERTHREADS : $HT

PINNING MAP:
FIX_TRADE  -> core $FIX_TRADE
FIX_QUOTE  -> core $FIX_QUOTE
ENGINES    -> core $ENGINES
ATTRIB     -> ${ATTRIB:-N/A}
GUI        -> core $GUI
EOI

############################################
# 5. PINNED RUN SCRIPT
############################################
cat << 'EOR' > "$RUN"
#!/usr/bin/env bash
set -e

ROOT="/home/trader/Chimera"
BIN="$ROOT/build/chimera"
ENV="$ROOT/cpu_pinning.env"

source "$ENV"

echo "[RUN] CPU pinning:"
cat "$ENV"

taskset -c "$FIX_TRADE" "$BIN" --role=fix_trade &
taskset -c "$FIX_QUOTE" "$BIN" --role=fix_quote &
taskset -c "$ENGINES" "$BIN" --role=engines &

if [ -n "$ATTRIB" ]; then
  taskset -c "$ATTRIB" "$BIN" --role=attrib &
fi

taskset -c "$GUI" "$BIN" --role=gui &

wait
EOR

chmod +x "$RUN"

############################################
# 6. DONE
############################################
echo "[CPU PIN] Installed successfully"
echo "Env file:   $ENV"
echo "Layout:     $INFO"
echo "Run with:"
echo "  $RUN"

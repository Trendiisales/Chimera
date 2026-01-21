import csv
import time
import yaml
import asyncio
import statistics
from collections import defaultdict
from fastapi import FastAPI, WebSocket
from fastapi.middleware.cors import CORSMiddleware

CONFIG = yaml.safe_load(open("alpha_governor/config/governor.yaml"))

MIN_EDGE = CONFIG["min_edge_bps"]
COST = CONFIG["cost_bps"]
WINDOW = CONFIG["window"]
MIN_CONF = CONFIG["min_confidence"]
CMD_FILE = CONFIG["command_file"]
RESEARCH = CONFIG["research_csv"]

# Hysteresis settings
DISABLE_WINDOWS = CONFIG.get("disable_consecutive_windows", 3)
ENABLE_WINDOWS = CONFIG.get("enable_consecutive_windows", 5)
COOLDOWN = CONFIG.get("cooldown_trades", 100)

# Operating mode
MODE = CONFIG.get("mode", "OBSERVE")  # OBSERVE | ADVISE | ACT

app = FastAPI()
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

CLIENTS = set()
STATE = {}

# Hysteresis tracking
fail_counts = defaultdict(int)
pass_counts = defaultdict(int)
last_action_trade = defaultdict(int)

def load_trades():
    rows = []
    try:
        with open(RESEARCH, newline="") as f:
            reader = csv.DictReader(f)
            for r in reader:
                rows.append(r)
    except:
        return []
    return rows

def compute_confidence(pnls):
    if len(pnls) < 5:
        return 0.0
    mu = statistics.mean(pnls)
    sigma = statistics.pstdev(pnls) or 1e-9
    return abs(mu / sigma)

def emit(cmd, mode_prefix=""):
    if MODE == "OBSERVE":
        print(f"[OBSERVE] Would execute: {cmd}")
        return
    elif MODE == "ADVISE":
        print(f"[ADVISE] Recommending: {cmd}")
        with open(CMD_FILE, "a") as f:
            f.write(f"[ADVISE] {cmd}\n")
        return
    else:  # ACT mode
        with open(CMD_FILE, "a") as f:
            f.write(cmd + "\n")
        print(f"[ACT] {cmd}")

async def broadcast(data):
    for ws in list(CLIENTS):
        try:
            await ws.send_json(data)
        except:
            CLIENTS.remove(ws)

def analyze():
    global fail_counts, pass_counts, last_action_trade
    
    trades = load_trades()
    if len(trades) < WINDOW:
        return
    
    buckets = {}
    current_trade_count = len(trades)

    for t in trades[-WINDOW:]:
        sym = t["symbol"]
        if sym not in buckets:
            buckets[sym] = []
        buckets[sym].append(float(t["total_pnl"]))

    for sym, pnls in buckets.items():
        edge = statistics.mean(pnls) if pnls else 0.0
        conf = compute_confidence(pnls)
        survival = edge - COST

        # Check if enough trades have passed since last action
        trades_since_action = current_trade_count - last_action_trade[sym]
        can_act = trades_since_action >= COOLDOWN

        # Hysteresis logic
        is_failing = (survival < MIN_EDGE or conf < MIN_CONF)
        
        if is_failing:
            fail_counts[sym] += 1
            pass_counts[sym] = 0
        else:
            pass_counts[sym] += 1
            fail_counts[sym] = 0

        STATE[sym] = {
            "edge": round(edge, 4),
            "confidence": round(conf, 4),
            "survival": round(survival, 4),
            "fail_count": fail_counts[sym],
            "pass_count": pass_counts[sym],
            "trades_since_action": trades_since_action,
            "can_act": can_act,
            "mode": MODE,
            "status": "PASS" if not is_failing else "WARN"
        }

        # Only act if cooldown passed
        if not can_act:
            continue

        # Disable only after N consecutive failures
        if fail_counts[sym] >= DISABLE_WINDOWS:
            emit(f"DISABLE_ENGINE {sym} reason=survival:{survival:.2f}_conf:{conf:.2f}")
            fail_counts[sym] = 0
            last_action_trade[sym] = current_trade_count

        # Re-enable only after N consecutive passes
        if pass_counts[sym] >= ENABLE_WINDOWS:
            weight = min(CONFIG["max_weight"],
                         max(CONFIG["min_weight"],
                             conf / 2.0))
            emit(f"ENABLE_ENGINE {sym} weight={round(weight, 3)}")
            pass_counts[sym] = 0
            last_action_trade[sym] = current_trade_count

@app.websocket("/ws")
async def ws(ws: WebSocket):
    await ws.accept()
    CLIENTS.add(ws)
    try:
        while True:
            await asyncio.sleep(2)
            analyze()
            await broadcast({"mode": MODE, "engines": STATE})
    except:
        CLIENTS.remove(ws)

@app.get("/health")
def health():
    return {"status": "alpha-governor-online", "mode": MODE}

@app.get("/state")
def get_state():
    return {"mode": MODE, "engines": STATE}

@app.post("/set_mode")
def set_mode(mode: str):
    global MODE
    if mode in ["OBSERVE", "ADVISE", "ACT"]:
        MODE = mode
        return {"status": "ok", "mode": MODE}
    return {"status": "error", "message": "Invalid mode"}

if __name__ == "__main__":
    import uvicorn
    print(f"Alpha Governor starting in {MODE} mode")
    uvicorn.run(app, host="0.0.0.0", port=CONFIG["ws_port"])

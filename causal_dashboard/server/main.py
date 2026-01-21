import asyncio
import json
import csv
import time
from fastapi import FastAPI, WebSocket
from fastapi.middleware.cors import CORSMiddleware

app = FastAPI()
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

CLIENTS = set()
DATA_FILE = "research.csv"

async def broadcast(msg):
    for ws in list(CLIENTS):
        try:
            await ws.send_text(msg)
        except:
            CLIENTS.remove(ws)

def load_latest():
    rows = []
    try:
        with open(DATA_FILE, newline="") as f:
            reader = csv.DictReader(f)
            for r in reader:
                rows.append(r)
    except:
        return []
    return rows[-50:]

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    CLIENTS.add(ws)
    try:
        while True:
            await asyncio.sleep(1)
            rows = load_latest()
            await ws.send_text(json.dumps(rows))
    except:
        CLIENTS.remove(ws)

@app.get("/health")
def health():
    return {"status": "ok"}

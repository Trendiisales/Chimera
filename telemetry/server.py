from fastapi import FastAPI, WebSocket
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
import asyncio

from state import TelemetryState
from chimera_client import ChimeraClient

app = FastAPI()
state = TelemetryState()
chimera = ChimeraClient(state)

app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/")
async def root():
    with open("static/index.html", "r") as f:
        return HTMLResponse(f.read())

@app.get("/api/fitness")
async def fitness():
    return JSONResponse(state.snapshot()["fitness"])

@app.get("/api/correlation")
async def correlation():
    return JSONResponse(state.correlation_matrix())

@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await ws.accept()
    try:
        while True:
            chimera.tick()
            await ws.send_json({
                "snapshot": state.snapshot(),
                "correlation": state.correlation_matrix()
            })
            await asyncio.sleep(0.5)
    except:
        pass

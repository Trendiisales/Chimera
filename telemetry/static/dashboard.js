const ws = new WebSocket(`ws://${location.host}/ws`);

function renderTable(id, headers, rows, colorFn) {
    const table = document.getElementById(id);
    table.innerHTML = "";

    const thead = document.createElement("tr");
    headers.forEach(h => {
        const th = document.createElement("th");
        th.innerText = h;
        thead.appendChild(th);
    });
    table.appendChild(thead);

    rows.forEach(r => {
        const tr = document.createElement("tr");
        headers.forEach(h => {
            const td = document.createElement("td");
            td.innerText = r[h] ?? "";
            if (colorFn) {
                const c = colorFn(h, r[h], r);
                if (c) td.style.color = c;
            }
            tr.appendChild(td);
        });
        table.appendChild(tr);
    });
}

ws.onmessage = evt => {
    const data = JSON.parse(evt.data);

    const fitness = Object.values(
        data.snapshot.fitness
    );

    renderTable(
        "fitness",
        ["engine", "total_pnl", "wins", "losses", "win_rate", "max_drawdown", "healthy"],
        fitness,
        (h, v, r) => {
            if (h === "healthy") {
                return v ? "lime" : "red";
            }
            if (h === "total_pnl") {
                return v >= 0 ? "lime" : "red";
            }
            return null;
        }
    );

    const corr = data.correlation;
    const engines = Object.keys(corr);

    const corrRows = engines.map(e => {
        const row = { engine: e };
        engines.forEach(o => {
            row[o] = corr[e][o];
        });
        return row;
    });

    renderTable(
        "correlation",
        ["engine", ...engines],
        corrRows,
        (h, v) => {
            if (typeof v === "number" && Math.abs(v) >= 0.85) {
                return "orange";
            }
            return null;
        }
    );

    renderTable(
        "trades",
        ["engine", "symbol", "side", "qty", "price", "pnl", "ts"],
        data.snapshot.trades,
        (h, v) => {
            if (h === "pnl") {
                return v >= 0 ? "lime" : "red";
            }
            return null;
        }
    );

    renderTable(
        "decisions",
        ["engine", "symbol", "approved", "reason", "edge", "spread", "ts"],
        data.snapshot.decisions,
        (h, v) => {
            if (h === "approved") {
                return v ? "lime" : "red";
            }
            if (h === "reason" && v === "CORR") {
                return "orange";
            }
            if (h === "reason" && v === "FITNESS") {
                return "purple";
            }
            return null;
        }
    );
};

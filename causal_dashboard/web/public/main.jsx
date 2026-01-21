const { useState, useEffect } = React;

function Bar({ label, value }) {
  const width = Math.min(Math.abs(value) * 20, 100);
  const color = value >= 0 ? "bg-green-500" : "bg-red-500";
  return (
    <div className="mb-2">
      <div className="text-sm">{label}: {value.toFixed(4)}</div>
      <div className="w-full bg-gray-700 h-3">
        <div className={color + " h-3"} style={{ width: width + "%" }}></div>
      </div>
    </div>
  );
}

function App() {
  const [rows, setRows] = useState([]);
  const [connected, setConnected] = useState(false);

  useEffect(() => {
    const ws = new WebSocket("ws://" + window.location.hostname + ":8088/ws");
    
    ws.onopen = () => setConnected(true);
    ws.onclose = () => setConnected(false);
    
    ws.onmessage = (e) => {
      try {
        setRows(JSON.parse(e.data));
      } catch {}
    };

    return () => ws.close();
  }, []);

  return (
    <div className="p-4">
      <div className="flex justify-between items-center mb-6">
        <h1 className="text-3xl font-bold">CHIMERA — LIVE CAUSAL ATTRIBUTION</h1>
        <div className={"px-3 py-1 rounded " + (connected ? "bg-green-700" : "bg-red-700")}>
          {connected ? "LIVE" : "DISCONNECTED"}
        </div>
      </div>

      {rows.length === 0 && (
        <div className="text-gray-400 text-center mt-20">
          No data yet. Run chimera_attrib to generate research.csv
        </div>
      )}

      {rows.map((r, i) => (
        <div key={i} className="border border-gray-700 p-3 mb-3 rounded">
          <div className="mb-2 text-sm font-mono">
            Trade {r.trade_id} | {r.symbol} | Regime {r.regime} | PnL {parseFloat(r.total_pnl).toFixed(4)}
          </div>

          <Bar label="OFI" value={parseFloat(r.ofi)} />
          <Bar label="Impulse" value={parseFloat(r.impulse)} />
          <Bar label="Spread" value={parseFloat(r.spread)} />
          <Bar label="Depth" value={parseFloat(r.depth)} />
          <Bar label="Toxic" value={parseFloat(r.toxic)} />
          <Bar label="VPIN" value={parseFloat(r.vpin)} />
          <Bar label="Funding" value={parseFloat(r.funding)} />
          <Bar label="Regime" value={parseFloat(r.regime_contrib)} />
        </div>
      ))}
    </div>
  );
}

ReactDOM.render(<App />, document.getElementById('root'));

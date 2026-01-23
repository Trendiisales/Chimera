// Embedded Dashboard HTML
namespace chimera {
const char* g_dashboard_html = R"HTMLDELIM(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8"/>
<title>CHIMERA - Telemetry Dashboard</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }

body { 
    background: #0a0e14; 
    color: #8fa1b3; 
    font-family: 'SF Mono', 'Consolas', 'Monaco', monospace;
    font-size: 13px;
    padding: 20px;
}

.grid {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 16px;
    margin-bottom: 16px;
}

.panel {
    background: #14181f;
    border: 1px solid #1f2933;
    border-radius: 8px;
    padding: 16px;
}

.panel-title {
    font-size: 11px;
    font-weight: 600;
    color: #5294e2;
    text-transform: uppercase;
    letter-spacing: 1px;
    margin-bottom: 12px;
    padding-bottom: 8px;
    border-bottom: 1px solid #1f2933;
}

.metric {
    margin-bottom: 8px;
}

.metric-label {
    font-size: 10px;
    color: #6b7a8f;
    text-transform: uppercase;
}

.metric-value {
    font-size: 14px;
    font-weight: 600;
    color: #c5cdd8;
}

.positive { color: #4caf50 !important; }
.negative { color: #ef5350 !important; }

table {
    width: 100%;
    border-collapse: collapse;
    font-size: 11px;
}

th {
    text-align: left;
    padding: 8px;
    font-size: 10px;
    color: #6b7a8f;
    text-transform: uppercase;
    border-bottom: 1px solid #1f2933;
}

td {
    padding: 8px;
    border-bottom: 1px solid #1a1f29;
}

.wide {
    grid-column: 1 / -1;
}

.status {
    display: inline-block;
    padding: 4px 8px;
    border-radius: 4px;
    font-size: 10px;
    background: rgba(76, 175, 80, 0.1);
    color: #4caf50;
    border: 1px solid rgba(76, 175, 80, 0.3);
}

#connection-status {
    position: fixed;
    top: 10px;
    right: 10px;
    padding: 6px 12px;
    border-radius: 6px;
    font-size: 11px;
    font-weight: 600;
}

.connected {
    background: rgba(76, 175, 80, 0.15);
    color: #4caf50;
    border: 1px solid rgba(76, 175, 80, 0.3);
}

.disconnected {
    background: rgba(239, 83, 80, 0.15);
    color: #ef5350;
    border: 1px solid rgba(239, 83, 80, 0.3);
}
</style>
</head>
<body>

<div id="connection-status" class="disconnected">● CONNECTING</div>

<div id="dashboard"></div>

<script>
const TELEMETRY_URL = '/json';
let state = null;

function updateStatus(connected) {
    const el = document.getElementById('connection-status');
    if (connected) {
        el.className = 'connected';
        el.textContent = '● LIVE';
    } else {
        el.className = 'disconnected';
        el.textContent = '● DISCONNECTED';
    }
}

function formatNumber(n, decimals = 2) {
    return n.toFixed(decimals);
}

function render() {
    if (!state) {
        document.getElementById('dashboard').innerHTML = '<div class="panel"><div class="panel-title">Waiting for data...</div></div>';
        return;
    }

    const s = state.system;
    const l = state.latency;
    const p = state.pnl;
    const g = state.governor;
    const symbols = state.symbols || [];
    const trades = state.trades || [];

    document.getElementById('dashboard').innerHTML = `
        <div class="grid">
            <div class="panel">
                <div class="panel-title">System</div>
                <div class="metric">
                    <div class="metric-label">Mode</div>
                    <div class="metric-value">${s.mode}</div>
                </div>
                <div class="metric">
                    <div class="metric-label">Governor</div>
                    <div class="metric-value">${s.governor_mode}</div>
                </div>
                <div class="metric">
                    <div class="metric-label">Build</div>
                    <div class="metric-value">${s.build_id}</div>
                </div>
                <div class="metric">
                    <div class="metric-label">Uptime</div>
                    <div class="metric-value">${s.uptime_s}s</div>
                </div>
                <div class="metric">
                    <div class="metric-label">Kill Switch</div>
                    <div class="metric-value ${s.kill_switch ? 'negative' : 'positive'}">${s.kill_switch ? 'ACTIVE' : 'OFF'}</div>
                </div>
            </div>

            <div class="panel">
                <div class="panel-title">Latency</div>
                <div class="metric">
                    <div class="metric-label">Tick→Decision</div>
                    <div class="metric-value">${formatNumber(l.tick_to_decision_ms)} ms</div>
                </div>
                <div class="metric">
                    <div class="metric-label">Decision→Send</div>
                    <div class="metric-value">${formatNumber(l.decision_to_send_ms)} ms</div>
                </div>
                <div class="metric">
                    <div class="metric-label">Send→Ack</div>
                    <div class="metric-value">${formatNumber(l.send_to_ack_ms)} ms</div>
                </div>
                <div class="metric">
                    <div class="metric-label">Total RTT</div>
                    <div class="metric-value">${formatNumber(l.rtt_total_ms)} ms</div>
                </div>
                <div class="metric">
                    <div class="metric-label">Slippage</div>
                    <div class="metric-value">${formatNumber(l.slippage_bps)} bps</div>
                </div>
            </div>

            <div class="panel">
                <div class="panel-title">PnL</div>
                <div class="metric">
                    <div class="metric-label">Realized</div>
                    <div class="metric-value ${p.realized_bps >= 0 ? 'positive' : 'negative'}">
                        ${p.realized_bps >= 0 ? '+' : ''}${formatNumber(p.realized_bps)} bps
                    </div>
                </div>
                <div class="metric">
                    <div class="metric-label">Unrealized</div>
                    <div class="metric-value ${p.unrealized_bps >= 0 ? 'positive' : 'negative'}">
                        ${p.unrealized_bps >= 0 ? '+' : ''}${formatNumber(p.unrealized_bps)} bps
                    </div>
                </div>
                <div class="metric">
                    <div class="metric-label">Daily DD</div>
                    <div class="metric-value negative">${formatNumber(p.daily_dd_bps)} bps</div>
                </div>
                <div class="metric">
                    <div class="metric-label">Risk Limit</div>
                    <div class="metric-value">${formatNumber(p.risk_limit_bps)} bps</div>
                </div>
            </div>

            <div class="panel">
                <div class="panel-title">Governor</div>
                <div class="metric">
                    <div class="metric-label">Recommendation</div>
                    <div class="metric-value">${g.recommendation}</div>
                </div>
                <div class="metric">
                    <div class="metric-label">Confidence</div>
                    <div class="metric-value">${formatNumber(g.confidence * 100, 1)}%</div>
                </div>
                <div class="metric">
                    <div class="metric-label">Survival</div>
                    <div class="metric-value ${g.survival_bps >= 0 ? 'positive' : 'negative'}">
                        ${formatNumber(g.survival_bps)} bps
                    </div>
                </div>
                <div class="metric">
                    <div class="metric-label">Cooldown</div>
                    <div class="metric-value">${g.cooldown_s}s</div>
                </div>
            </div>
        </div>

        <div class="grid">
            <div class="panel wide">
                <div class="panel-title">Market Data</div>
                <table>
                    <thead>
                        <tr>
                            <th>Symbol</th>
                            <th>Bid</th>
                            <th>Ask</th>
                            <th>Last</th>
                            <th>Spread</th>
                            <th>OFI</th>
                            <th>Regime</th>
                            <th>Engine</th>
                            <th>Weight</th>
                            <th>Status</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${symbols.map(sym => `
                            <tr>
                                <td><strong>${sym.symbol}</strong></td>
                                <td>${formatNumber(sym.bid, 2)}</td>
                                <td>${formatNumber(sym.ask, 2)}</td>
                                <td>${formatNumber(sym.last, 2)}</td>
                                <td>${formatNumber(sym.spread_bps, 2)} bps</td>
                                <td>${formatNumber(sym.ofi, 1)}</td>
                                <td>${sym.regime}</td>
                                <td>${sym.engine}</td>
                                <td>${formatNumber(sym.capital_weight * 100, 0)}%</td>
                                <td><span class="status">${sym.enabled ? 'ENABLED' : 'DISABLED'}</span></td>
                            </tr>
                        `).join('')}
                    </tbody>
                </table>
            </div>
        </div>

        <div class="grid">
            <div class="panel wide">
                <div class="panel-title">Recent Trades</div>
                <table>
                    <thead>
                        <tr>
                            <th>ID</th>
                            <th>Time</th>
                            <th>Symbol</th>
                            <th>Engine</th>
                            <th>Side</th>
                            <th>Qty</th>
                            <th>Entry</th>
                            <th>Exit</th>
                            <th>PnL</th>
                            <th>Slippage</th>
                            <th>Latency</th>
                            <th>OFI</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${trades.slice(0, 20).map(t => `
                            <tr>
                                <td>${t.id}</td>
                                <td>${t.time}</td>
                                <td>${t.symbol}</td>
                                <td>${t.engine}</td>
                                <td class="${t.side === 'BUY' ? 'positive' : 'negative'}">${t.side}</td>
                                <td>${formatNumber(t.qty, 4)}</td>
                                <td>${formatNumber(t.entry, 2)}</td>
                                <td>${formatNumber(t.exit, 2)}</td>
                                <td class="${t.pnl_bps >= 0 ? 'positive' : 'negative'}">
                                    ${t.pnl_bps >= 0 ? '+' : ''}${formatNumber(t.pnl_bps)} bps
                                </td>
                                <td>${formatNumber(t.slippage_bps, 2)} bps</td>
                                <td>${formatNumber(t.latency_ms, 1)} ms</td>
                                <td>${formatNumber(t.signals.ofi, 1)}</td>
                            </tr>
                        `).join('')}
                    </tbody>
                </table>
            </div>
        </div>
    `;
}

async function poll() {
    try {
        const response = await fetch(TELEMETRY_URL);
        state = await response.json();
        updateStatus(true);
        render();
    } catch (err) {
        updateStatus(false);
        console.error('Telemetry fetch error:', err);
    }
}

// Poll every 500ms
setInterval(poll, 500);
poll();
</script>

</body>
</html>
)HTMLDELIM";
} // namespace chimera

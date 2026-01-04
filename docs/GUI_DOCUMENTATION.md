# Chimera HFT Dashboard v7.15 - Complete GUI Documentation

## Overview

The Chimera Dashboard is a real-time monitoring and control interface for the dual-engine HFT trading system. It connects to the trading engine via WebSocket (port 7777) and displays live market data, system status, trade execution, and configuration options.

**Access URL:** `http://45.85.3.38:8080/chimera_dashboard.html`
**WebSocket:** `ws://45.85.3.38:7777`

---

## 1. HEADER BAR

### Location: Top of screen

| Element | Description |
|---------|-------------|
| **CHIMERA** | System name |
| **Version Badge** | Current version (e.g., v7.15) - pulled from backend |
| **LIVE Badge** | Pulsing red indicator showing system is live |
| **ANTI-CHURN Badge** | Indicates anti-churn protection is active |
| **Connection Status Dot** | Green = connected, Red = disconnected |
| **Connection Text** | Shows "Connected", "Disconnected", or "Reconnecting (n)..." |
| **WebSocket URL Input** | Server address (default: ws://45.85.3.38:7777) |
| **Connect Button** | Manually reconnect to WebSocket |
| **Disconnect Button** | Manually disconnect from WebSocket |
| **⚠️ KILL Button** | **EMERGENCY STOP** - Immediately halts all trading |

### Hotkey Hints (Bottom-right corner)
- **R** = Reconnect
- **F5** = Reload page
- **Space** = Pause (toggle)

---

## 2. NETWORK LATENCY HERO

### Location: Below header, full width
### Purpose: Shows VPS-to-exchange network latency

| Element | Description |
|---------|-------------|
| **Current Latency** | Large cyan number showing real-time latency in ms |
| **MIN** | Minimum observed latency (green) |
| **AVG** | Average latency (yellow) |
| **MAX** | Maximum observed latency (red) |
| **Status Badge** | OPTIMAL / GOOD / DEGRADED / CRITICAL |

**Target:** < 0.5ms for NY co-located VPS to Binance

---

## 3. WHY NO TRADE DIAGNOSTICS (Block Banner)

### Location: Below latency hero, orange/red gradient
### Purpose: Shows **why the system is NOT trading right now**

| Element | Description |
|---------|-------------|
| **⚠️ BLOCK:** | Primary block reason (large red text) |
| **Detail** | Extended explanation of the block |
| **State** | Current market state (RANGING, TRENDING, DEAD, etc.) |
| **Intent** | Current trade intent (MEAN_REVERSION, MOMENTUM, NO_TRADE) |
| **Conv** | Conviction score (0-10) |
| **Votes** | Buy/Sell vote ratio (e.g., "4/0") |
| **Gated** | Number of orders blocked by state machine |

**Common Block Reasons:**
- `INIT` - System initializing, building EMAs
- `SHADOW_MODE` - Live data but orders disabled
- `NO_SIGNAL` - No trading opportunity detected
- `REGIME_TOXIC` - Market conditions unfavorable
- `COOLDOWN` - Post-trade cooldown active
- `VPIN_HIGH` - Toxicity too high
- `SPREAD_WIDE` - Spread exceeds threshold

---

## 4. LIVE PRICES (Left Column)

### Location: Far left, 2 columns wide
### Purpose: Real-time prices for all monitored symbols

**Organized by Asset Class:**
- 🪙 **CRYPTO** - BTCUSDT, ETHUSDT, SOLUSDT
- 💱 **FOREX** - EURUSD, GBPUSD, USDJPY, AUDUSD, USDCAD, AUDJPY, USDCHF
- 🥇 **METALS** - XAUUSD, XAGUSD
- 📈 **INDICES** - NAS100, SPX500, US30

**Each Symbol Row Shows:**
| Element | Description |
|---------|-------------|
| **Symbol Name** | e.g., BTCUSDT |
| **Price** | Current mid price |
| **Spread** | Current spread in appropriate units |
| **Latency Badge** | Color-coded latency (green/yellow/red) |
| **Fire Icon 🔥** | Shown when symbol is actively trading |

**Interactions:**
- **Click** = Select symbol for configuration
- **Double-click** = Toggle trading enabled/disabled

---

## 5. CONNECTION STATUS

### Location: Middle-left area, top
### Purpose: Exchange connection health

| Panel | Description |
|-------|-------------|
| **BINANCE** | Crypto exchange status (Connected/Disconnected) |
| **CTRADER FIX** | CFD/Forex broker FIX connection status |

Each shows:
- Status dot (green/red)
- Connection text

---

## 6. CORE METRICS

### Location: Middle-left area
### Purpose: Key trading statistics

| Metric | Description |
|--------|-------------|
| **Ticks** | Total market data ticks received this session |
| **Orders** | Total orders sent |
| **Fills** | Total orders filled |
| **PnL** | Session profit/loss in USD |
| **Gated** | Orders blocked by state machine |

---

## 7. OPPORTUNITY ALERTS

### Location: Middle-left area, yellow border
### Purpose: High-conviction setup notifications

Shows alerts when the system detects tradeable setups with high confidence. Each alert includes:
- Timestamp
- Symbol
- Direction (BUY/SELL)
- Conviction score
- Setup type

---

## 8. MICROSTRUCTURE SIGNALS

### Location: Middle-left area
### Purpose: Real-time market microstructure indicators

| Signal | Description | Range |
|--------|-------------|-------|
| **OFI** | Order Flow Imbalance - directional pressure | -1 to +1 |
| **VPIN** | Volume-Synchronized Probability of Informed Trading | 0 to 1 |
| **Spread** | Current spread in bps | 0+ |
| **Pressure** | Aggregate buy/sell pressure | -1 to +1 |

**Color Coding:**
- Green = Favorable for trading
- Yellow = Caution
- Red = Adverse / Toxic

---

## 9. BUCKET VOTING

### Location: Middle-left area
### Purpose: Multi-strategy signal aggregation

| Element | Description |
|---------|-------------|
| **BUY** | Number of strategies voting bullish (green) |
| **Signal** | Current consensus signal (BUY/SELL/NONE) |
| **SELL** | Number of strategies voting bearish (red) |

The system requires multiple strategy agreement before trading.

---

## 10. BRING-UP STATUS

### Location: Middle-left area, purple border
### Purpose: Symbol-level trading readiness

**Venue Health Table:**
| Column | Description |
|--------|-------------|
| **Symbol** | Trading symbol |
| **Venue** | Exchange (BINANCE/CTRADER) |
| **Health** | Venue health score (0-100%) |
| **Ladder** | Order book depth quality |
| **Scale** | Current position scaling factor |
| **Fills** | Recent fill count |
| **Blocker** | Current blocking reason (if any) |

**Suppression Counts:** Shows how many times each suppression type fired this session.

**Last Block Reason:** Most recent reason why a trade was blocked.

---

## 11. TRADING CONFIG (Middle-Right Column)

### Location: Center-right, 3 columns
### Purpose: Trading parameter configuration

### 11.1 PRESET BUTTONS
| Preset | Description |
|--------|-------------|
| **CONSERVATIVE** | Small sizes, tight stops, low risk |
| **BALANCED** | Default settings |
| **AGGRESSIVE** | Larger sizes, wider parameters |

### 11.2 GLOBAL LIMITS
| Parameter | Description | Default |
|-----------|-------------|---------|
| **Daily Loss** | Max daily loss before shutdown | -$500 |
| **Max DD %** | Maximum drawdown percentage | 10% |
| **Max Exposure** | Maximum position exposure | 0.05 |
| **Max Positions** | Concurrent positions allowed | 3 |

### 11.3 ASSET CLASS TABS
Click to switch between asset class defaults:
- 🪙 Crypto
- 💱 Forex
- 🥇 Metals
- 📈 Indices

### 11.4 ASSET CLASS DEFAULTS
| Parameter | Description |
|-----------|-------------|
| **Size** | Default position size |
| **SL (bps)** | Stop loss in basis points |
| **TP (bps)** | Take profit in basis points |
| **Max Spread** | Maximum allowed spread |
| **VPIN Max** | Maximum VPIN before blocking |
| **Cooldown** | Post-trade cooldown (ms) |

### 11.5 SYMBOL-SPECIFIC CONFIG
When a symbol is selected:
| Parameter | Description |
|-----------|-------------|
| **Enabled** | Checkbox to enable/disable trading |
| **Size** | Symbol-specific size override |
| **SL/TP** | Symbol-specific stops |
| **VPIN Max** | Symbol-specific VPIN limit |
| **Cooldown** | Symbol-specific cooldown |

### 11.6 CONTROL BUTTONS
| Button | Description |
|--------|-------------|
| **APPLY** | Send config to engine (runtime only) |
| **💾 SAVE TO DISK** | Persist config to config.ini |

### 11.7 ENABLED SYMBOLS PANEL
Shows all symbols that are currently enabled for trading:
- **Click** = Toggle symbol for active trading
- **Double-click** = Select for config editing
- Green highlight = Currently active for trading

---

## 12. TRADES PANEL (Right Column) ⚠️ NEEDS ENHANCEMENT

### Location: Far right, top
### Purpose: Trade execution history

**Header Stats:**
| Stat | Description |
|------|-------------|
| **Trade Count** | Number of trades this session |
| **PnL** | Session profit/loss |
| **W** | Win count (green) |
| **L** | Loss count (red) |
| **Win Rate** | Win percentage (yellow) |
| **📥** | Export trades to CSV |
| **🗑️** | Clear trade history |

**Trade Columns:**
| Column | Description |
|--------|-------------|
| **Time** | Execution timestamp |
| **Symbol** | Trading pair |
| **Side** | BUY or SELL |
| **Qty** | Position quantity |
| **Price** | Execution price |
| **PnL** | Trade profit/loss |

**Current Limitation:** Trades panel exists but may not show trades during SHADOW mode since no actual orders are executed.

---

## 13. PROCESSING LATENCY

### Location: Right column
### Purpose: Internal engine performance

| Metric | Description |
|--------|-------------|
| **AVG μs** | Average tick-to-decision latency |
| **MIN μs** | Best case latency |
| **MAX μs** | Worst case latency |
| **P99 μs** | 99th percentile latency |

**Target:** < 100μs average for HFT performance

---

## 14. REGIME & SYSTEM

### Location: Right column, two side-by-side panels

**REGIME Panel:**
| Element | Description |
|---------|-------------|
| **Session** | Current trading session (ASIA/LONDON/NY) |
| **Trending** | YES/NO - Is market trending? |
| **Volatile** | YES/NO - Is market volatile? |
| **Veto** | CLEAR or reason for regime veto |

**SYSTEM Panel:**
| Element | Description |
|---------|-------------|
| **Heartbeat** | Engine heartbeat counter |
| **Loop** | Main loop time |
| **Uptime** | System uptime (e.g., "5m 32s") |
| **DD Buffer** | Remaining drawdown buffer (%) |

---

## 15. ML FEATURE LOGGER

### Location: Right column, green border
### Purpose: Machine learning data capture status

| Metric | Description |
|--------|-------------|
| **Features/sec** | ML feature capture rate |
| **Total logged** | Total feature rows captured |
| **Labels ready** | Labeled samples for training |
| **Model score** | Current model performance |

Shows which features are being captured (OFI, VPIN, spread, vol_z, trend, momentum).

---

## 16. LIVE DIAGNOSTICS

### Location: Right column, red border
### Purpose: Real-time debug console

Shows timestamped log entries with severity levels:
- **INFO** (cyan) - Normal operations
- **WARN** (yellow) - Warnings
- **ERROR** (red) - Errors
- **TRADE** (green) - Trade executions
- **BLOCK** (orange) - Blocked orders

**Clear Button:** Clears the diagnostic log.

---

## 17. MARKET STATE MACHINE

### Location: Right column, bottom, purple border
### Purpose: Current state machine status

| Element | Description |
|---------|-------------|
| **State** | Current market state (DEAD, RANGING, TRENDING, BREAKOUT, etc.) |
| **Intent** | Trade intent (NO_TRADE, MEAN_REVERSION, MOMENTUM, BREAKOUT) |
| **Conviction** | Confidence score (0-10) |
| **Size Mult** | Position size multiplier based on conditions |
| **Reason** | Detailed explanation of current state |

**State Colors:**
- DEAD = Red
- RANGING = Yellow
- TRENDING = Cyan
- BREAKOUT = Green

---

## KEYBOARD SHORTCUTS

| Key | Action |
|-----|--------|
| **R** | Reconnect WebSocket |
| **F5** | Reload page |
| **Space** | Pause/Resume data updates |

---

## DATA PERSISTENCE

The following settings are saved to localStorage:
- Selected symbol (`chimera_selected_symbol`)
- Active trading symbols (`chimera_active_trading`)

---

## MISSING/FUTURE ENHANCEMENTS

### ⚠️ TRADES WINDOW ENHANCEMENT NEEDED

The current trades panel shows:
- Basic trade log with Time, Symbol, Side, Qty, Price, PnL
- Session stats (wins/losses/PnL)
- Export functionality

**Suggested Additions:**
1. **Live Trade Ticker** - Flash new trades prominently
2. **Trade Sound Effects** - Audio notification on fills
3. **Position Summary** - Current open positions with unrealized PnL
4. **Recent Trades Highlight** - Color-code last N trades
5. **Trade Details Popup** - Click trade for full details
6. **Running PnL Chart** - Mini chart of session PnL curve
7. **Entry/Exit Linking** - Show round-trip trades together

---

## WebSocket Message Format

The dashboard receives JSON messages containing:
```json
{
  "version": "v7.15",
  "symbols": [...],
  "latency": {...},
  "quality": {...},
  "regime": {...},
  "buckets": {...},
  "market_state": {...},
  "stats": {...},
  "execution": {...},
  "system": {...},
  "trade": {...}  // Only present on trade events
}
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| **Dashboard shows "Disconnected"** | Click Connect or press R |
| **Prices not updating** | Check engine is running, check WebSocket URL |
| **Version shows old** | Hard refresh (Ctrl+Shift+R), rebuild engine |
| **Uptime shows 0s** | Rebuild engine with v7.15 fix |
| **No trades showing** | System is in SHADOW mode - no real orders |
| **All symbols blocked** | Check Block banner for reason |

---

## File Locations

- **Dashboard HTML:** `~/Chimera/chimera_dashboard.html`
- **Config File:** `~/Chimera/config.ini`
- **Trade Logs:** `~/Chimera/trades/`
- **ML Features:** `~/Chimera/ml_features/`
- **Post-Mortems:** `~/Chimera/postmortem.csv`

---

*Document Version: v7.15*
*Last Updated: 2024-12-25*

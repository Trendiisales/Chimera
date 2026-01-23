#include "chimera/infra/Clock.hpp"
#include "chimera/SymbolLane.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <random>
#include <thread>

std::string getTimestamp() {
    auto now = chimera::infra::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;
    
    std::time_t t = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now()
    );
    
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%S")
        << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";
    return oss.str();
}

Fill ShadowVenue::execute(const Order& o, double bid, double ask) {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> qdelay(100, 400);  // microseconds
    std::uniform_real_distribution<double> slip_bps(0.0, 2.0);

    Fill f;
    f.order_id = o.id;
    f.ts_ack_ns = now_ns();
    
    // Simulate exchange queue time
    std::this_thread::sleep_for(std::chrono::microseconds(qdelay(rng)));

    double base = (o.side == Side::BUY) ? ask : bid;
    double slip = base * (slip_bps(rng) / 10000.0);

    f.price = (o.side == Side::BUY) ? base + slip : base - slip;
    f.qty = o.qty;
    f.ts_fill_ns = now_ns();
    
    return f;
}

Lane::Lane(std::string sym, uint32_t hash)
    : symbol_(std::move(sym)),
      symbol_hash_(hash),
      market_bus_(),
      survival_(market_bus_),
      cost_gate_(survival_) {
    std::cout << "[LANE] " << symbol_ << " initialized (hash=0x"
              << std::hex << symbol_hash_ << std::dec << ")" << std::endl;

    chimera::FeeModel f;
    f.maker_bps = 0.2;
    f.taker_bps = 1.0;
    survival_.setFeeModel(f);
    survival_.setMinSurvivalBps(6.5);
}


void Lane::on_tick(const chimera::MarketTick& t) {
    // Routing safety check
    if (t.symbol_hash != symbol_hash_) {
        std::cerr << "[LANE_MISMATCH] " << symbol_ << " got " << t.symbol << std::endl;
        return;
    }
    
    if (t.bid <= 0 || t.ask <= 0) return;
    
    double mid = (t.bid + t.ask) / 2.0;
    double spread_bps = ((t.ask - t.bid) / mid) * 10000.0;
    
    // Update GUI state with live market data
    {
        auto& gui = chimera::GuiState::instance();
        std::lock_guard<std::mutex> lock(gui.mtx);
        
        bool found = false;
        for (auto& s : gui.symbols) {
            if (s.hash == symbol_hash_) {
                s.bid = t.bid;
                s.ask = t.ask;
                s.last = t.last;
                s.spread_bps = spread_bps;
                s.depth = t.bid_size + t.ask_size;
                s.regime = (warmup_ticks_ < 100) ? "WARMUP" : "LIVE";
                s.enabled = true;
                found = true;
                break;
            }
        }
        
        if (!found) {
            chimera::SymbolState s;
            s.symbol = symbol_;
            s.hash = symbol_hash_;
            s.bid = t.bid;
            s.ask = t.ask;
            s.last = t.last;
            s.spread_bps = spread_bps;
            s.depth = t.bid_size + t.ask_size;
            s.engine = symbol_;
            s.regime = "WARMUP";
            s.capital_weight = 1.0;
            s.enabled = true;
            gui.symbols.push_back(s);
        }
        
        gui.system.uptime_s++;
        gui.system.build_id = "CHIMERA_v3.0_INSTRUMENTED";
    }
    
    // Warmup period
    if (warmup_ticks_ < 100) {
        warmup_ticks_++;
        last_mid_ = mid;
        
        if (warmup_ticks_ % 20 == 0) {
            std::cout << "[LANE] " << symbol_ << " warmup: " 
                      << warmup_ticks_ << "/100" << std::endl;
        }
        return;
    }
    
    if (warmup_ticks_ == 100) {
        std::cout << "[LANE] " << symbol_ << " LIVE" << std::endl;
        warmup_ticks_++;
    }
    
    // DECISION POINT - timestamp it
    uint64_t decision_ns = now_ns();
    
    Signal sig = strategy_.evaluate(t);
    
    if (!sig.fire) return;
    
    double qty = (sig.side == Side::BUY) ? 0.1 : -0.1;
    
    // === EDGE ESTIMATE (expected bps proxy) ===
    double expected_edge_bps = (sig.confidence * 10.0);
    double latency_ms = (now_ns() - t.ts_ns) / 1000000.0;
    bool is_maker = false;

    auto gate =
        cost_gate_.evaluate(
            symbol_,
            is_maker,
            expected_edge_bps,
            std::abs(qty),
            latency_ms
        );

    {
        auto& gui = chimera::GuiState::instance();
        std::lock_guard<std::mutex> lock(gui.mtx);
        for (auto& s : gui.symbols) {
            if (s.hash == symbol_hash_) {
                s.edge_bps = gate.edge_bps;
                s.cost_bps = gate.cost_bps;
                s.margin_bps = gate.margin_bps;
                s.enabled = gate.pass;
                break;
            }
        }
    }

    if (!gate.pass) {
        std::cout << "[COST_GATE] BLOCKED "
                  << symbol_
                  << " edge=" << gate.edge_bps
                  << " cost=" << gate.cost_bps
                  << " margin=" << gate.margin_bps
                  << " reason=" << gate.reason
                  << std::endl;
        return;
    }

    if (!risk_.allow(qty)) {
        std::cout << "[RISK] BLOCKED " << symbol_ << std::endl;
        return;
    }
    
    // CREATE ORDER - timestamp it
    Order o;
    o.id = next_order_id_++;
    o.symbol = symbol_;
    o.symbol_hash = symbol_hash_;
    o.side = sig.side;
    o.qty = std::abs(qty);
    o.ts_created_ns = now_ns();
    
    // EXECUTE (shadow venue simulates exchange latency)
    Fill f = venue_.execute(o, t.bid, t.ask);
    
    // Calculate PnL
    double pnl = (sig.side == Side::BUY)
        ? (mid - f.price) * f.qty
        : (f.price - mid) * f.qty;
    
    risk_.on_fill(pnl, qty);
    trade_count_++;
    
    // CALCULATE REAL LATENCY (nanoseconds)
    uint64_t tick_to_decision = decision_ns - t.ts_ns;
    uint64_t decision_to_order = o.ts_created_ns - decision_ns;
    uint64_t order_to_ack = f.ts_ack_ns - o.ts_created_ns;
    uint64_t ack_to_fill = f.ts_fill_ns - f.ts_ack_ns;
    uint64_t rtt_total = f.ts_fill_ns - t.ts_ns;
    
    // Update GUI state with trade and latency
    {
        auto& gui = chimera::GuiState::instance();
        std::lock_guard<std::mutex> lock(gui.mtx);
        
        // Record trade
        chimera::TradeState trade;
        trade.id = trade_count_;
        trade.time = getTimestamp();
        trade.symbol = symbol_;
        trade.engine = symbol_;
        trade.side = (sig.side == Side::BUY) ? "BUY" : "SELL";
        trade.qty = f.qty;
        trade.entry = (sig.side == Side::BUY) ? f.price : mid;
        trade.exit = (sig.side == Side::BUY) ? mid : f.price;
        trade.pnl_bps = (pnl / mid) * 10000.0;
        trade.slippage_bps = ((f.price - mid) / mid) * 10000.0;
        trade.latency_ms = rtt_total / 1000000.0;
        trade.regime = "LIVE";
        
        trade.signals.ofi = (t.bid_size - t.ask_size) / (t.bid_size + t.ask_size);
        trade.signals.impulse = 0.0;
        trade.signals.funding = 0.0;
        trade.signals.volatility = spread_bps / 10.0;
        trade.signals.correlation = 0.0;
        trade.signals.levels = 0.0;
        
        gui.trades.insert(gui.trades.begin(), trade);
        if (gui.trades.size() > 50) {
            gui.trades.pop_back();
        }
        
        // Update PnL
        gui.pnl.realized_bps += trade.pnl_bps;
        gui.pnl.unrealized_bps = 0.0;
        
        if (gui.pnl.realized_bps < gui.pnl.daily_dd_bps) {
            gui.pnl.daily_dd_bps = gui.pnl.realized_bps;
        }
        
        // Update REAL latency metrics (convert ns to ms)
        gui.latency.tick_to_decision_ms = tick_to_decision / 1000000.0;
        gui.latency.decision_to_send_ms = decision_to_order / 1000000.0;
        gui.latency.send_to_ack_ms = order_to_ack / 1000000.0;
        gui.latency.ack_to_fill_ms = ack_to_fill / 1000000.0;
        gui.latency.rtt_total_ms = rtt_total / 1000000.0;
        gui.latency.slippage_bps = trade.slippage_bps;
        
        // Update governor
        gui.governor.recommendation = risk_.kill ? "STOP" : "TRADE";
        gui.governor.confidence = sig.confidence;
        gui.governor.survival_bps = gui.pnl.realized_bps;
    }
    
    std::cout << "[TRADE] " << symbol_
              << " #" << trade_count_
              << " " << (sig.side == Side::BUY ? "BUY" : "SELL")
              << " @ " << f.price
              << " PnL=" << std::fixed << std::setprecision(2) << pnl
              << " Pos=" << risk_.position
              << " RTT=" << rtt_total / 1000 << "us"
              << (risk_.kill ? " KILLED" : "") << std::endl;
    
    std::cout << "[LATENCY] "
              << "t2d=" << tick_to_decision / 1000 << "us "
              << "d2o=" << decision_to_order / 1000 << "us "
              << "o2a=" << order_to_ack / 1000 << "us "
              << "a2f=" << ack_to_fill / 1000 << "us "
              << "RTT=" << rtt_total / 1000 << "us" << std::endl;
}

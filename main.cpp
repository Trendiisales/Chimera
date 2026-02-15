/*
 * Chimera Production - Institutional Trading System
 * 
 * DURABILITY GUARANTEES:
 * - Crash-safe binary journal (fsync, atomic writes, CRC32)
 * - FIX sequence persistence (session recovery)
 * - State snapshots (fast restart)
 * - File rotation (100MB segments)
 * - Replay engine (CRC validation)
 */

#include <iostream>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <map>
#include <string>
#include <vector>
#include <array>
#include <chrono>
#include <mutex>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <pthread.h>
#include <signal.h>

// Persistence layer
#include "persistence/BinaryJournal.hpp"
#include "persistence/FixSequenceStore.hpp"
#include "persistence/StateSnapshot.hpp"
#include "persistence/ReplayEngine.hpp"

using namespace std;

static atomic<bool> RUNNING{true};

// ============================================================================
// SIGNAL HANDLING
// ============================================================================
void signal_handler(int signum)
{
    cout << "\n[SIGNAL] Received signal " << signum << ", shutting down gracefully..." << endl;
    RUNNING = false;
}

// ============================================================================
// UTILITY
// ============================================================================
static void pin_thread(int core)
{
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

static long now_ms()
{
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now().time_since_epoch()).count();
}

static uint64_t now_ns()
{
    return chrono::duration_cast<chrono::nanoseconds>(
        chrono::steady_clock::now().time_since_epoch()).count();
}

// ============================================================================
// FIX MESSAGE BUILDER
// ============================================================================
static string fix_build(const string& body)
{
    string header = "8=FIX.4.4|9=";
    string temp = body;
    for (auto& c : temp) if (c == '|') c = 0x01;
    string bodylen = to_string(temp.size());
    string msg = header + bodylen + "|" + body;
    for (auto& c : msg) if (c == '|') c = 0x01;
    int chksum = 0;
    for (char c : msg) chksum += (unsigned char)c;
    chksum %= 256;
    char buf[8];
    snprintf(buf, sizeof(buf), "%03d", chksum);
    msg += string("10=") + buf + char(0x01);
    return msg;
}

// ============================================================================
// CLORDID GENERATOR - Unique with Entropy
// ============================================================================
class ClOrdIDGenerator
{
public:
    ClOrdIDGenerator() : rng(random_device{}()), dist(0, 999999) {}

    string generate()
    {
        uint64_t ts = now_ns();
        int entropy = dist(rng);
        return "ORD" + to_string(ts) + "_" + to_string(entropy);
    }

private:
    mt19937 rng;
    uniform_int_distribution<int> dist;
};

// ============================================================================
// CORE DATA STRUCTURES
// ============================================================================
struct MarketData { double bid = 0; double ask = 0; };
struct Position { double size = 0; double avg = 0; };
struct PnL { double realized = 0; double unreal = 0; };

struct EngineSignal
{
    bool valid = false;
    int direction = 0;
    double confidence = 0.0;
};

struct L2Level
{
    double price;
    double size;
};

// ============================================================================
// PACKED EVENT STRUCTURES
// ============================================================================
#pragma pack(push, 1)
struct TickEvent
{
    char symbol[8];
    double bid;
    double ask;
    double bid_size;
    double ask_size;
};

struct FillEvent
{
    char symbol[8];
    char side;  // 'B' or 'S'
    double price;
    double qty;
    double fee;
};

struct SignalEvent
{
    char symbol[8];
    int8_t direction;
    double confidence;
};
#pragma pack(pop)

// ============================================================================
// ENGINE INTERFACE
// ============================================================================
class IEngine
{
public:
    virtual ~IEngine() = default;
    virtual EngineSignal on_tick(double bid, double ask) = 0;
};

// ============================================================================
// CONTROL COMPONENTS
// ============================================================================
class CapitalAllocator
{
public:
    void register_symbol(const string& s, double limit)
    {
        limits[s] = limit;
        daily_loss[s] = 0;
    }
    
    bool allow(const string& s)
    {
        return daily_loss[s] > -limits[s];
    }
    
    void realize(const string& s, double pnl)
    {
        daily_loss[s] += pnl;
    }
    
    double loss(const string& s) { return daily_loss[s]; }

private:
    unordered_map<string, double> limits;
    unordered_map<string, double> daily_loss;
};

class ExecutionGovernor
{
public:
    void update(const string& s, double rtt, bool connected)
    {
        metrics[s] = {rtt, connected};
    }
    
    bool allow(const string& s)
    {
        return metrics[s].connected && metrics[s].rtt < 25.0;
    }

private:
    struct M { double rtt; bool connected; };
    unordered_map<string, M> metrics;
};

class ShadowGate
{
public:
    void set(bool s) { shadow.store(s); }
    bool allow_live() { return !shadow.load(); }
    bool is_shadow() { return shadow.load(); }

private:
    atomic<bool> shadow{true};
};

// ============================================================================
// ORDER BOOK (L2)
// ============================================================================
class OrderBook
{
public:
    void update_bid(double price, double size)
    {
        bids[price] = size;
        trim_book(bids);
    }

    void update_ask(double price, double size)
    {
        asks[price] = size;
        trim_book(asks);
    }

    double estimate_slippage(double qty, bool is_buy)
    {
        double filled = 0;
        double cost = 0;

        if (is_buy)
        {
            for (auto& [price, size] : asks)
            {
                double take = min(qty - filled, size);
                cost += take * price;
                filled += take;
                if (filled >= qty) break;
            }
        }
        else
        {
            for (auto it = bids.rbegin(); it != bids.rend(); ++it)
            {
                double take = min(qty - filled, it->second);
                cost += take * it->first;
                filled += take;
                if (filled >= qty) break;
            }
        }

        if (filled == 0) return 0;
        return (cost / filled);
    }

private:
    void trim_book(map<double, double>& book)
    {
        if (book.size() > 10)
        {
            auto it = book.begin();
            book.erase(it);
        }
    }

    map<double, double> bids;
    map<double, double> asks;
};

// ============================================================================
// SYMBOL CONTROLLER
// ============================================================================
class SymbolController
{
public:
    SymbolController(const string& s, double tick, BinaryJournal* journal)
        : symbol(s), tick_size(tick), journal_(journal)
    {}

    void register_engine(unique_ptr<IEngine> e)
    {
        engines.push_back(move(e));
    }

    void on_market(double bid, double ask, double bid_sz, double ask_sz)
    {
        lock_guard<mutex> lg(mx);

        md.bid = bid;
        md.ask = ask;

        book.update_bid(bid, bid_sz);
        book.update_ask(ask, ask_sz);

        update_unreal();

        // Log to journal
        if (journal_)
        {
            TickEvent evt{};
            strncpy(evt.symbol, symbol.c_str(), 7);
            evt.bid = bid;
            evt.ask = ask;
            evt.bid_size = bid_sz;
            evt.ask_size = ask_sz;

            vector<uint8_t> payload((uint8_t*)&evt, (uint8_t*)&evt + sizeof(evt));
            journal_->write_event(1, payload);  // Type 1 = Tick
        }
    }

    void evaluate(CapitalAllocator& cap,
                  ExecutionGovernor& exec,
                  ShadowGate& shadow,
                  function<void(const string&, char, double, const string&)> on_fill_live)
    {
        lock_guard<mutex> lg(mx);

        if (!cap.allow(symbol)) return;
        if (!exec.allow(symbol)) return;

        auto signal = aggregate_signals();
        if (!signal.valid) return;

        // Log signal
        if (journal_)
        {
            SignalEvent evt{};
            strncpy(evt.symbol, symbol.c_str(), 7);
            evt.direction = signal.direction;
            evt.confidence = signal.confidence;

            vector<uint8_t> payload((uint8_t*)&evt, (uint8_t*)&evt + sizeof(evt));
            journal_->write_event(3, payload);  // Type 3 = Signal
        }

        double qty = 1.0;

        if (signal.direction > 0 && pos.size == 0)
        {
            execute(cap, shadow, on_fill_live, 'B', qty);
        }
        else if (signal.direction < 0 && pos.size > 0)
        {
            execute(cap, shadow, on_fill_live, 'S', qty);
        }
    }

    PnL get() const 
    { 
        lock_guard<mutex> lg(mx);
        return pnl; 
    }

    SymbolSnapshot get_snapshot() const
    {
        lock_guard<mutex> lg(mx);
        return {1, pos.size, pos.avg, pnl.realized, 0, now_ns()};  // version=1
    }

    void restore_snapshot(const SymbolSnapshot& snap)
    {
        lock_guard<mutex> lg(mx);
        pos.size = snap.pos_size;
        pos.avg = snap.pos_avg;
        pnl.realized = snap.realized;
        cout << "[" << symbol << "] Restored from snapshot: pos=" << pos.size << " realized=" << pnl.realized << endl;
    }

private:
    EngineSignal aggregate_signals()
    {
        double score = 0;
        int dir = 0;

        for (auto& e : engines)
        {
            auto s = e->on_tick(md.bid, md.ask);
            if (!s.valid) continue;

            score += s.confidence;
            dir = s.direction;
        }

        if (score > 1.0)
            return {true, dir, score};

        return {};
    }

    void execute(CapitalAllocator& cap,
                 ShadowGate& shadow,
                 function<void(const string&, char, double, const string&)>& on_fill_live,
                 char side,
                 double qty)
    {
        double fill_price = 0;

        if (shadow.is_shadow())
        {
            // Shadow execution
            fill_price = book.estimate_slippage(qty, side == 'B');
            if (fill_price == 0)
                fill_price = (side == 'B') ? md.ask : md.bid;
        }
        else
        {
            // Live execution path
            string clordid = "ORD" + to_string(now_ns());
            on_fill_live(symbol, side, qty, clordid);
            return;  // Wait for fill callback
        }

        // Process fill
        double fee = 0.0002 * fill_price * qty;

        if (side == 'B')
        {
            pos.size = qty;
            pos.avg = fill_price;
        }
        else
        {
            double gross = (fill_price - pos.avg) * qty;
            double net = gross - fee;
            pnl.realized += net;
            cap.realize(symbol, net);

            pos.size = 0;
            pos.avg = 0;
        }

        update_unreal();

        // Log fill
        if (journal_)
        {
            FillEvent evt{};
            strncpy(evt.symbol, symbol.c_str(), 7);
            evt.side = side;
            evt.price = fill_price;
            evt.qty = qty;
            evt.fee = fee;

            vector<uint8_t> payload((uint8_t*)&evt, (uint8_t*)&evt + sizeof(evt));
            journal_->write_event(2, payload);  // Type 2 = Fill
        }
    }

    void update_unreal()
    {
        if (pos.size != 0)
            pnl.unreal = (md.bid - pos.avg) * pos.size;
        else
            pnl.unreal = 0;
    }

    string symbol;
    double tick_size;

    MarketData md;
    OrderBook book;

    Position pos;
    PnL pnl;

    vector<unique_ptr<IEngine>> engines;
    BinaryJournal* journal_;

    mutable mutex mx;
};

// ============================================================================
// FIX SESSION
// ============================================================================
class FixSession
{
public:
    FixSession(const string& sender, const string& subid, const string& name)
        : sender_(sender), subid_(subid), name_(name)
    {}

    bool connect_ssl(const string& ip, int port)
    {
        SSL_load_error_strings();
        SSL_library_init();
        ctx = SSL_CTX_new(TLS_client_method());
        sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        if (::connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) return false;
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, sock);
        if (SSL_connect(ssl) <= 0) return false;
        connected = true;
        cout << "[FIX:" << name_ << "] Connected to " << ip << ":" << port << endl;
        return true;
    }

    void send_logon()
    {
        string user = getenv("FIX_USERNAME") ? getenv("FIX_USERNAME") : "";
        string pass = getenv("FIX_PASSWORD") ? getenv("FIX_PASSWORD") : "";
        string body =
            "35=A|49=" + sender_ +
            "|56=Server|34=" + to_string(++seq_out) +
            "|98=0|108=30|553=" + user +
            "|554=" + pass +
            "|57=" + subid_ + "|";
        send_raw(fix_build(body));
        cout << "[FIX:" << name_ << "] Sent Logon (seq=" << seq_out << ")" << endl;
    }

    void send_md_request(const string& symbol)
    {
        string body =
            "35=V|49=" + sender_ +
            "|56=Server|34=" + to_string(++seq_out) +
            "|262=MD" + symbol +
            "|263=1|264=1|146=1|55=" + symbol +
            "|267=2|269=0|269=1|";
        send_raw(fix_build(body));
        cout << "[FIX:" << name_ << "] Requested MarketData for " << symbol << " (seq=" << seq_out << ")" << endl;
    }

    void send_new_order_single(const string& symbol, char side, double qty, const string& clordid)
    {
        string body =
            "35=D|49=" + sender_ +
            "|56=Server|34=" + to_string(++seq_out) +
            "|11=" + clordid +
            "|55=" + symbol +
            "|54=" + string(1, side) +
            "|38=" + to_string(qty) +
            "|40=1|59=0|";  // Market order, Day
        send_raw(fix_build(body));
        cout << "[FIX:" << name_ << "] Sent NewOrderSingle: " << clordid << " " << symbol << " " << side << " " << qty << endl;
    }

    void reader(function<void(const string&)> cb)
    {
        pin_thread(0);
        cout << "[FIX:" << name_ << "] Reader thread started on core 0" << endl;
        char buf[8192];
        while (RUNNING)
        {
            int n = SSL_read(ssl, buf, sizeof(buf));
            if (n > 0)
            {
                last_rx = now_ms();
                cb(string(buf, n));
            }
        }
    }

    bool is_connected() { return connected; }
    long latency() { return now_ms() - last_rx; }
    uint64_t current_seq() const { return seq_out; }
    void set_seq(uint64_t seq) { seq_out = seq; }

private:
    void send_raw(const string& m)
    {
        SSL_write(ssl, m.data(), m.size());
    }

    string sender_;
    string subid_;
    string name_;
    SSL_CTX* ctx{};
    SSL* ssl{};
    int sock{};
    bool connected = false;
    long last_rx = 0;
    uint64_t seq_out = 0;
};

// ============================================================================
// FIX PARSER
// ============================================================================
class FixParser
{
public:
    static bool parse_snapshot(const string& raw, string& symbol, double& bid, double& ask)
    {
        if (raw.find("35=W") == string::npos) return false;

        vector<string> fields;
        string temp;
        for (char c : raw)
        {
            if (c == 0x01) { fields.push_back(temp); temp.clear(); }
            else temp += c;
        }

        string sym;
        double b = 0, a = 0;

        for (auto& f : fields)
        {
            if (f.rfind("55=", 0) == 0) sym = f.substr(3);
        }

        for (size_t i = 0; i < fields.size(); ++i)
        {
            if (fields[i].rfind("269=", 0) == 0)
            {
                int type = stoi(fields[i].substr(4));
                for (size_t j = i; j < fields.size(); ++j)
                {
                    if (fields[j].rfind("270=", 0) == 0)
                    {
                        double px = stod(fields[j].substr(4));
                        if (type == 0) b = px;
                        if (type == 1) a = px;
                        break;
                    }
                }
            }
        }

        if (sym.empty()) return false;
        symbol = sym;
        bid = b;
        ask = a;
        return true;
    }
};

// ============================================================================
// SIMPLE ENGINE WRAPPER
// ============================================================================
class MicroImpulseEngineWrapper : public IEngine
{
public:
    EngineSignal on_tick(double bid, double ask) override
    {
        EngineSignal s;
        double spread = ask - bid;
        
        if (spread < 0.5 && bid > last_bid)
        {
            s.valid = true;
            s.direction = 1;
            s.confidence = 0.6;
        }
        
        last_bid = bid;
        return s;
    }

private:
    double last_bid = 0;
};

// ============================================================================
// TELEMETRY
// ============================================================================
class Telemetry
{
public:
    Telemetry(int port) : port_(port) {}
    
    void start() { thr = thread(&Telemetry::run, this); }
    
    void update(const string& j)
    {
        lock_guard<mutex> lg(mx);
        json = j;
    }

private:
    void run()
    {
        pin_thread(3);
        cout << "[TELEMETRY] HTTP server started on port " << port_ << " (core 3)" << endl;
        
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_);
        bind(server_fd, (sockaddr*)&address, sizeof(address));
        listen(server_fd, 5);
        
        while (RUNNING)
        {
            int addrlen = sizeof(address);
            int new_socket = accept(server_fd, (sockaddr*)&address, (socklen_t*)&addrlen);
            if (new_socket < 0) continue;
            
            string body;
            {
                lock_guard<mutex> lg(mx);
                body = json;
            }
            
            string resp =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: " + to_string(body.size()) + "\r\n\r\n" + body;
            
            send(new_socket, resp.c_str(), resp.size(), 0);
            close(new_socket);
        }
    }

    int port_;
    thread thr;
    string json;
    mutex mx;
};

// ============================================================================
// MAIN
// ============================================================================
int main()
{
    // Signal handling (SIGINT and SIGTERM)
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    cout << "============================================================" << endl;
    cout << "  CHIMERA PRODUCTION - Institutional Trading System" << endl;
    cout << "============================================================" << endl;
    cout << "  Persistence:  Crash-safe journal + FIX seq + Snapshots" << endl;
    cout << "  Durability:   fsync, atomic writes, CRC32, file rotation" << endl;
    cout << "  Mode:         SHADOW (safe testing)" << endl;
    cout << "============================================================" << endl << endl;

    // Persistence layer
    BinaryJournal journal("events");
    FixSequenceStore seq_store;
    StateSnapshot snapshot;

    // Load previous state
    auto seq_state = seq_store.load();
    auto xau_snap = snapshot.load("XAUUSD");
    auto xag_snap = snapshot.load("XAGUSD");

    // Control spine
    CapitalAllocator capital;
    ExecutionGovernor exec;
    ShadowGate shadow;
    shadow.set(true);

    capital.register_symbol("XAUUSD", 1000);
    capital.register_symbol("XAGUSD", 600);

    // FIX sessions
    FixSession quote("live.blackbull.8077780", "QUOTE", "QUOTE");
    FixSession trade("live.blackbull.8077780", "TRADE", "TRADE");

    // Restore FIX sequences
    quote.set_seq(seq_state.quote_seq);
    trade.set_seq(seq_state.trade_seq);

    if (!quote.connect_ssl("76.223.4.250", 5211))
    {
        cerr << "[ERROR] Failed to connect QUOTE" << endl;
        return 1;
    }

    trade.connect_ssl("76.223.4.250", 5212);

    quote.send_logon();
    trade.send_logon();
    
    this_thread::sleep_for(chrono::seconds(1));
    
    quote.send_md_request("XAUUSD");
    quote.send_md_request("XAGUSD");

    // Symbol controllers
    SymbolController xau("XAUUSD", 0.01, &journal);
    SymbolController xag("XAGUSD", 0.01, &journal);

    // Restore snapshots
    xau.restore_snapshot(xau_snap);
    xag.restore_snapshot(xag_snap);

    cout << "[XAUUSD] Registering engines" << endl;
    xau.register_engine(make_unique<MicroImpulseEngineWrapper>());
    
    cout << "[XAGUSD] Registering engines" << endl;
    xag.register_engine(make_unique<MicroImpulseEngineWrapper>());

    Telemetry telemetry(8080);
    telemetry.start();

    // FIX reader
    thread reader([&]() {
        quote.reader([&](const string& raw) {
            string sym;
            double bid = 0, ask = 0;
            
            if (FixParser::parse_snapshot(raw, sym, bid, ask))
            {
                double depth = 100.0;
                
                if (sym == "XAUUSD")
                    xau.on_market(bid, ask, depth, depth);
                if (sym == "XAGUSD")
                    xag.on_market(bid, ask, depth, depth);
            }
        });
    });

    // Gold thread
    ClOrdIDGenerator clordid_gen;
    thread gold_thread([&]() {
        pin_thread(1);
        cout << "[XAU] Engine thread started on core 1" << endl;
        
        auto on_fill_live = [&](const string& symbol, char side, double qty, const string& clordid) {
            if (!shadow.is_shadow())
            {
                trade.send_new_order_single(symbol, side, qty, clordid);
            }
        };

        while (RUNNING)
        {
            this_thread::sleep_for(chrono::milliseconds(500));
            exec.update("XAUUSD", quote.latency(), quote.is_connected());
            xau.evaluate(capital, exec, shadow, on_fill_live);
        }
    });

    // Silver thread
    thread silver_thread([&]() {
        pin_thread(2);
        cout << "[XAG] Engine thread started on core 2" << endl;
        
        auto on_fill_live = [&](const string& symbol, char side, double qty, const string& clordid) {
            if (!shadow.is_shadow())
            {
                trade.send_new_order_single(symbol, side, qty, clordid);
            }
        };

        while (RUNNING)
        {
            this_thread::sleep_for(chrono::milliseconds(700));
            exec.update("XAGUSD", quote.latency(), quote.is_connected());
            xag.evaluate(capital, exec, shadow, on_fill_live);
        }
    });

    // Snapshot saver thread
    thread snapshot_saver([&]() {
        while (RUNNING)
        {
            this_thread::sleep_for(chrono::seconds(60));  // Every 60 seconds
            
            snapshot.save("XAUUSD", xau.get_snapshot());
            snapshot.save("XAGUSD", xag.get_snapshot());
            
            FixSeqState state;
            state.quote_seq = quote.current_seq();
            state.trade_seq = trade.current_seq();
            seq_store.save(state);
        }
    });

    // Monitoring
    cout << endl << "[SYSTEM] All threads started. Monitoring active..." << endl << endl;

    while (RUNNING)
    {
        this_thread::sleep_for(chrono::seconds(1));
        
        auto xau_pnl = xau.get();
        auto xag_pnl = xag.get();

        stringstream ss;
        ss << "{";
        ss << "\"shadow\":" << (shadow.is_shadow() ? "true" : "false") << ",";
        ss << "\"fix_connected\":" << (quote.is_connected() ? "true" : "false") << ",";
        ss << "\"xau\":{\"realized\":" << xau_pnl.realized << ",\"unreal\":" << xau_pnl.unreal << "},";
        ss << "\"xag\":{\"realized\":" << xag_pnl.realized << ",\"unreal\":" << xag_pnl.unreal << "}";
        ss << "}";

        telemetry.update(ss.str());
    }

    // Graceful shutdown
    cout << endl << "[SHUTDOWN] Saving final state..." << endl;
    
    snapshot.save("XAUUSD", xau.get_snapshot());
    snapshot.save("XAGUSD", xag.get_snapshot());
    
    FixSeqState final_state;
    final_state.quote_seq = quote.current_seq();
    final_state.trade_seq = trade.current_seq();
    seq_store.save(final_state);

    journal.close();

    reader.join();
    gold_thread.join();
    silver_thread.join();
    snapshot_saver.join();

    cout << "[SHUTDOWN] Complete." << endl;
    return 0;
}

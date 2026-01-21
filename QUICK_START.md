# QUICK START GUIDE - CHIMERA CAUSAL TOOLING

## 5-MINUTE DEPLOYMENT

### Step 1: Extract (if packaged)
```bash
cd ~/Chimera
# If you have the tarball
tar -xzf chimera_causal_tooling_complete.tar.gz
```

### Step 2: Build Causal Lab Tools (1 min)
```bash
cd causal_lab
mkdir build && cd build
cmake ..
make -j$(nproc)
```

**Test**:
```bash
./chimera_replay --help  # Should show usage
```

### Step 3: Install Python Dependencies (30 sec)
```bash
# Dashboard
cd ../../causal_dashboard/server
pip install -r requirements.txt

# Alpha Governor
cd ../../alpha_governor/server
pip install -r requirements.txt
```

### Step 4: Start Services (30 sec)

**Terminal 1: Dashboard**
```bash
cd causal_dashboard/server
./run.sh
# Should show: "Uvicorn running on http://0.0.0.0:8088"
```

**Terminal 2: Alpha Governor**
```bash
cd alpha_governor/server
./run.sh
# Should show: "Uvicorn running on http://0.0.0.0:8099"
```

**Terminal 3: Open Browser**
```bash
open http://localhost:8088  # macOS
# or
xdg-open http://localhost:8088  # Linux
```

## FIRST RUN: Test with Sample Data

### Create Sample Event Log (for testing)
```bash
cd causal_lab/build

# Create tiny test file
cat > test_events.cpp << 'EOF'
#include "../include/EventBus.hpp"
using namespace chimera_lab;

int main() {
    EventBus bus("test.bin");
    
    SignalVector sig{1.2, 0.8, -0.5, 0.3, 0.15, 0.22, 0.1, 0.4};
    DecisionPayload dec{true, 0.001, 50000.0, sig};
    FillPayload fill{50000.5, 0.001, 0.1, 2.3};
    
    for (int i = 0; i < 10; i++) {
        bus.logSignal(i, i*1000, i*1000, 1, 1, 1, sig);
        bus.logDecision(i, i*1000, i*1000, 1, 1, 1, dec);
        bus.logFill(i, i*1000, i*1000, 1, 1, 1, fill);
    }
    
    return 0;
}
EOF

g++ -std=c++20 -I../include test_events.cpp ../src/*.cpp -o test_gen
./test_gen
```

### Run Tools
```bash
# Replay
./chimera_replay test.bin

# Attribution
./chimera_attrib test.bin test_results.csv

# View results
cat test_results.csv
```

## INTEGRATE WITH EXISTING CHIMERA

### Option A: Wire BinaryEventLog (Recommended)

**In your main.cpp** (around your trading loop):
```cpp
#include "chimera/audit/BinaryEventLog.hpp"

int main() {
    // Your existing code...
    
    std::string date_str = getCurrentDateString(); // Your function
    BinaryEventLog event_log("events/events_" + date_str + ".bin");
    
    // In your fill handler:
    auto on_fill = [&](const Fill& fill) {
        // Your existing fill logic...
        
        // ADD THIS: Log fill for causal analysis
        event_log.logWithTimestamp(
            EventType::FILL,
            fill.timestamp_ns,
            &fill,
            sizeof(fill)
        );
    };
    
    // Rest of your code...
}
```

### Option B: Export Attribution CSV

**In your session cleanup** (end of day):
```cpp
// Your existing code likely has SignalAttributionLedger
signal_attribution_ledger.saveToDisk("research/" + date_str + ".csv");

// This feeds the dashboard and alpha governor
```

### Option C: Wire Alpha Governor Commands

**Create command consumer** (alpha_consumer.cpp):
```cpp
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include "chimera/governance/CapitalAllocator.hpp"

void watchAlphaGovernor(CapitalAllocator& allocator) {
    std::ifstream in("alpha_governor/logs/commands.out");
    in.seekg(0, std::ios::end);
    
    while (true) {
        std::string line;
        if (std::getline(in, line)) {
            if (line.find("DISABLE_ENGINE") != std::string::npos) {
                std::string engine = line.substr(15);
                allocator.setWeight(engine, 0.0);  // Disable
            }
            else if (line.find("SET_WEIGHT") != std::string::npos) {
                auto parts = split(line, ' ');
                allocator.setWeight(parts[1], std::stod(parts[2]));
            }
        } else {
            in.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}

// In main:
std::thread alpha_thread(watchAlphaGovernor, std::ref(capital_allocator));
```

## DAILY WORKFLOW

### Morning (Pre-Market)
```bash
# 1. Start monitoring stack
screen -dmS dashboard bash -c "cd causal_dashboard/server && ./run.sh"
screen -dmS governor bash -c "cd alpha_governor/server && ./run.sh"

# 2. Start Chimera
./chimera
```

### Evening (Post-Market)
```bash
# 1. Stop Chimera (Ctrl+C)

# 2. Run attribution
./causal_lab/build/chimera_attrib \
    events/$(date +%Y%m%d).bin \
    research/$(date +%Y%m%d).csv

# 3. Check results
tail -20 research/$(date +%Y%m%d).csv

# 4. Check alpha governor actions
tail -20 alpha_governor/logs/commands.out
```

### Weekend (Deep Analysis)
```bash
# Batch process entire week
./causal_lab/build/chimera_batch \
    events/ \
    research/week_$(date +%Y%V).csv

# Open in spreadsheet or Jupyter
libreoffice research/week_*.csv
# or
jupyter notebook
```

## MONITORING

### Check Dashboard
```bash
curl http://localhost:8088/health
# Should return: {"status":"ok"}

# Open in browser
open http://localhost:8088
```

### Check Alpha Governor
```bash
curl http://localhost:8099/health
# Should return: {"status":"alpha-governor-online"}

curl http://localhost:8099/state
# Shows current signal states
```

### Check Logs
```bash
# Dashboard logs
tail -f causal_dashboard/server/uvicorn.log

# Governor logs
tail -f alpha_governor/server/uvicorn.log
```

## CONFIGURATION

### Tune Alpha Governor
Edit `alpha_governor/config/governor.yaml`:
```yaml
min_edge_bps: 0.5      # Lower = more lenient
cost_bps: 0.8          # Your actual execution cost
window: 200            # Trades to evaluate (lower = more reactive)
min_confidence: 0.65   # Statistical threshold (lower = more aggressive)
```

## TROUBLESHOOTING

### Dashboard: "No data"
```bash
# Generate sample data
cd causal_lab/build
./chimera_attrib test.bin ../../research.csv

# Refresh browser
```

### Governor: No commands issued
```bash
# Check if CSV has enough rows
wc -l research.csv
# Need 200+ for confidence

# Lower threshold temporarily
echo "window: 50" >> alpha_governor/config/governor.yaml

# Restart
pkill -f "python.*main.py"
cd alpha_governor/server && ./run.sh
```

### Build: C++ errors
```bash
# Check compiler version
g++ --version  # Need 10+
clang++ --version  # Need 11+

# Use specific compiler
cd causal_lab/build
CXX=g++-11 cmake ..
make
```

### Port conflicts
```bash
# Dashboard on 8088
lsof -ti:8088 | xargs kill -9

# Governor on 8099
lsof -ti:8099 | xargs kill -9
```

## VERIFICATION CHECKLIST

- [ ] causal_lab builds without errors
- [ ] `./chimera_replay test.bin` runs
- [ ] Dashboard loads at http://localhost:8088
- [ ] Alpha Governor responds at http://localhost:8099
- [ ] Can generate attribution CSV
- [ ] CSV appears in dashboard
- [ ] Governor issues commands to `logs/commands.out`

## NEXT STEPS

Once verified:
1. Connect your existing BinaryEventLog to causal_lab format
2. Export SignalAttributionLedger to CSV
3. Wire alpha governor commands into CapitalAllocator
4. Run for 1 week
5. Analyze results
6. Tune parameters

## SUPPORT

These tools are **additive** - they don't replace anything in your existing Chimera system. They consume data your system already produces (logs, CSVs) and provide additional capabilities.

If something breaks, you can simply stop the background services and continue trading normally.

## RESOURCES

- Full documentation: `README_CAUSAL_TOOLING.md`
- Architecture comparison: `INTEGRATION_COMPARISON.md`
- Original reports: `CAUSAL_FRAMEWORK_REPORT.txt`

## CONTACT

For issues with:
- **Core Chimera causal system**: Use your existing code (it's excellent)
- **New standalone tools**: Check README or modify Python/C++ as needed

import random
import time
from state import Trade, Decision, Position

ENGINES = ["ETH_FADE", "BTC_CASCADE", "MAKER_REBATE"]

class ChimeraClient:
    def __init__(self, state):
        self.state = state

    def tick(self):
        for sym in ["BTCUSDT", "ETHUSDT"]:
            price = random.uniform(100, 200)
            self.state.market[sym] = {
                "price": round(price, 2),
                "spread": round(random.uniform(0.01, 0.1), 4),
                "ofi": round(random.uniform(-20, 20), 2),
                "impulse": random.choice([True, False])
            }

            if sym not in self.state.positions:
                self.state.positions[sym] = Position(symbol=sym)

        if random.random() < 0.4:
            engine = random.choice(ENGINES)
            pnl = round(random.uniform(-2, 3), 2)

            t = Trade(
                engine=engine,
                symbol=random.choice(["BTCUSDT", "ETHUSDT"]),
                side=random.choice(["BUY", "SELL"]),
                qty=round(random.uniform(0.01, 0.1), 4),
                price=round(random.uniform(100, 200), 2),
                ts=time.time(),
                pnl=pnl
            )
            self.state.trades.append(t)
            self.state.update_fitness(t)

        if random.random() < 0.5:
            d = Decision(
                engine=random.choice(ENGINES),
                symbol=random.choice(["BTCUSDT", "ETHUSDT"]),
                approved=random.choice([True, False]),
                reason=random.choice(
                    ["EDGE", "RISK", "CORR", "FITNESS", "OK"]
                ),
                edge=round(random.uniform(0, 20), 2),
                spread=round(random.uniform(0, 0.1), 4),
                ts=time.time()
            )
            self.state.decisions.append(d)

        self.state.last_update = time.time()

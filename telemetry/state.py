from dataclasses import dataclass, field
from typing import Dict, List
import time
import math

@dataclass
class Position:
    symbol: str
    net_qty: float = 0.0
    avg_price: float = 0.0
    unrealized: float = 0.0
    realized: float = 0.0

@dataclass
class Trade:
    engine: str
    symbol: str
    side: str
    qty: float
    price: float
    ts: float
    pnl: float = 0.0

@dataclass
class Decision:
    engine: str
    symbol: str
    approved: bool
    reason: str
    edge: float
    spread: float
    ts: float

@dataclass
class EngineFitness:
    engine: str
    total_pnl: float = 0.0
    wins: int = 0
    losses: int = 0
    max_drawdown: float = 0.0
    equity: float = 0.0
    win_rate: float = 0.0
    healthy: bool = True

@dataclass
class TelemetryState:
    positions: Dict[str, Position] = field(default_factory=dict)
    trades: List[Trade] = field(default_factory=list)
    decisions: List[Decision] = field(default_factory=list)
    market: Dict[str, dict] = field(default_factory=dict)
    fitness: Dict[str, EngineFitness] = field(default_factory=dict)
    last_update: float = field(default_factory=lambda: time.time())

    def snapshot(self):
        return {
            "positions": {
                k: vars(v) for k, v in self.positions.items()
            },
            "trades": [vars(t) for t in self.trades[-50:]],
            "decisions": [vars(d) for d in self.decisions[-50:]],
            "market": self.market,
            "fitness": {
                k: vars(v) for k, v in self.fitness.items()
            },
            "ts": self.last_update
        }

    def update_fitness(self, trade: Trade):
        f = self.fitness.get(trade.engine)
        if not f:
            f = EngineFitness(engine=trade.engine)
            self.fitness[trade.engine] = f

        f.total_pnl += trade.pnl
        f.equity += trade.pnl

        if trade.pnl >= 0:
            f.wins += 1
        else:
            f.losses += 1

        if f.equity < f.max_drawdown:
            f.max_drawdown = f.equity

        total = f.wins + f.losses
        if total > 0:
            f.win_rate = f.wins / total

        f.healthy = not (
            f.max_drawdown <= -0.02 or
            (total >= 10 and f.win_rate < 0.45)
        )

    def correlation_matrix(self):
        engines = list(self.fitness.keys())
        matrix = {}

        series = {}
        for e in engines:
            series[e] = [t.pnl for t in self.trades if t.engine == e][-50:]

        def corr(a, b):
            if len(a) != len(b) or len(a) < 2:
                return 0.0
            ma = sum(a) / len(a)
            mb = sum(b) / len(b)
            num = 0.0
            da = 0.0
            db = 0.0
            for i in range(len(a)):
                xa = a[i] - ma
                xb = b[i] - mb
                num += xa * xb
                da += xa * xa
                db += xb * xb
            den = math.sqrt(da * db)
            return 0.0 if den == 0 else num / den

        for a in engines:
            matrix[a] = {}
            for b in engines:
                matrix[a][b] = round(corr(
                    series.get(a, []),
                    series.get(b, [])
                ), 2)

        return matrix

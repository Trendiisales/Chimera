#!/usr/bin/env python3
"""
Generate sample 5-minute bar data for backtest testing.

This creates synthetic data with realistic characteristics:
- Opening range patterns
- VWAP pullbacks
- Trend days and chop days
- Realistic spread and volatility

Usage:
  python3 generate_sample_data.py

Output:
  data/NAS100_5min.csv
  data/US30_5min.csv
"""

import os
import random
import math
from datetime import datetime, timedelta

def generate_bars(symbol, days=60, base_price=18000, volatility=0.002):
    """Generate realistic 5-minute bars."""
    
    bars = []
    price = base_price
    
    start_date = datetime(2024, 11, 1)
    
    for day in range(days):
        current_date = start_date + timedelta(days=day)
        
        # Skip weekends
        if current_date.weekday() >= 5:
            continue
        
        # Daily characteristics
        is_trend_day = random.random() > 0.6
        trend_direction = random.choice([1, -1]) if is_trend_day else 0
        day_range = volatility * (1.5 if is_trend_day else 1.0)
        
        # Opening gap
        gap = random.gauss(0, volatility * 0.3) * price
        price += gap
        
        # NY session: 09:30 to 16:00 (78 bars)
        session_start = datetime(current_date.year, current_date.month, current_date.day, 9, 30)
        
        # Opening range (first 6 bars = 30 minutes)
        or_high = price
        or_low = price
        
        for bar_num in range(78):
            bar_time = session_start + timedelta(minutes=bar_num * 5)
            
            # Time-based volatility (higher at open and close)
            time_factor = 1.0
            if bar_num < 6:  # First 30 min
                time_factor = 1.5
            elif bar_num > 70:  # Last hour
                time_factor = 1.3
            elif bar_num > 12 and bar_num < 60:  # Midday
                time_factor = 0.7
            
            # Generate bar
            bar_vol = day_range * time_factor * random.gauss(1.0, 0.2)
            
            # Trend bias
            if is_trend_day:
                drift = trend_direction * volatility * 0.1 * price
            else:
                # Mean reversion on chop days
                drift = -0.1 * (price - base_price)
            
            open_price = price
            
            # Intrabar movement
            high = open_price + abs(random.gauss(0, bar_vol)) * price
            low = open_price - abs(random.gauss(0, bar_vol)) * price
            
            # Close with drift
            close_price = open_price + random.gauss(drift, bar_vol * 0.5 * price)
            close_price = max(low, min(high, close_price))
            
            # Track opening range
            if bar_num < 6:
                or_high = max(or_high, high)
                or_low = min(or_low, low)
            
            # VWAP pullback simulation (after OR)
            if bar_num > 6 and bar_num < 40 and random.random() > 0.85:
                # Pull back toward VWAP (approximated as session midpoint)
                vwap_approx = (or_high + or_low) / 2
                close_price = close_price * 0.7 + vwap_approx * 0.3
            
            # Volume (higher at open/close)
            volume = random.randint(5000, 25000) * time_factor
            
            bars.append({
                'date': current_date.strftime('%Y-%m-%d'),
                'time': bar_time.strftime('%H:%M:%S'),
                'open': round(open_price, 2),
                'high': round(max(high, open_price, close_price), 2),
                'low': round(min(low, open_price, close_price), 2),
                'close': round(close_price, 2),
                'volume': int(volume)
            })
            
            price = close_price
        
        # End of day reset toward base
        price = price * 0.99 + base_price * 0.01
    
    return bars

def write_csv(bars, filename):
    """Write bars to CSV."""
    os.makedirs(os.path.dirname(filename), exist_ok=True)
    
    with open(filename, 'w') as f:
        f.write('date,time,open,high,low,close,volume\n')
        for bar in bars:
            f.write(f"{bar['date']},{bar['time']},{bar['open']},{bar['high']},{bar['low']},{bar['close']},{bar['volume']}\n")
    
    print(f"Generated {len(bars)} bars -> {filename}")

def main():
    print("Generating sample backtest data...")
    print()
    
    # NAS100 - base ~18000, higher volatility
    nas100_bars = generate_bars('NAS100', days=90, base_price=18000, volatility=0.003)
    write_csv(nas100_bars, 'data/NAS100_5min.csv')
    
    # US30 - base ~37000, lower volatility
    us30_bars = generate_bars('US30', days=90, base_price=37000, volatility=0.002)
    write_csv(us30_bars, 'data/US30_5min.csv')
    
    print()
    print("Sample data generated!")
    print()
    print("Next steps:")
    print("  1. cd build && cmake .. && make backtest_v4.10.2")
    print("  2. ./backtest_v4.10.2 --test-a")
    print()
    print("NOTE: This is SYNTHETIC data for testing the backtest harness.")
    print("      Replace with REAL data before making trading decisions.")

if __name__ == '__main__':
    main()

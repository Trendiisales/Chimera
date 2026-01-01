#!/usr/bin/env python3
"""
build_labels.py - Create REAL expectancy labels from raw trades
Chimera ML v4.6.0

This creates labels for training that represent:
  E[PnL | trade fired, given engine constraints]

NOT market direction. NOT raw returns.
REALIZED PnL after spreads, slippage, latency.
"""

import pandas as pd
import numpy as np
import struct
import sys
import os
from datetime import datetime

# =============================================================================
# ML Attribution Record Structure (matches C++ exactly)
# =============================================================================
ATTRIBUTION_RECORD_SIZE = 64
ATTRIBUTION_FORMAT = '<Q I B 3s B B B B f f f f f f f f f f f f I'

def load_attribution_binary(path: str) -> pd.DataFrame:
    """Load binary ML attribution log"""
    
    if not os.path.exists(path):
        raise FileNotFoundError(f"Attribution file not found: {path}")
    
    records = []
    
    with open(path, 'rb') as f:
        while True:
            data = f.read(ATTRIBUTION_RECORD_SIZE)
            if len(data) < ATTRIBUTION_RECORD_SIZE:
                break
            
            try:
                fields = struct.unpack(ATTRIBUTION_FORMAT, data)
                
                records.append({
                    'timestamp_ns': fields[0],
                    'symbol_id': fields[1],
                    'side': np.int8(fields[2]),
                    'regime': fields[4],
                    'session': fields[5],
                    'ml_decision': fields[6],
                    'q10': fields[8],
                    'q25': fields[9],
                    'q50': fields[10],
                    'q75': fields[11],
                    'q90': fields[12],
                    'latency_us': fields[13],
                    'size_scale': fields[14],
                    'entry_price': fields[15],
                    'exit_price': fields[16],
                    'realized_pnl': fields[17],
                    'mfe': fields[18],
                    'mae': fields[19],
                    'hold_time_ms': fields[20]
                })
                
            except struct.error as e:
                print(f"Error unpacking record: {e}")
                continue
    
    return pd.DataFrame(records)

def load_feature_binary(path: str) -> pd.DataFrame:
    """Load binary ML feature log"""
    
    FEATURE_RECORD_SIZE = 64
    FEATURE_FORMAT = '<Q I 4s B B B B f f f f f f f f f H b B f f f I'
    
    if not os.path.exists(path):
        raise FileNotFoundError(f"Feature file not found: {path}")
    
    records = []
    
    with open(path, 'rb') as f:
        while True:
            data = f.read(FEATURE_RECORD_SIZE)
            if len(data) < FEATURE_RECORD_SIZE:
                break
            
            try:
                fields = struct.unpack(FEATURE_FORMAT, data)
                
                records.append({
                    'timestamp_ns': fields[0],
                    'symbol_id': fields[1],
                    'state': fields[3],
                    'intent': fields[4],
                    'regime': fields[5],
                    'f_atr_mult': fields[7],
                    'f_vol_z': fields[8],
                    'f_range_z': fields[9],
                    'f_dist_vwap': fields[10],
                    'f_ofi': fields[11],
                    'f_vpin': fields[12],
                    'f_conviction': fields[13],
                    'f_spread_bps': fields[14],
                    'f_trend_str': fields[15],
                    'f_min_open': fields[16],
                    'side': fields[17],
                    'strategy_id': fields[18],
                    'realized_R': fields[19],
                    'mfe_R': fields[20],
                    'mae_R': fields[21],
                    'hold_time_ms': fields[22]
                })
                
            except struct.error as e:
                continue
    
    return pd.DataFrame(records)

def build_training_labels(
    features_path: str = "ml_features.bin",
    attribution_path: str = "ml_attribution.bin",
    output_path: str = "training_data.parquet",
    spread_cost_bps: float = 0.5,
    slippage_bps: float = 0.3,
    latency_cost_factor: float = 0.001
) -> pd.DataFrame:
    """
    Build training labels from raw feature and attribution logs.
    
    Labels represent REALIZED expectancy:
      label = realized_pnl - spread_cost - slippage - latency_cost
    
    Only trades that actually fired are included (trade_fired == 1).
    """
    
    print(f"Loading features from {features_path}...")
    df_features = load_feature_binary(features_path)
    print(f"  Loaded {len(df_features)} feature records")
    
    # Filter to trades only (has outcome)
    df_trades = df_features[
        (df_features['realized_R'] != 0) | 
        (df_features['mfe_R'] != 0) | 
        (df_features['mae_R'] != 0) |
        (df_features['hold_time_ms'] > 0)
    ].copy()
    
    print(f"  Found {len(df_trades)} trade records")
    
    if len(df_trades) == 0:
        print("No trades found. Cannot build training data.")
        return pd.DataFrame()
    
    # Calculate realized PnL with costs
    # Note: realized_R is already in R-multiples, convert to bps estimate
    df_trades['realized_pnl_bps'] = df_trades['realized_R'] * 10.0  # Rough conversion
    
    # Deduct costs
    df_trades['spread_cost'] = spread_cost_bps
    df_trades['slippage_cost'] = slippage_bps
    df_trades['latency_cost'] = df_trades['f_spread_bps'] * latency_cost_factor  # Proxy
    
    df_trades['label_pnl'] = (
        df_trades['realized_pnl_bps'] - 
        df_trades['spread_cost'] - 
        df_trades['slippage_cost'] - 
        df_trades['latency_cost']
    )
    
    # Add regime labels
    REGIME_MAP = {0: 'TREND', 1: 'MEANREV', 2: 'BURST', 3: 'DEAD'}
    df_trades['regime_name'] = df_trades['regime'].map(REGIME_MAP).fillna('UNKNOWN')
    
    # Mark as trade_fired
    df_trades['trade_fired'] = 1
    
    # Save
    print(f"Saving {len(df_trades)} training samples to {output_path}...")
    df_trades.to_parquet(output_path, index=False)
    
    # Stats
    print("\n" + "="*60)
    print("TRAINING DATA SUMMARY")
    print("="*60)
    print(f"Total samples:    {len(df_trades)}")
    print(f"Mean PnL (bps):   {df_trades['label_pnl'].mean():.2f}")
    print(f"Std PnL (bps):    {df_trades['label_pnl'].std():.2f}")
    print(f"Win rate:         {(df_trades['label_pnl'] > 0).mean() * 100:.1f}%")
    print("\nBy regime:")
    for regime in df_trades['regime_name'].unique():
        sub = df_trades[df_trades['regime_name'] == regime]
        print(f"  {regime}: n={len(sub)}, mean={sub['label_pnl'].mean():.2f}, "
              f"win={100*(sub['label_pnl']>0).mean():.1f}%")
    print("="*60)
    
    return df_trades

def main():
    """Main entry point"""
    
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python build_labels.py <features_file> [attribution_file] [output_file]")
        print("")
        print("Examples:")
        print("  python build_labels.py ml_features.bin")
        print("  python build_labels.py ml_features.bin ml_attribution.bin training.parquet")
        sys.exit(1)
    
    features_path = sys.argv[1]
    attribution_path = sys.argv[2] if len(sys.argv) > 2 else "ml_attribution.bin"
    output_path = sys.argv[3] if len(sys.argv) > 3 else "training_data.parquet"
    
    build_training_labels(features_path, attribution_path, output_path)

if __name__ == '__main__':
    main()

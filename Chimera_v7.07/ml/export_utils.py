#!/usr/bin/env python3
# =============================================================================
# export_utils.py - Data Export Utilities for Chimera ML
# =============================================================================
# PURPOSE: Convert binary logs to CSV, generate reports, manage datasets
# DESIGN:
#   - Feature log export (ml_features.bin → CSV)
#   - Audit log export (audit_log.bin → CSV)
#   - Dataset splitting (train/val/test)
#   - Summary statistics
# =============================================================================

import struct
import numpy as np
import pandas as pd
from pathlib import Path
from datetime import datetime
import argparse

# =============================================================================
# Binary Format Definitions
# =============================================================================

# MLFeatureRecord (64 bytes)
FEATURE_FORMAT = "<Q I 4s B B B x f f f f f f f f f H b B f f f I"
FEATURE_SIZE = struct.calcsize(FEATURE_FORMAT)
FEATURE_COLUMNS = [
    "timestamp_ns", "symbol_id", "_pad1",
    "state", "intent", "regime",
    "atr_mult", "vol_z", "range_z", "dist_vwap", "ofi", "vpin",
    "conviction", "spread_bps", "trend_str",
    "min_open", "side", "strategy_id",
    "realized_R", "mfe_R", "mae_R", "hold_ms"
]

# FullAuditRecord (128 bytes)
AUDIT_FORMAT = "<Q Q I b B 2s d d d d B B B B B 3s f f f f f f f f B B B 5s f f f I Q"
AUDIT_SIZE = struct.calcsize(AUDIT_FORMAT)
AUDIT_COLUMNS = [
    "order_id", "timestamp_ns", "symbol_id", "side", "record_type", "_pad1",
    "price", "size", "stop", "notional",
    "market_state", "trade_intent", "regime", "conviction", "strategy_id", "_pad2",
    "ml_expected_R", "ml_prob_positive", "ml_size_mult", "ml_model_conf",
    "kelly_raw", "kelly_damped",
    "bandit_mult", "drift_rmse",
    "ml_allowed", "ml_active", "drift_degraded", "_pad3",
    "realized_R", "mfe_R", "mae_R", "hold_ms", "close_timestamp_ns"
]

STATE_NAMES = {0: "DEAD", 1: "TRENDING", 2: "RANGING", 3: "VOLATILE"}
INTENT_NAMES = {0: "NO_TRADE", 1: "MOMENTUM", 2: "MEAN_REVERSION"}
REGIME_NAMES = {0: "LOW_VOL", 1: "NORMAL_VOL", 2: "HIGH_VOL", 3: "CRISIS"}

# =============================================================================
# Feature Export
# =============================================================================

def export_features_to_csv(bin_path: str, csv_path: str) -> int:
    """Export binary feature log to CSV."""
    records = []
    
    with open(bin_path, "rb") as f:
        while True:
            buf = f.read(FEATURE_SIZE)
            if not buf or len(buf) < FEATURE_SIZE:
                break
            row = struct.unpack(FEATURE_FORMAT, buf)
            records.append(row)
    
    if not records:
        print(f"[WARN] No records in {bin_path}")
        return 0
    
    df = pd.DataFrame(records, columns=FEATURE_COLUMNS)
    
    # Drop padding columns
    df = df.drop(columns=[c for c in df.columns if c.startswith("_")])
    
    # Add human-readable columns
    df["state_name"] = df["state"].map(STATE_NAMES)
    df["intent_name"] = df["intent"].map(INTENT_NAMES)
    df["regime_name"] = df["regime"].map(REGIME_NAMES)
    df["side_name"] = df["side"].map({1: "BUY", -1: "SELL", 0: "NONE"})
    
    # Convert timestamp to datetime
    df["datetime"] = pd.to_datetime(df["timestamp_ns"], unit="ns")
    
    # Computed columns
    df["is_win"] = df["realized_R"] > 0
    df["is_closed"] = df["realized_R"].abs() > 1e-6
    
    df.to_csv(csv_path, index=False)
    print(f"[INFO] Exported {len(df)} records to {csv_path}")
    
    return len(df)

def export_audit_to_csv(bin_path: str, csv_path: str) -> int:
    """Export binary audit log to CSV."""
    records = []
    
    with open(bin_path, "rb") as f:
        while True:
            buf = f.read(AUDIT_SIZE)
            if not buf or len(buf) < AUDIT_SIZE:
                break
            row = struct.unpack(AUDIT_FORMAT, buf)
            records.append(row)
    
    if not records:
        print(f"[WARN] No records in {bin_path}")
        return 0
    
    df = pd.DataFrame(records, columns=AUDIT_COLUMNS)
    
    # Drop padding columns
    df = df.drop(columns=[c for c in df.columns if c.startswith("_")])
    
    # Add human-readable columns
    df["state_name"] = df["market_state"].map(STATE_NAMES)
    df["intent_name"] = df["trade_intent"].map(INTENT_NAMES)
    df["regime_name"] = df["regime"].map(REGIME_NAMES)
    df["record_type_name"] = df["record_type"].map({0: "ORDER", 1: "CLOSE"})
    df["side_name"] = df["side"].map({1: "BUY", -1: "SELL"})
    
    df["datetime"] = pd.to_datetime(df["timestamp_ns"], unit="ns")
    
    df.to_csv(csv_path, index=False)
    print(f"[INFO] Exported {len(df)} records to {csv_path}")
    
    return len(df)

# =============================================================================
# Dataset Management
# =============================================================================

def split_dataset(csv_path: str, output_dir: str, 
                  train_ratio: float = 0.7,
                  val_ratio: float = 0.15) -> None:
    """Split dataset into train/val/test with time ordering."""
    df = pd.read_csv(csv_path)
    
    # Filter to closed trades only
    df = df[df["is_closed"] == True].copy()
    
    # Sort by timestamp
    df = df.sort_values("timestamp_ns")
    
    n = len(df)
    train_end = int(n * train_ratio)
    val_end = int(n * (train_ratio + val_ratio))
    
    df_train = df.iloc[:train_end]
    df_val = df.iloc[train_end:val_end]
    df_test = df.iloc[val_end:]
    
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)
    
    df_train.to_csv(output_path / "train.csv", index=False)
    df_val.to_csv(output_path / "val.csv", index=False)
    df_test.to_csv(output_path / "test.csv", index=False)
    
    print(f"[INFO] Split dataset:")
    print(f"  Train: {len(df_train)} ({100*len(df_train)/n:.1f}%)")
    print(f"  Val:   {len(df_val)} ({100*len(df_val)/n:.1f}%)")
    print(f"  Test:  {len(df_test)} ({100*len(df_test)/n:.1f}%)")

# =============================================================================
# Statistics
# =============================================================================

def generate_summary(csv_path: str) -> dict:
    """Generate summary statistics from feature CSV."""
    df = pd.read_csv(csv_path)
    
    df_closed = df[df["is_closed"] == True]
    
    stats = {
        "total_records": len(df),
        "closed_trades": len(df_closed),
        "open_records": len(df) - len(df_closed),
        "win_rate": df_closed["is_win"].mean() if len(df_closed) > 0 else 0,
        "mean_R": df_closed["realized_R"].mean() if len(df_closed) > 0 else 0,
        "std_R": df_closed["realized_R"].std() if len(df_closed) > 0 else 0,
        "median_R": df_closed["realized_R"].median() if len(df_closed) > 0 else 0,
        "max_R": df_closed["realized_R"].max() if len(df_closed) > 0 else 0,
        "min_R": df_closed["realized_R"].min() if len(df_closed) > 0 else 0,
        "total_R": df_closed["realized_R"].sum() if len(df_closed) > 0 else 0,
    }
    
    # By state
    if len(df_closed) > 0:
        for state_val, state_name in STATE_NAMES.items():
            mask = df_closed["state"] == state_val
            if mask.sum() > 0:
                stats[f"winrate_{state_name}"] = df_closed.loc[mask, "is_win"].mean()
                stats[f"mean_R_{state_name}"] = df_closed.loc[mask, "realized_R"].mean()
                stats[f"count_{state_name}"] = mask.sum()
    
    # By regime
    if len(df_closed) > 0:
        for regime_val, regime_name in REGIME_NAMES.items():
            mask = df_closed["regime"] == regime_val
            if mask.sum() > 0:
                stats[f"winrate_{regime_name}"] = df_closed.loc[mask, "is_win"].mean()
                stats[f"mean_R_{regime_name}"] = df_closed.loc[mask, "realized_R"].mean()
                stats[f"count_{regime_name}"] = mask.sum()
    
    return stats

def print_summary(csv_path: str):
    """Print summary statistics."""
    stats = generate_summary(csv_path)
    
    print("\n" + "=" * 50)
    print("  DATASET SUMMARY")
    print("=" * 50)
    print(f"  Total records:  {stats['total_records']:,}")
    print(f"  Closed trades:  {stats['closed_trades']:,}")
    print(f"  Open records:   {stats['open_records']:,}")
    print()
    print(f"  Win rate:       {stats['win_rate']:.2%}")
    print(f"  Mean R:         {stats['mean_R']:.4f}")
    print(f"  Std R:          {stats['std_R']:.4f}")
    print(f"  Total R:        {stats['total_R']:.2f}")
    print()
    
    print("  By State:")
    for state_name in STATE_NAMES.values():
        key = f"count_{state_name}"
        if key in stats and stats[key] > 0:
            wr = stats.get(f"winrate_{state_name}", 0)
            mr = stats.get(f"mean_R_{state_name}", 0)
            print(f"    {state_name:10s}: n={stats[key]:5d}, WR={wr:.1%}, R={mr:.4f}")
    
    print()
    print("  By Regime:")
    for regime_name in REGIME_NAMES.values():
        key = f"count_{regime_name}"
        if key in stats and stats[key] > 0:
            wr = stats.get(f"winrate_{regime_name}", 0)
            mr = stats.get(f"mean_R_{regime_name}", 0)
            print(f"    {regime_name:12s}: n={stats[key]:5d}, WR={wr:.1%}, R={mr:.4f}")
    
    print("=" * 50)

# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description="Chimera ML Export Utilities")
    subparsers = parser.add_subparsers(dest="command", help="Command")
    
    # Export features
    p_features = subparsers.add_parser("features", help="Export feature log to CSV")
    p_features.add_argument("--input", type=str, default="ml_features.bin")
    p_features.add_argument("--output", type=str, default="ml_features.csv")
    
    # Export audit
    p_audit = subparsers.add_parser("audit", help="Export audit log to CSV")
    p_audit.add_argument("--input", type=str, default="audit_log.bin")
    p_audit.add_argument("--output", type=str, default="audit_log.csv")
    
    # Split dataset
    p_split = subparsers.add_parser("split", help="Split dataset into train/val/test")
    p_split.add_argument("--input", type=str, required=True)
    p_split.add_argument("--output", type=str, default="datasets/")
    p_split.add_argument("--train", type=float, default=0.7)
    p_split.add_argument("--val", type=float, default=0.15)
    
    # Summary
    p_summary = subparsers.add_parser("summary", help="Print dataset summary")
    p_summary.add_argument("--input", type=str, required=True)
    
    args = parser.parse_args()
    
    if args.command == "features":
        export_features_to_csv(args.input, args.output)
    elif args.command == "audit":
        export_audit_to_csv(args.input, args.output)
    elif args.command == "split":
        split_dataset(args.input, args.output, args.train, args.val)
    elif args.command == "summary":
        print_summary(args.input)
    else:
        parser.print_help()
        return 1
    
    return 0

if __name__ == "__main__":
    exit(main())

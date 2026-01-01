#!/usr/bin/env python3
# =============================================================================
# shap_analysis.py - SHAP Explainability for Chimera ML
# =============================================================================
# PURPOSE: Generate SHAP explanations for ML model decisions
# DESIGN:
#   - Overall feature importance
#   - State-conditional importance
#   - Individual decision explanations
#   - Regime-specific analysis
#
# USAGE:
#   python shap_analysis.py --model models/staging/regressor.json --data ml_features.bin
# =============================================================================

import struct
import numpy as np
import pandas as pd
from pathlib import Path
import argparse
import warnings
warnings.filterwarnings('ignore')

import xgboost as xgb
import matplotlib
matplotlib.use('Agg')  # Non-interactive backend
import matplotlib.pyplot as plt

try:
    import shap
    SHAP_AVAILABLE = True
except ImportError:
    SHAP_AVAILABLE = False
    print("[WARN] SHAP not available - install with: pip install shap")

# =============================================================================
# Data Loading (same as train_ml.py)
# =============================================================================
RECORD_FORMAT = "<Q I 4s B B B x f f f f f f f f f H b B f f f I"
RECORD_SIZE = struct.calcsize(RECORD_FORMAT)

COLUMNS = [
    "timestamp_ns", "symbol_id", "_padding1",
    "state", "intent", "regime", 
    "atr_mult", "vol_z", "range_z", "dist_vwap", "ofi", "vpin",
    "conviction", "spread_bps", "trend_str",
    "min_open", "side", "strategy_id",
    "realized_R", "mfe_R", "mae_R", "hold_ms"
]

FEATURE_COLS = [
    "state", "intent", "regime",
    "atr_mult", "vol_z", "range_z", "dist_vwap",
    "ofi", "vpin", "conviction", "spread_bps", "trend_str"
]

STATE_NAMES = ["DEAD", "TRENDING", "RANGING", "VOLATILE"]
REGIME_NAMES = ["LOW_VOL", "NORMAL_VOL", "HIGH_VOL", "CRISIS"]

def load_binary_features(path: str) -> pd.DataFrame:
    """Load binary feature file from C++ engine."""
    records = []
    with open(path, "rb") as f:
        while True:
            buf = f.read(RECORD_SIZE)
            if not buf or len(buf) < RECORD_SIZE:
                break
            row = struct.unpack(RECORD_FORMAT, buf)
            records.append(row)
    
    df = pd.DataFrame(records, columns=COLUMNS)
    df = df.drop(columns=["_padding1"])
    return df

def preprocess_data(df: pd.DataFrame) -> pd.DataFrame:
    """Filter to valid trades."""
    df_trades = df[df["realized_R"].abs() > 1e-6].copy()
    df_trades = df_trades[df_trades["realized_R"].abs() < 10.0]
    return df_trades

# =============================================================================
# SHAP Analysis
# =============================================================================

def generate_shap_analysis(model: xgb.XGBRegressor, 
                           X: pd.DataFrame,
                           output_dir: str):
    """Generate comprehensive SHAP analysis."""
    if not SHAP_AVAILABLE:
        print("[ERROR] SHAP not available")
        return
    
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)
    
    print("[INFO] Computing SHAP values...")
    explainer = shap.Explainer(model)
    shap_values = explainer(X)
    
    # 1. Summary plot (overall importance)
    print("[INFO] Generating summary plot...")
    plt.figure(figsize=(10, 8))
    shap.summary_plot(shap_values, X, show=False)
    plt.tight_layout()
    plt.savefig(output_path / "shap_summary.png", dpi=150)
    plt.close()
    
    # 2. Bar plot (mean absolute SHAP)
    print("[INFO] Generating importance bar plot...")
    plt.figure(figsize=(10, 6))
    shap.summary_plot(shap_values, X, plot_type="bar", show=False)
    plt.tight_layout()
    plt.savefig(output_path / "shap_importance.png", dpi=150)
    plt.close()
    
    # 3. State-conditional analysis
    if "state" in X.columns:
        print("[INFO] Generating state-conditional plots...")
        for state_val, state_name in enumerate(STATE_NAMES):
            mask = X["state"] == state_val
            if mask.sum() < 10:
                continue
            
            plt.figure(figsize=(10, 8))
            shap.summary_plot(shap_values[mask], X[mask], show=False)
            plt.title(f"SHAP for State: {state_name}")
            plt.tight_layout()
            plt.savefig(output_path / f"shap_state_{state_name.lower()}.png", dpi=150)
            plt.close()
    
    # 4. Regime-conditional analysis
    if "regime" in X.columns:
        print("[INFO] Generating regime-conditional plots...")
        for regime_val, regime_name in enumerate(REGIME_NAMES):
            mask = X["regime"] == regime_val
            if mask.sum() < 10:
                continue
            
            plt.figure(figsize=(10, 8))
            shap.summary_plot(shap_values[mask], X[mask], show=False)
            plt.title(f"SHAP for Regime: {regime_name}")
            plt.tight_layout()
            plt.savefig(output_path / f"shap_regime_{regime_name.lower()}.png", dpi=150)
            plt.close()
    
    # 5. Feature dependence plots
    print("[INFO] Generating dependence plots...")
    for feature in ["conviction", "vpin", "ofi", "trend_str"]:
        if feature in X.columns:
            plt.figure(figsize=(10, 6))
            shap.dependence_plot(feature, shap_values.values, X, show=False)
            plt.tight_layout()
            plt.savefig(output_path / f"shap_dep_{feature}.png", dpi=150)
            plt.close()
    
    # 6. Save SHAP values for further analysis
    shap_df = pd.DataFrame(
        shap_values.values,
        columns=[f"shap_{c}" for c in X.columns]
    )
    shap_df.to_csv(output_path / "shap_values.csv", index=False)
    
    # 7. Feature importance summary
    importance = np.abs(shap_values.values).mean(axis=0)
    importance_df = pd.DataFrame({
        "feature": X.columns,
        "mean_abs_shap": importance
    }).sort_values("mean_abs_shap", ascending=False)
    
    importance_df.to_csv(output_path / "shap_importance.csv", index=False)
    
    print(f"\n[INFO] SHAP analysis saved to {output_path}/")
    print("\nTop features by SHAP importance:")
    for _, row in importance_df.head(10).iterrows():
        print(f"  {row['feature']:15s}: {row['mean_abs_shap']:.4f}")

def analyze_misclassifications(model: xgb.XGBRegressor,
                               X: pd.DataFrame,
                               y: pd.Series,
                               output_dir: str):
    """Analyze cases where model was significantly wrong."""
    if not SHAP_AVAILABLE:
        return
    
    output_path = Path(output_dir)
    
    preds = model.predict(X)
    errors = np.abs(preds - y)
    
    # Get worst predictions
    worst_idx = np.argsort(errors)[-100:]  # Top 100 worst
    
    print("\n[INFO] Analyzing worst predictions...")
    explainer = shap.Explainer(model)
    worst_shap = explainer(X.iloc[worst_idx])
    
    plt.figure(figsize=(12, 8))
    shap.summary_plot(worst_shap, X.iloc[worst_idx], show=False)
    plt.title("SHAP for Worst Predictions (Top 100 Errors)")
    plt.tight_layout()
    plt.savefig(output_path / "shap_worst_predictions.png", dpi=150)
    plt.close()
    
    # Error analysis by state
    df_analysis = X.copy()
    df_analysis["error"] = errors
    df_analysis["pred"] = preds
    df_analysis["actual"] = y.values
    
    print("\nMean absolute error by state:")
    for state_val, state_name in enumerate(STATE_NAMES):
        mask = df_analysis["state"] == state_val
        if mask.sum() > 0:
            mae = df_analysis.loc[mask, "error"].mean()
            print(f"  {state_name}: {mae:.4f}")
    
    df_analysis.to_csv(output_path / "error_analysis.csv", index=False)

# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description="SHAP Analysis for Chimera ML")
    parser.add_argument("--model", type=str, default="models/staging/regressor.json",
                       help="Path to XGBoost model")
    parser.add_argument("--data", type=str, default="ml_features.bin",
                       help="Path to feature data")
    parser.add_argument("--output", type=str, default="shap_output",
                       help="Output directory for plots")
    parser.add_argument("--sample", type=int, default=5000,
                       help="Max samples for SHAP (memory)")
    args = parser.parse_args()
    
    if not SHAP_AVAILABLE:
        print("[ERROR] SHAP is required. Install with: pip install shap")
        return 1
    
    if not Path(args.model).exists():
        print(f"[ERROR] Model not found: {args.model}")
        return 1
    
    if not Path(args.data).exists():
        print(f"[ERROR] Data not found: {args.data}")
        return 1
    
    print("=" * 60)
    print("  CHIMERA ML SHAP ANALYSIS")
    print("=" * 60)
    
    # Load model
    model = xgb.XGBRegressor()
    model.load_model(args.model)
    print(f"[INFO] Loaded model from {args.model}")
    
    # Load data
    df_raw = load_binary_features(args.data)
    df = preprocess_data(df_raw)
    X = df[FEATURE_COLS]
    y = df["realized_R"]
    
    # Sample if too large
    if len(X) > args.sample:
        print(f"[INFO] Sampling {args.sample} from {len(X)} records")
        idx = np.random.choice(len(X), args.sample, replace=False)
        X = X.iloc[idx]
        y = y.iloc[idx]
    
    # Generate analysis
    generate_shap_analysis(model, X, args.output)
    analyze_misclassifications(model, X, y, args.output)
    
    print("\n" + "=" * 60)
    print("  SHAP ANALYSIS COMPLETE")
    print("=" * 60)
    
    return 0

if __name__ == "__main__":
    exit(main())

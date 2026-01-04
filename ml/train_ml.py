#!/usr/bin/env python3
# =============================================================================
# train_ml.py - Chimera ML Training Pipeline
# =============================================================================
# PURPOSE: Train XGBoost models from logged features, export to ONNX
# DESIGN:
#   - Load binary feature files from C++ engine
#   - Train on R-multiples (not price prediction)
#   - Walk-forward validation
#   - Export ONNX for C++ inference
#   - Generate quantile models for asymmetric sizing
#
# USAGE:
#   python train_ml.py --data ml_features.bin --output models/active/chimera_ml.onnx
#
# MODELS:
#   1. XGBRegressor: Predict expected R
#   2. XGBClassifier: Predict P(R > 0)
#   3. Quantile Regressor: Predict 25%, 50%, 75% outcomes
# =============================================================================

import struct
import numpy as np
import pandas as pd
from pathlib import Path
from datetime import datetime
from typing import Tuple, Optional, Dict, Any
import argparse
import warnings
warnings.filterwarnings('ignore')

# ML imports
import xgboost as xgb
from sklearn.model_selection import TimeSeriesSplit
from sklearn.metrics import mean_squared_error, accuracy_score, r2_score
from sklearn.preprocessing import StandardScaler

# ONNX export
try:
    import onnx
    import onnxmltools
    from onnxmltools.convert.common.data_types import FloatTensorType
    ONNX_AVAILABLE = True
except ImportError:
    ONNX_AVAILABLE = False
    print("[WARN] onnxmltools not available - ONNX export disabled")

# =============================================================================
# Binary Format Definition (matches C++ MLFeatureRecord)
# =============================================================================
# Total: 64 bytes
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
    
    print(f"[INFO] Loaded {len(df)} records from {path}")
    return df

def preprocess_data(df: pd.DataFrame) -> Tuple[pd.DataFrame, pd.DataFrame]:
    """Clean and prepare data for training."""
    # Filter to closed trades only (have outcome)
    df_trades = df[df["realized_R"].abs() > 1e-6].copy()
    
    # Remove extreme outliers (more than 10R)
    df_trades = df_trades[df_trades["realized_R"].abs() < 10.0]
    
    # Create binary target
    df_trades["is_win"] = (df_trades["realized_R"] > 0).astype(int)
    
    # Sort by timestamp for time-series validation
    df_trades = df_trades.sort_values("timestamp_ns")
    
    print(f"[INFO] {len(df_trades)} valid trades after filtering")
    print(f"[INFO] Win rate: {df_trades['is_win'].mean():.2%}")
    print(f"[INFO] Mean R: {df_trades['realized_R'].mean():.4f}")
    
    return df_trades[FEATURE_COLS], df_trades[["realized_R", "is_win", "mfe_R", "mae_R"]]

# =============================================================================
# Model Training
# =============================================================================

def train_regressor(X: pd.DataFrame, y: pd.Series, 
                   n_splits: int = 5) -> Tuple[xgb.XGBRegressor, Dict[str, float]]:
    """Train XGBoost regressor with walk-forward validation."""
    
    model = xgb.XGBRegressor(
        n_estimators=300,
        max_depth=4,
        learning_rate=0.05,
        subsample=0.8,
        colsample_bytree=0.8,
        reg_alpha=0.1,
        reg_lambda=1.0,
        random_state=42
    )
    
    tscv = TimeSeriesSplit(n_splits=n_splits)
    rmse_scores = []
    r2_scores = []
    
    print("\n[INFO] Walk-forward validation (Regressor):")
    for fold, (train_idx, test_idx) in enumerate(tscv.split(X)):
        X_train, X_test = X.iloc[train_idx], X.iloc[test_idx]
        y_train, y_test = y.iloc[train_idx], y.iloc[test_idx]
        
        model.fit(X_train, y_train)
        preds = model.predict(X_test)
        
        rmse = np.sqrt(mean_squared_error(y_test, preds))
        r2 = r2_score(y_test, preds)
        rmse_scores.append(rmse)
        r2_scores.append(r2)
        
        print(f"  Fold {fold+1}: RMSE={rmse:.4f}, R²={r2:.4f}")
    
    # Final training on all data
    model.fit(X, y)
    
    metrics = {
        "rmse_mean": np.mean(rmse_scores),
        "rmse_std": np.std(rmse_scores),
        "r2_mean": np.mean(r2_scores),
        "r2_std": np.std(r2_scores)
    }
    
    print(f"\n  Mean RMSE: {metrics['rmse_mean']:.4f} ± {metrics['rmse_std']:.4f}")
    print(f"  Mean R²: {metrics['r2_mean']:.4f} ± {metrics['r2_std']:.4f}")
    
    return model, metrics

def train_classifier(X: pd.DataFrame, y: pd.Series,
                    n_splits: int = 5) -> Tuple[xgb.XGBClassifier, Dict[str, float]]:
    """Train XGBoost classifier for P(R > 0)."""
    
    model = xgb.XGBClassifier(
        n_estimators=300,
        max_depth=4,
        learning_rate=0.05,
        subsample=0.8,
        colsample_bytree=0.8,
        scale_pos_weight=1.0,
        random_state=42
    )
    
    tscv = TimeSeriesSplit(n_splits=n_splits)
    acc_scores = []
    
    print("\n[INFO] Walk-forward validation (Classifier):")
    for fold, (train_idx, test_idx) in enumerate(tscv.split(X)):
        X_train, X_test = X.iloc[train_idx], X.iloc[test_idx]
        y_train, y_test = y.iloc[train_idx], y.iloc[test_idx]
        
        model.fit(X_train, y_train)
        preds = model.predict(X_test)
        
        acc = accuracy_score(y_test, preds)
        acc_scores.append(acc)
        
        print(f"  Fold {fold+1}: Accuracy={acc:.4f}")
    
    model.fit(X, y)
    
    metrics = {
        "accuracy_mean": np.mean(acc_scores),
        "accuracy_std": np.std(acc_scores)
    }
    
    print(f"\n  Mean Accuracy: {metrics['accuracy_mean']:.4f} ± {metrics['accuracy_std']:.4f}")
    
    return model, metrics

def train_quantile_model(X: pd.DataFrame, y: pd.Series,
                        alpha: float) -> xgb.XGBRegressor:
    """Train quantile regressor for specified alpha (e.g., 0.25, 0.50, 0.75)."""
    
    model = xgb.XGBRegressor(
        n_estimators=200,
        max_depth=3,
        learning_rate=0.05,
        subsample=0.8,
        objective="reg:quantileerror",
        quantile_alpha=alpha,
        random_state=42
    )
    
    model.fit(X, y)
    return model

# =============================================================================
# Feature Importance Analysis
# =============================================================================

def analyze_feature_importance(model: xgb.XGBRegressor, 
                              feature_names: list) -> pd.DataFrame:
    """Analyze and display feature importance."""
    importance = model.get_booster().get_score(importance_type="gain")
    
    # Map to feature names
    df_importance = pd.DataFrame([
        {"feature": k.replace("f", ""), "importance": v}
        for k, v in importance.items()
    ])
    
    # Add feature names
    df_importance["feature_name"] = df_importance["feature"].astype(int).apply(
        lambda i: feature_names[i] if i < len(feature_names) else f"unknown_{i}"
    )
    
    df_importance = df_importance.sort_values("importance", ascending=False)
    
    print("\n[INFO] Feature Importance:")
    for _, row in df_importance.iterrows():
        print(f"  {row['feature_name']:15s}: {row['importance']:.2f}")
    
    return df_importance

# =============================================================================
# ONNX Export
# =============================================================================

def export_to_onnx(model: xgb.XGBRegressor, 
                   output_path: str,
                   n_features: int) -> bool:
    """Export XGBoost model to ONNX format."""
    if not ONNX_AVAILABLE:
        print("[WARN] ONNX export not available")
        return False
    
    try:
        initial_types = [
            ("float_input", FloatTensorType([None, n_features]))
        ]
        
        onnx_model = onnxmltools.convert_xgboost(
            model,
            initial_types=initial_types
        )
        
        onnx.save_model(onnx_model, output_path)
        print(f"[INFO] Exported ONNX model to {output_path}")
        return True
        
    except Exception as e:
        print(f"[ERROR] ONNX export failed: {e}")
        return False

# =============================================================================
# Model Promotion Logic
# =============================================================================

def should_promote(new_metrics: Dict[str, float], 
                   baseline_path: str,
                   threshold: float = 0.98) -> bool:
    """Check if new model should replace baseline."""
    if not Path(baseline_path).exists():
        print("[INFO] No baseline - promoting new model")
        return True
    
    try:
        with open(baseline_path.replace(".onnx", "_metrics.txt"), "r") as f:
            baseline_rmse = float(f.readline().split("=")[1])
    except:
        print("[INFO] No baseline metrics - promoting new model")
        return True
    
    new_rmse = new_metrics.get("rmse_mean", float('inf'))
    
    if new_rmse < baseline_rmse * threshold:
        print(f"[INFO] New model is better: {new_rmse:.4f} < {baseline_rmse:.4f} * {threshold}")
        return True
    else:
        print(f"[INFO] New model not better: {new_rmse:.4f} >= {baseline_rmse:.4f} * {threshold}")
        return False

def save_metrics(metrics: Dict[str, float], path: str):
    """Save metrics for future comparison."""
    with open(path, "w") as f:
        for k, v in metrics.items():
            f.write(f"{k}={v}\n")
    print(f"[INFO] Saved metrics to {path}")

# =============================================================================
# Main Training Pipeline
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description="Chimera ML Training Pipeline")
    parser.add_argument("--data", type=str, default="ml_features.bin",
                       help="Path to binary feature file")
    parser.add_argument("--output", type=str, default="models/active/chimera_ml.onnx",
                       help="Output path for ONNX model")
    parser.add_argument("--staging", type=str, default="models/staging/",
                       help="Staging directory for new models")
    parser.add_argument("--splits", type=int, default=5,
                       help="Number of walk-forward splits")
    parser.add_argument("--promote", action="store_true",
                       help="Automatically promote if better than baseline")
    args = parser.parse_args()
    
    print("=" * 60)
    print("  CHIMERA ML TRAINING PIPELINE")
    print("=" * 60)
    print(f"  Data:   {args.data}")
    print(f"  Output: {args.output}")
    print("=" * 60)
    
    # Load data
    if not Path(args.data).exists():
        print(f"[ERROR] Data file not found: {args.data}")
        print("[INFO] Generate data by running Chimera with feature logging enabled")
        return 1
    
    df_raw = load_binary_features(args.data)
    
    if len(df_raw) < 100:
        print(f"[ERROR] Not enough data: {len(df_raw)} records (need at least 100)")
        return 1
    
    # Preprocess
    X, y_all = preprocess_data(df_raw)
    y_R = y_all["realized_R"]
    y_win = y_all["is_win"]
    
    if len(X) < 50:
        print(f"[ERROR] Not enough valid trades: {len(X)}")
        return 1
    
    # Train main regressor
    print("\n" + "=" * 40)
    print("  Training Expected R Regressor")
    print("=" * 40)
    regressor, reg_metrics = train_regressor(X, y_R, n_splits=args.splits)
    
    # Analyze features
    importance_df = analyze_feature_importance(regressor, FEATURE_COLS)
    
    # Train classifier
    print("\n" + "=" * 40)
    print("  Training Win Probability Classifier")
    print("=" * 40)
    classifier, clf_metrics = train_classifier(X, y_win, n_splits=args.splits)
    
    # Train quantile models
    print("\n" + "=" * 40)
    print("  Training Quantile Models")
    print("=" * 40)
    q25_model = train_quantile_model(X, y_R, 0.25)
    q50_model = train_quantile_model(X, y_R, 0.50)
    q75_model = train_quantile_model(X, y_R, 0.75)
    print("[INFO] Quantile models trained (Q25, Q50, Q75)")
    
    # Create output directories
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    Path(args.staging).mkdir(parents=True, exist_ok=True)
    
    # Export main model to ONNX
    staging_path = Path(args.staging) / "chimera_ml_candidate.onnx"
    if export_to_onnx(regressor, str(staging_path), len(FEATURE_COLS)):
        
        # Save metrics
        all_metrics = {**reg_metrics, **clf_metrics}
        metrics_path = str(staging_path).replace(".onnx", "_metrics.txt")
        save_metrics(all_metrics, metrics_path)
        
        # Check promotion
        if args.promote:
            if should_promote(all_metrics, args.output):
                import shutil
                
                # Archive current
                if Path(args.output).exists():
                    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
                    archive_path = Path(args.output).parent.parent / "archive" / f"chimera_ml_{ts}.onnx"
                    archive_path.parent.mkdir(parents=True, exist_ok=True)
                    shutil.move(args.output, str(archive_path))
                    print(f"[INFO] Archived old model to {archive_path}")
                
                # Promote new
                shutil.copy(str(staging_path), args.output)
                print(f"[INFO] PROMOTED new model to {args.output}")
    
    # Also save XGBoost native format for diagnostics
    regressor.save_model(str(Path(args.staging) / "regressor.json"))
    classifier.save_model(str(Path(args.staging) / "classifier.json"))
    
    print("\n" + "=" * 60)
    print("  TRAINING COMPLETE")
    print("=" * 60)
    print(f"  Expected R RMSE: {reg_metrics['rmse_mean']:.4f}")
    print(f"  Win Accuracy:    {clf_metrics['accuracy_mean']:.2%}")
    print(f"  Model saved to:  {staging_path}")
    print("=" * 60)
    
    return 0

if __name__ == "__main__":
    exit(main())

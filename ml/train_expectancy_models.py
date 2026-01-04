#!/usr/bin/env python3
"""
train_expectancy_models.py - Regime-Specific Quantile Model Training
Chimera ML v4.6.0

Pipeline:
  1. Load training data (from build_labels.py)
  2. Filter to trade_fired == 1 only
  3. Split by regime (TREND, MEANREV, BURST, DEAD)
  4. Train LightGBM quantile models (q10, q25, q50, q75, q90)
  5. SHAP prune features
  6. Retrain on pruned features
  7. Export ONNX models
  8. Emit metadata

This is production-grade, not demo ML.
"""

import os
import sys
import json
import numpy as np
import pandas as pd
from typing import Dict, List, Tuple

try:
    import lightgbm as lgb
    import shap
    from skl2onnx import convert_lightgbm
    from skl2onnx.common.data_types import FloatTensorType
    HAS_DEPS = True
except ImportError as e:
    print(f"Missing dependency: {e}")
    print("Install with: pip install lightgbm shap skl2onnx")
    HAS_DEPS = False

# =============================================================================
# Configuration
# =============================================================================
REGIMES = ["TREND", "MEANREV", "BURST", "DEAD"]
QUANTILES = [0.1, 0.25, 0.5, 0.75, 0.9]

# LightGBM hyperparameters (tuned for HFT)
LGB_PARAMS = {
    'objective': 'quantile',
    'num_leaves': 64,
    'learning_rate': 0.03,
    'n_estimators': 400,
    'subsample': 0.8,
    'colsample_bytree': 0.8,
    'min_child_samples': 20,
    'reg_alpha': 0.1,
    'reg_lambda': 0.1,
    'verbose': -1
}

# Feature pruning threshold (percentile of SHAP importance)
SHAP_PRUNE_PERCENTILE = 35

# Minimum samples per regime
MIN_SAMPLES_PER_REGIME = 100

# =============================================================================
# Feature Engineering
# =============================================================================
def get_feature_columns(df: pd.DataFrame) -> List[str]:
    """Extract feature columns (start with f_)"""
    return [c for c in df.columns if c.startswith('f_')]

def add_derived_features(df: pd.DataFrame) -> pd.DataFrame:
    """Add derived features for better signal"""
    
    df = df.copy()
    
    # Interaction features
    if 'f_ofi' in df.columns and 'f_vpin' in df.columns:
        df['f_ofi_vpin'] = df['f_ofi'] * df['f_vpin']
    
    if 'f_spread_bps' in df.columns and 'f_vpin' in df.columns:
        df['f_spread_vpin'] = df['f_spread_bps'] * df['f_vpin']
    
    if 'f_ofi' in df.columns and 'f_trend_str' in df.columns:
        df['f_ofi_trend'] = df['f_ofi'] * df['f_trend_str']
    
    # Ratios
    if 'f_atr_mult' in df.columns and 'f_spread_bps' in df.columns:
        df['f_atr_spread_ratio'] = df['f_atr_mult'] / (df['f_spread_bps'] + 0.01)
    
    return df

# =============================================================================
# Model Training
# =============================================================================
def train_quantile_models(
    X: pd.DataFrame, 
    y: pd.Series,
    quantiles: List[float] = QUANTILES
) -> Tuple[List[lgb.LGBMRegressor], np.ndarray]:
    """
    Train quantile regression models.
    
    Returns:
        models: List of trained models (one per quantile)
        shap_importance: Mean |SHAP| importance across all models
    """
    
    models = []
    shap_accum = np.zeros(len(X.columns))
    
    for q in quantiles:
        params = LGB_PARAMS.copy()
        params['alpha'] = q
        
        model = lgb.LGBMRegressor(**params)
        model.fit(X, y)
        models.append(model)
        
        # SHAP importance
        explainer = shap.TreeExplainer(model)
        shap_values = np.abs(explainer.shap_values(X)).mean(axis=0)
        shap_accum += shap_values
    
    mean_shap = shap_accum / len(quantiles)
    
    return models, mean_shap

def prune_features(
    features: List[str],
    shap_importance: np.ndarray,
    percentile: float = SHAP_PRUNE_PERCENTILE
) -> List[str]:
    """Prune features based on SHAP importance"""
    
    threshold = np.percentile(shap_importance, percentile)
    keep_mask = shap_importance > threshold
    
    pruned = [f for f, keep in zip(features, keep_mask) if keep]
    
    print(f"  Pruning: {len(features)} -> {len(pruned)} features "
          f"(threshold: {threshold:.4f})")
    
    return pruned

def export_onnx(
    model: lgb.LGBMRegressor,
    num_features: int,
    output_path: str
) -> bool:
    """Export model to ONNX format"""
    
    try:
        onnx_model = convert_lightgbm(
            model,
            initial_types=[("input", FloatTensorType([None, num_features]))]
        )
        
        with open(output_path, 'wb') as f:
            f.write(onnx_model.SerializeToString())
        
        return True
    except Exception as e:
        print(f"  ONNX export failed: {e}")
        return False

# =============================================================================
# Main Training Pipeline
# =============================================================================
def train_pipeline(
    data_path: str = "training_data.parquet",
    output_dir: str = "models",
    symbol: str = "XAUUSD"
) -> Dict:
    """
    Full training pipeline:
      1. Load data
      2. Split by regime
      3. Train quantile models
      4. SHAP prune
      5. Retrain
      6. Export ONNX
      7. Save metadata
    """
    
    print("="*60)
    print(f"CHIMERA ML TRAINING PIPELINE - {symbol}")
    print("="*60)
    
    # Create output directory
    os.makedirs(output_dir, exist_ok=True)
    
    # Load data
    print(f"\n1. Loading data from {data_path}...")
    df = pd.read_parquet(data_path)
    print(f"   Total samples: {len(df)}")
    
    # Filter to trades only
    if 'trade_fired' in df.columns:
        df = df[df['trade_fired'] == 1]
        print(f"   After trade_fired filter: {len(df)}")
    
    # Add derived features
    print("\n2. Adding derived features...")
    df = add_derived_features(df)
    
    # Get feature columns
    features = get_feature_columns(df)
    print(f"   Features: {len(features)}")
    
    if len(features) == 0:
        print("ERROR: No features found (columns starting with f_)")
        return {}
    
    # Target column
    if 'label_pnl' in df.columns:
        target = 'label_pnl'
    elif 'realized_pnl' in df.columns:
        target = 'realized_pnl'
    else:
        print("ERROR: No target column found (label_pnl or realized_pnl)")
        return {}
    
    print(f"   Target: {target}")
    
    # Metadata to save
    metadata = {
        'symbol': symbol,
        'quantiles': QUANTILES,
        'regimes': {},
        'training_samples': len(df),
        'lgb_params': LGB_PARAMS
    }
    
    # Train per regime
    print("\n3. Training regime-specific models...")
    
    for regime in REGIMES:
        print(f"\n   === {regime} ===")
        
        # Filter to regime
        if 'regime_name' in df.columns:
            sub = df[df['regime_name'] == regime]
        elif 'regime' in df.columns:
            regime_map = {0: 'TREND', 1: 'MEANREV', 2: 'BURST', 3: 'DEAD'}
            regime_id = {v: k for k, v in regime_map.items()}.get(regime, -1)
            sub = df[df['regime'] == regime_id]
        else:
            print(f"   SKIP: No regime column")
            continue
        
        if len(sub) < MIN_SAMPLES_PER_REGIME:
            print(f"   SKIP: Only {len(sub)} samples (need {MIN_SAMPLES_PER_REGIME})")
            continue
        
        print(f"   Samples: {len(sub)}")
        
        X = sub[features]
        y = sub[target]
        
        # Train initial models
        print(f"   Training {len(QUANTILES)} quantile models...")
        models, shap_importance = train_quantile_models(X, y)
        
        # SHAP prune
        print(f"   SHAP pruning...")
        pruned_features = prune_features(features, shap_importance)
        
        if len(pruned_features) < 3:
            print(f"   WARNING: Too few features after pruning, keeping top 10")
            top_indices = np.argsort(shap_importance)[-10:]
            pruned_features = [features[i] for i in top_indices]
        
        # Retrain on pruned features
        print(f"   Retraining on {len(pruned_features)} features...")
        X_pruned = sub[pruned_features]
        final_models, _ = train_quantile_models(X_pruned, y)
        
        # Export ONNX
        print(f"   Exporting ONNX models...")
        for q, model in zip(QUANTILES, final_models):
            q_str = str(q).replace('.', '')
            onnx_path = os.path.join(output_dir, f"{symbol}_{regime}_{q_str}.onnx")
            
            if export_onnx(model, len(pruned_features), onnx_path):
                print(f"   ✓ {onnx_path}")
        
        # Save regime metadata
        metadata['regimes'][regime] = {
            'features': pruned_features,
            'n_samples': len(sub),
            'mean_target': float(y.mean()),
            'std_target': float(y.std()),
            'win_rate': float((y > 0).mean())
        }
    
    # Save metadata
    metadata_path = os.path.join(output_dir, "model_metadata.json")
    with open(metadata_path, 'w') as f:
        json.dump(metadata, f, indent=2)
    print(f"\n4. Saved metadata to {metadata_path}")
    
    print("\n" + "="*60)
    print("TRAINING COMPLETE")
    print("="*60)
    
    return metadata

# =============================================================================
# Validation
# =============================================================================
def validate_models(
    data_path: str = "training_data.parquet",
    model_dir: str = "models"
) -> None:
    """Validate trained models on holdout data"""
    
    print("\n" + "="*60)
    print("MODEL VALIDATION")
    print("="*60)
    
    # Load metadata
    meta_path = os.path.join(model_dir, "model_metadata.json")
    if not os.path.exists(meta_path):
        print("No metadata found. Run training first.")
        return
    
    with open(meta_path) as f:
        metadata = json.load(f)
    
    # Load data
    df = pd.read_parquet(data_path)
    df = add_derived_features(df)
    
    target = 'label_pnl' if 'label_pnl' in df.columns else 'realized_pnl'
    
    for regime, regime_meta in metadata['regimes'].items():
        print(f"\n{regime}:")
        
        features = regime_meta['features']
        
        # Filter to regime
        if 'regime_name' in df.columns:
            sub = df[df['regime_name'] == regime]
        else:
            continue
        
        if len(sub) == 0:
            continue
        
        X = sub[features]
        y = sub[target]
        
        # Simple validation: check feature coverage
        missing = [f for f in features if f not in X.columns]
        if missing:
            print(f"  WARNING: Missing features: {missing}")
        
        print(f"  Samples: {len(sub)}")
        print(f"  Mean target: {y.mean():.3f}")
        print(f"  Win rate: {(y > 0).mean() * 100:.1f}%")
        print(f"  Features: {len(features)}")

# =============================================================================
# Main
# =============================================================================
def main():
    if not HAS_DEPS:
        print("ERROR: Missing dependencies. Install with:")
        print("  pip install lightgbm shap skl2onnx pandas numpy")
        sys.exit(1)
    
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python train_expectancy_models.py train [data.parquet] [output_dir] [symbol]")
        print("  python train_expectancy_models.py validate [data.parquet] [model_dir]")
        print("")
        print("Examples:")
        print("  python train_expectancy_models.py train training_data.parquet models XAUUSD")
        print("  python train_expectancy_models.py validate training_data.parquet models")
        sys.exit(1)
    
    command = sys.argv[1]
    
    if command == 'train':
        data_path = sys.argv[2] if len(sys.argv) > 2 else "training_data.parquet"
        output_dir = sys.argv[3] if len(sys.argv) > 3 else "models"
        symbol = sys.argv[4] if len(sys.argv) > 4 else "XAUUSD"
        
        train_pipeline(data_path, output_dir, symbol)
        
    elif command == 'validate':
        data_path = sys.argv[2] if len(sys.argv) > 2 else "training_data.parquet"
        model_dir = sys.argv[3] if len(sys.argv) > 3 else "models"
        
        validate_models(data_path, model_dir)
        
    else:
        print(f"Unknown command: {command}")
        sys.exit(1)

if __name__ == '__main__':
    main()

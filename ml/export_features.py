#!/usr/bin/env python3
"""
ML Feature Export Utility - Chimera v4.6.0
Converts binary ml_features.bin to CSV for analysis
"""

import struct
import sys
import os
from datetime import datetime

# MLFeatureRecord structure (64 bytes, matches C++ exactly):
# uint64_t timestamp_ns           (8 bytes)
# uint32_t symbol_id              (4 bytes)
# uint8_t  padding1[4]            (4 bytes)
# uint8_t  state                  (1 byte)
# uint8_t  intent                 (1 byte)
# uint8_t  regime                 (1 byte)
# uint8_t  padding2               (1 byte)
# float    atr_multiple           (4 bytes)
# float    volume_z               (4 bytes)
# float    range_z                (4 bytes)
# float    distance_vwap          (4 bytes)
# float    ofi                    (4 bytes)
# float    vpin                   (4 bytes)
# float    conviction_score       (4 bytes)
# float    spread_bps             (4 bytes)
# float    trend_strength         (4 bytes)
# uint16_t minutes_from_open      (2 bytes)
# int8_t   side                   (1 byte)
# uint8_t  strategy_id            (1 byte)
# float    realized_R             (4 bytes)
# float    mfe_R                  (4 bytes)
# float    mae_R                  (4 bytes)
# uint32_t hold_time_ms           (4 bytes)
# Total: 64 bytes

RECORD_SIZE = 64
RECORD_FORMAT = '<Q I 4s B B B B f f f f f f f f f H b B f f f I'

REGIME_NAMES = ['LOW_VOL', 'NORMAL_VOL', 'HIGH_VOL', 'CRISIS']
STATE_NAMES = ['DEAD', 'TRENDING', 'RANGING', 'VOLATILE']
INTENT_NAMES = ['NO_TRADE', 'MOMENTUM', 'MEAN_REVERSION']

def export_to_csv(binary_path: str, csv_path: str):
    """Convert binary ML features to CSV"""
    
    if not os.path.exists(binary_path):
        print(f"Error: {binary_path} not found")
        return False
    
    file_size = os.path.getsize(binary_path)
    num_records = file_size // RECORD_SIZE
    
    print(f"File size: {file_size} bytes")
    print(f"Expected records: {num_records}")
    
    with open(binary_path, 'rb') as fin, open(csv_path, 'w') as fout:
        # Header
        fout.write("timestamp,timestamp_human,symbol_id,state,intent,regime,"
                   "atr_mult,vol_z,range_z,dist_vwap,ofi,vpin,"
                   "conviction,spread_bps,trend_str,min_open,"
                   "side,strategy_id,realized_R,mfe_R,mae_R,hold_ms,is_trade\n")
        
        count = 0
        while True:
            data = fin.read(RECORD_SIZE)
            if len(data) < RECORD_SIZE:
                break
            
            try:
                fields = struct.unpack(RECORD_FORMAT, data)
                
                ts_ns = fields[0]
                symbol_id = fields[1]
                # fields[2] is padding
                state = fields[3]
                intent = fields[4]
                regime = fields[5]
                # fields[6] is padding
                atr_mult = fields[7]
                vol_z = fields[8]
                range_z = fields[9]
                dist_vwap = fields[10]
                ofi = fields[11]
                vpin = fields[12]
                conviction = fields[13]
                spread_bps = fields[14]
                trend_str = fields[15]
                min_open = fields[16]
                side = fields[17]
                strategy_id = fields[18]
                realized_R = fields[19]
                mfe_R = fields[20]
                mae_R = fields[21]
                hold_ms = fields[22]
                
                # Convert timestamp to human readable
                ts_sec = ts_ns / 1e9
                ts_human = datetime.fromtimestamp(ts_sec).strftime('%Y-%m-%d %H:%M:%S.%f')
                
                # Determine if this is a trade record (has outcome)
                is_trade = 1 if (realized_R != 0 or mfe_R != 0 or mae_R != 0 or hold_ms > 0) else 0
                
                # Get enum names
                state_name = STATE_NAMES[state] if state < len(STATE_NAMES) else f"UNKNOWN_{state}"
                intent_name = INTENT_NAMES[intent] if intent < len(INTENT_NAMES) else f"UNKNOWN_{intent}"
                regime_name = REGIME_NAMES[regime] if regime < len(REGIME_NAMES) else f"UNKNOWN_{regime}"
                
                fout.write(f"{ts_ns},{ts_human},{symbol_id},{state_name},{intent_name},{regime_name},"
                          f"{atr_mult:.6f},{vol_z:.6f},{range_z:.6f},{dist_vwap:.6f},"
                          f"{ofi:.6f},{vpin:.6f},{conviction:.6f},{spread_bps:.6f},{trend_str:.6f},"
                          f"{min_open},{side},{strategy_id},"
                          f"{realized_R:.6f},{mfe_R:.6f},{mae_R:.6f},{hold_ms},{is_trade}\n")
                
                count += 1
                
            except struct.error as e:
                print(f"Error unpacking record {count}: {e}")
                continue
    
    print(f"Exported {count} records to {csv_path}")
    return True

def show_summary(binary_path: str):
    """Show summary statistics from binary file"""
    
    if not os.path.exists(binary_path):
        print(f"Error: {binary_path} not found")
        return
    
    trades = []
    ticks = 0
    
    with open(binary_path, 'rb') as f:
        while True:
            data = f.read(RECORD_SIZE)
            if len(data) < RECORD_SIZE:
                break
            
            fields = struct.unpack(RECORD_FORMAT, data)
            realized_R = fields[19]
            mfe_R = fields[20]
            mae_R = fields[21]
            hold_ms = fields[22]
            
            if realized_R != 0 or mfe_R != 0 or mae_R != 0 or hold_ms > 0:
                trades.append({
                    'realized_R': realized_R,
                    'mfe_R': mfe_R,
                    'mae_R': mae_R,
                    'hold_ms': hold_ms
                })
            else:
                ticks += 1
    
    print(f"\n{'='*60}")
    print(f"ML FEATURE LOG SUMMARY")
    print(f"{'='*60}")
    print(f"Total records:    {ticks + len(trades)}")
    print(f"Tick snapshots:   {ticks}")
    print(f"Trade records:    {len(trades)}")
    
    if trades:
        wins = [t for t in trades if t['realized_R'] > 0]
        losses = [t for t in trades if t['realized_R'] < 0]
        
        print(f"\nTRADE STATISTICS:")
        print(f"  Wins:           {len(wins)}")
        print(f"  Losses:         {len(losses)}")
        if len(wins) + len(losses) > 0:
            print(f"  Win Rate:       {100.0 * len(wins) / (len(wins) + len(losses)):.1f}%")
        
        if trades:
            avg_R = sum(t['realized_R'] for t in trades) / len(trades)
            avg_mfe = sum(t['mfe_R'] for t in trades) / len(trades)
            avg_mae = sum(t['mae_R'] for t in trades) / len(trades)
            avg_hold = sum(t['hold_ms'] for t in trades) / len(trades)
            
            print(f"  Avg R:          {avg_R:.3f}")
            print(f"  Avg MFE:        {avg_mfe:.3f}")
            print(f"  Avg MAE:        {avg_mae:.3f}")
            print(f"  Avg Hold (ms):  {avg_hold:.0f}")
    
    print(f"{'='*60}\n")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python export_features.py ml_features.bin             # Show summary")
        print("  python export_features.py ml_features.bin output.csv  # Export to CSV")
        sys.exit(1)
    
    binary_path = sys.argv[1]
    
    if len(sys.argv) >= 3:
        csv_path = sys.argv[2]
        export_to_csv(binary_path, csv_path)
    else:
        show_summary(binary_path)

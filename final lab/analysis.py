import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from datetime import datetime
from pathlib import Path

# ==========================================
#        CONFIGURATION AREA (PLAY HERE)
# ==========================================
FILE_NAME = "nrf_high_speed_data.csv"
DEVICE_VOLTAGE = 3.3   # Voltage across the measured device
ADC_VOLTAGE_REF = 5.0  # Arduino ADC reference voltage for the voltmeter
SAMPLING_RATE_MS = 10  # Your target interval (e.g., 10 for 10ms)

# --- Peak Detection Tuning ---
# Prominence: How much mA a peak must rise above the baseline to be counted
PEAK_PROMINENCE = 8.0  

# Distance (Seconds): How many seconds to "wait" before counting another peak
# Set this slightly lower than your loop delay (e.g., 2.0s if you delay 5s)
PEAK_DISTANCE_SEC = 2.0 

# Width (Seconds): How long the "mountain" must last to be counted
# Set this to 0.1s to catch 500ms mountains but ignore tiny noise
PEAK_WIDTH_SEC = 0.1   

# --- Filter Settings ---
MIN_mA = 2.0           # Ignore anything below this (0.0 noise)
MAX_mA = 200.0         # Ignore hardware glitches/spikes; raise if your peaks exceed this

# --- Custom Peak Detection ---
def detect_peaks(signal, baseline, prominence, min_distance, min_width):
    if len(signal) < 3:
        return np.array([], dtype=int), {'widths': np.array([], dtype=float)}

    peaks = []
    widths = []
    last_peak = -min_distance
    threshold = baseline + prominence

    for i in range(1, len(signal) - 1):
        if signal[i] >= threshold and signal[i] >= signal[i - 1] and signal[i] >= signal[i + 1]:
            if i - last_peak < min_distance:
                continue

            half_height = baseline + (signal[i] - baseline) / 2.0
            left = i
            while left > 0 and signal[left] > half_height:
                left -= 1
            right = i
            while right < len(signal) - 1 and signal[right] > half_height:
                right += 1

            width = right - left - 1
            if width >= min_width:
                peaks.append(i)
                widths.append(width)
                last_peak = i

    return np.array(peaks, dtype=int), {'widths': np.array(widths, dtype=float)}

# --- Battery Capacities (mWh) ---
CAP_CR2032 = 660.0     # 220mAh * 3.0V
CAP_LIPO   = 1850.0    # 500mAh * 3.7V
# ==========================================

print(f"--- Starting Analysis on {FILE_NAME} ---")

try:
    # 1. Load and Fix Data Types
    csv_path = Path(FILE_NAME)
    if not csv_path.exists():
        csv_path = Path(__file__).resolve().parent.parent / FILE_NAME

    df = pd.read_csv(csv_path)
    df['Current_mA'] = pd.to_numeric(df['Current_mA'], errors='coerce')
    df = df.dropna(subset=['Current_mA'])
    
    # 2. Parse Timestamps
    df['Timestamp'] = pd.to_datetime(df['Timestamp'], format='%H:%M:%S.%f')
    
    # 3. Apply Filters
    df = df[(df['Current_mA'] >= MIN_mA) & (df['Current_mA'] <= MAX_mA)].copy()
    
    # 4. Compute Power using device voltage, not Arduino reference voltage
    df['Power_mW'] = df['Current_mA'] * DEVICE_VOLTAGE

except Exception as e:
    print(f"CRITICAL ERROR: {e}")
    exit()

# --- Mathematical Calculations ---
times = df['Timestamp']
currents = df['Current_mA'].values
powers = df['Power_mW'].values

# Calculate Actual Sampling Rate from data
duration_sec = (times.iloc[-1] - times.iloc[0]).total_seconds()
actual_hz = len(df) / duration_sec if duration_sec > 0 else 100

# Convert Time-based variables to Sample-based variables
dist_samples = int(PEAK_DISTANCE_SEC * actual_hz)
width_samples = int(PEAK_WIDTH_SEC * actual_hz)

# --- DETECTION ENGINE ---
baseline_ma = np.median(currents)
peaks, props = detect_peaks(
    currents,
    baseline=baseline_ma,
    prominence=PEAK_PROMINENCE,
    min_distance=dist_samples,
    min_width=width_samples
)

# --- OUTPUT STATISTICS ---
print("\n" + "="*40)
print("       HARDWARE PROFILE RESULTS       ")
print("="*40)
print(f"Recorded Duration:      {duration_sec:.1f} Seconds")
print(f"Actual Sampling Rate:   {actual_hz:.2f} Hz")
print(f"Baseline Draw:          {baseline_ma:.2f} mA")
print(f"Overall Avg Power:      {df['Power_mW'].mean():.2f} mW")
print("-" * 40)
print(f"Mountains Detected:     {len(peaks)}")

if len(peaks) > 0:
    avg_peak_ma = currents[peaks].mean()
    avg_dur_ms = (props['widths'].mean() / actual_hz) * 1000
    energy_mJ = (avg_peak_ma * DEVICE_VOLTAGE) * (avg_dur_ms / 1000.0)
    
    print(f"Avg Mountain Height:    {avg_peak_ma:.2f} mA")
    print(f"Avg Mountain Width:     {avg_dur_ms:.1f} ms")
    print(f"Energy per Mountain:    {energy_mJ:.4f} mJ")

print("\n" + "="*40)
print("       BATTERY LIFE ESTIMATES         ")
print("="*40)
avg_mw = df['Power_mW'].mean()
print(f"CR2032 (Coin Cell):     {CAP_CR2032 / avg_mw:.1f} Hours")
print(f"500mAh LiPo:            {CAP_LIPO / avg_mw:.1f} Hours")
print("="*40)

# --- DUAL-PLOT VISUALIZATION ---
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8), sharex=True)

# Plot 1: Current
ax1.plot(times, currents, color='#1f77b4', label='Current (mA)', linewidth=1)
ax1.scatter(times.iloc[peaks], currents[peaks], color='red', s=40, label='Detection Point')
ax1.axhline(y=baseline_ma, color='green', linestyle='--', alpha=0.5, label='Idle Baseline')
ax1.set_ylabel('Current (mA)')
ax1.set_title(f'nRF52840 Analysis @ {actual_hz:.1f} Hz')
ax1.legend(loc='upper right')
ax1.grid(True, alpha=0.3)

# Plot 2: Power
ax2.plot(times, powers, color='#ff7f0e', label='Power (mW)', linewidth=1)
ax2.set_ylabel('Power (mW)')
ax2.set_xlabel('Time (HH:MM:SS)')
ax2.grid(True, alpha=0.3)
ax2.legend(loc='upper right')

plt.tight_layout()
plt.show()
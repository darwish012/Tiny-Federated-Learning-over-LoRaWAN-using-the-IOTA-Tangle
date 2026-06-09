import serial
import csv
import signal
import sys
from datetime import datetime

# --- Configuration ---
SERIAL_PORT = 'COM30'  # Matches your last setup
BAUD_RATE = 115200
FILE_NAME = "nrf_high_speed_data.csv"

# Global flag to handle clean exits
keep_running = True

def signal_handler(sig, frame):
    global keep_running
    print("\n[STOPPING] Closing file and port...")
    keep_running = False

# Register the CTRL+C signal
signal.signal(signal.SIGINT, signal_handler)

def run_logger():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print(f"--- LOGGING STARTED ON {SERIAL_PORT} ---")
        print("Capturing at full speed. Press CTRL+C to stop.")
    except Exception as e:
        print(f"Error: Could not open {SERIAL_PORT}. {e}")
        return

    with open(FILE_NAME, mode='w', newline='') as file:
        writer = csv.writer(file)
        # We include milliseconds (%f) to track the high-speed sampling
        writer.writerow(["Timestamp", "Current_mA"])
        
        sample_count = 0
        
        while keep_running:
            try:
                # Read the raw line from Arduino; this avoids per-line polling
                raw_line = ser.readline()
                if not raw_line:
                    continue

                line = raw_line.decode('utf-8', errors='ignore').strip()
                if not line:
                    continue

                try:
                    current_value = float(line)
                except ValueError:
                    continue

                # Capture timestamp with millisecond precision
                now = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                
                # Save to CSV without extra console output
                writer.writerow([now, current_value])
                
                sample_count += 1
                if sample_count % 5000 == 0:
                    print(f"Samples Captured: {sample_count}", end='\r')

            except Exception as e:
                print(f"\n[ERROR] Data skip: {e}")
                continue
        
        ser.close()

if __name__ == "__main__":
    run_logger()
    print("Done! Use analysis.py to view your high-speed data.")
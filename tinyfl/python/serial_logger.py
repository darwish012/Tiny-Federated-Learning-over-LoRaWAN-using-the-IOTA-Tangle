import sys
import os
import csv
import re
import time
from datetime import datetime

# Try to import serial, providing helper instructions if missing
try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Error: 'pyserial' package is not installed.")
    print("Please install it using your Python environment, for example:")
    print("    pip install pyserial")
    sys.exit(1)

# ANSI escape codes for beautiful terminal colors
class Color:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    GREEN = "\033[32m"
    CYAN = "\033[36m"
    YELLOW = "\033[33m"
    RED = "\033[31m"
    MAGENTA = "\033[35m"
    GRAY = "\033[90m"

def get_serial_ports():
    """Lists all available serial ports."""
    ports = list(serial.tools.list_ports.comports())
    return ports

def select_port(args_port=None):
    """Selects the serial port based on args or interactive user choice."""
    ports = get_serial_ports()
    
    if args_port:
        return args_port

    if not ports:
        print(f"{Color.RED}No serial ports detected.{Color.RESET} Please check your device connection.")
        sys.exit(1)

    if len(ports) == 1:
        selected = ports[0].device
        print(f"Only one serial port detected: {Color.CYAN}{selected} ({ports[0].description}){Color.RESET}")
        return selected

    print(f"\n{Color.BOLD}Available Serial Ports:{Color.RESET}")
    for idx, port in enumerate(ports):
        print(f"  [{idx + 1}] {Color.GREEN}{port.device}{Color.RESET} - {port.description}")
    
    while True:
        try:
            choice = input(f"\nSelect port (1-{len(ports)}): ").strip()
            if not choice:
                # Default to the first port if user presses enter
                return ports[0].device
            idx = int(choice) - 1
            if 0 <= idx < len(ports):
                return ports[idx].device
            else:
                print(f"Invalid choice. Please enter a number between 1 and {len(ports)}.")
        except ValueError:
            print("Invalid input. Please enter a number.")

def parse_line(line):
    """
    Parses a single line of serial output to extract structured metrics.
    Returns: (event_type, parsed_dict)
    """
    line = line.strip()
    
    # 1. Check for local training epoch log
    # Example: "  Epoch  0  Loss=1.3863  Acc=25.0%"
    epoch_match = re.search(r'Epoch\s+(\d+)\s+Loss=([\d\.]+)\s+Acc=([\d\.]+)%', line)
    if epoch_match:
        return "TRAIN_EPOCH", {
            "epoch": int(epoch_match.group(1)),
            "loss": float(epoch_match.group(2)),
            "accuracy": float(epoch_match.group(3)),
        }
        
    # 2. Check for local validation accuracy log
    # Example: "[ACCURACY] Validation: 25.0% (n=10)"
    val_match = re.search(r'\[ACCURACY\] Validation:\s*([\d\.]+)%\s*\(n=(\d+)\)', line)
    if val_match:
        return "VALIDATION", {
            "val_accuracy": float(val_match.group(1)),
            "sample_count": int(val_match.group(2))
        }

    # 3. Check for new mock data sample log
    # Example: "[DATA] class=dog (n=1)"
    data_match = re.search(r'\[DATA\] class=(\w+)\s*\(n=(\d+)\)', line)
    if data_match:
        return "NEW_SAMPLE", {
            "detected_class": data_match.group(1),
            "sample_count": int(data_match.group(2))
        }
        
    # 4. General system logs
    # Example: "[BLE] Connected", "[SYSTEM] TinyFL Ready...", "[FL] Model sent (72 bytes)"
    sys_match = re.search(r'^\[(BLE|SYSTEM|FL)\]\s*(.*)', line)
    if sys_match:
        return sys_match.group(1), {
            "extra_info": sys_match.group(2)
        }

    return "RAW", {}

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Read TinyFL ESP32/Arduino serial outputs and save to CSV and TXT.")
    parser.add_argument("-p", "--port", help="COM port (e.g. COM3 or /dev/ttyUSB0). If not specified, scans and prompts.")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("-o", "--output-txt", default="serial_log.txt", help="Output TXT filepath (default: serial_log.txt)")
    parser.add_argument("-c", "--output-csv", default="serial_log.csv", help="Output CSV filepath (default: serial_log.csv)")
    args = parser.parse_args()

    port = select_port(args.port)
    txt_file = args.output_txt
    csv_file = args.output_csv
    baud_rate = args.baud

    print(f"\n{Color.BOLD}Configuring Session:{Color.RESET}")
    print(f"  Port:      {Color.CYAN}{port}{Color.RESET}")
    print(f"  Baud:      {Color.CYAN}{baud_rate}{Color.RESET}")
    print(f"  TXT File:  {Color.GREEN}{txt_file}{Color.RESET}")
    print(f"  CSV File:  {Color.GREEN}{csv_file}{Color.RESET}")
    print(f"Connecting to device... (Press {Color.RED}Ctrl+C{Color.RESET} to stop logging at any time)\n")

    # CSV headers definition
    headers = [
        "timestamp", 
        "epoch_time", 
        "event_type", 
        "epoch", 
        "loss", 
        "accuracy", 
        "val_accuracy", 
        "sample_count", 
        "detected_class", 
        "extra_info",
        "raw_message"
    ]

    # Open CSV file and write header
    try:
        f_csv = open(csv_file, mode="w", newline="", encoding="utf-8")
        writer = csv.DictWriter(f_csv, fieldnames=headers)
        writer.writeheader()
        f_csv.flush()
    except Exception as e:
        print(f"{Color.RED}Failed to open output CSV file: {e}{Color.RESET}")
        sys.exit(1)

    # Open TXT file
    try:
        f_txt = open(txt_file, mode="w", encoding="utf-8")
    except Exception as e:
        print(f"{Color.RED}Failed to open output TXT file: {e}{Color.RESET}")
        f_csv.close()
        sys.exit(1)

    try:
        ser = serial.Serial(port, baud_rate, timeout=1)
        # Flush buffers to avoid reading old garbage
        ser.reset_input_buffer()
        print(f"{Color.GREEN}Connected successfully!{Color.RESET} Streaming logs...\n")
    except Exception as e:
        print(f"{Color.RED}Failed to connect to port {port}: {e}{Color.RESET}")
        f_csv.close()
        f_txt.close()
        sys.exit(1)

    log_count = 0
    
    try:
        while True:
            if ser.in_waiting > 0:
                try:
                    # Read line and decode
                    raw_bytes = ser.readline()
                    line = raw_bytes.decode('utf-8', errors='replace').rstrip('\r\n')
                except Exception as read_err:
                    print(f"{Color.YELLOW}[Warning] Read error: {read_err}{Color.RESET}")
                    continue

                if not line:
                    continue

                # Generate Timestamps
                now = datetime.now()
                iso_time = now.isoformat()
                epoch_time = time.time()
                timestamp_str = now.strftime("%Y-%m-%d %H:%M:%S")

                # Parse metrics from line
                event_type, parsed_data = parse_line(line)

                # Write to TXT (raw log with timestamp)
                f_txt.write(f"[{timestamp_str}] {line}\n")
                f_txt.flush()

                # Build row structure for CSV
                row = {
                    "timestamp": iso_time,
                    "epoch_time": epoch_time,
                    "event_type": event_type,
                    "epoch": parsed_data.get("epoch", ""),
                    "loss": parsed_data.get("loss", ""),
                    "accuracy": parsed_data.get("accuracy", ""),
                    "val_accuracy": parsed_data.get("val_accuracy", ""),
                    "sample_count": parsed_data.get("sample_count", ""),
                    "detected_class": parsed_data.get("detected_class", ""),
                    "extra_info": parsed_data.get("extra_info", ""),
                    "raw_message": line
                }

                # Write to CSV
                writer.writerow(row)
                f_csv.flush()
                log_count += 1

                # Pretty print in the console based on event type
                time_only = now.strftime("%H:%M:%S")
                prefix = f"{Color.GRAY}[{time_only}]{Color.RESET}"


                if event_type == "TRAIN_EPOCH":
                    epoch = row["epoch"]
                    loss = row["loss"]
                    acc = row["accuracy"]
                    print(f"{prefix} {Color.YELLOW}[EPOCH {epoch:2d}]{Color.RESET} Loss={loss:.4f} Acc={acc:.1f}%")
                elif event_type == "VALIDATION":
                    val_acc = row["val_accuracy"]
                    n = row["sample_count"]
                    print(f"{prefix} {Color.GREEN}{Color.BOLD}[VAL ACCURACY]{Color.RESET} {Color.GREEN}{val_acc:.1f}% (n={n}){Color.RESET}")
                elif event_type == "NEW_SAMPLE":
                    cls = row["detected_class"]
                    n = row["sample_count"]
                    print(f"{prefix} {Color.CYAN}[DATA]{Color.RESET} New sample class={cls} (total samples={n})")
                elif event_type in ["BLE", "SYSTEM", "FL"]:
                    color = Color.MAGENTA if event_type == "BLE" else (Color.CYAN if event_type == "FL" else Color.BOLD)
                    print(f"{prefix} {color}[{event_type}]{Color.RESET} {row['extra_info']}")
                else:
                    # Print raw output line if it doesn't match any templates
                    print(f"{prefix} {line}")

            else:
                # Idle delay to reduce CPU usage
                time.sleep(0.01)

    except KeyboardInterrupt:
        print(f"\n\n{Color.YELLOW}Logging stopped by user.{Color.RESET}")
    finally:
        try:
            f_csv.close()
        except Exception:
            pass
        try:
            f_txt.close()
        except Exception:
            pass
        try:
            ser.close()
        except Exception:
            pass
        print(f"Data saved to:")
        print(f"  TXT Log:  {Color.GREEN}{os.path.abspath(txt_file)}{Color.RESET}")
        print(f"  CSV Data: {Color.GREEN}{os.path.abspath(csv_file)}{Color.RESET}")
        print(f"Total lines logged: {Color.BOLD}{log_count}{Color.RESET}")

if __name__ == "__main__":
    main()

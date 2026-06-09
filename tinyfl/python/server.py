import asyncio
from bleak import BleakClient, BleakScanner
import struct
import numpy as np
import sys
import re
import atexit
from datetime import datetime

# Setup logging to file and console
LOG_FILE_PATH = "server_log.txt"
try:
    log_file = open(LOG_FILE_PATH, "w", encoding="utf-8")
except Exception as log_err:
    log_file = None
    sys.stderr.write(f"Warning: Could not open {LOG_FILE_PATH} for writing: {log_err}\n")

# Custom print wrapper to log server outputs to file
_original_print = print
def print(*args, **kwargs):
    _original_print(*args, **kwargs)
    if log_file:
        timestamp = datetime.now().strftime("[%Y-%m-%d %H:%M:%S]")
        sep = kwargs.get('sep', ' ')
        end = kwargs.get('end', '\n')
        msg = sep.join(map(str, args)) + end
        # Strip out any ANSI color codes for clean text logs
        clean_msg = re.sub(r'\033\[[0-9;]*m', '', msg)
        log_file.write(f"{timestamp} {clean_msg}")
        log_file.flush()

def close_log_file():
    if log_file:
        try:
            log_file.close()
        except Exception:
            pass

atexit.register(close_log_file)

np.random.seed(42)

# ── UUIDs ─────────────────────────────
WEIGHT_CHAR_UUID = "12345678-1234-1234-1234-123456789001"
GLOBAL_CHAR_UUID = "12345678-1234-1234-1234-123456789002"

# ── MODEL SIZE ────────────────────────
# LoRA B-only rank-4: B1(32) + B2(16) + B3(16) = 64 weights
NUM_WEIGHTS = 64

# ── TRACKING ──────────────────────────
global_model_history = []
round_accuracies = []
fl_round = 0

# ── CONSTANTS ─────────────────────────
LORA_ALPHA = 0.5
QUANT_SCALE = 0.1
SERVER_MOMENTUM = 0.5
CLASS_NAMES = ["dog", "cat", "horse", "other"]

# ── BASE WEIGHTS ──────────────────────
BASE_W1 = np.sin(np.arange(400, dtype=np.float32) * 0.1) * 0.1
BASE_W2 = np.array([
    0.1, 0.2, -0.1, 0.05,
    -0.2, 0.1, 0.3, -0.1,
    0.05, -0.2, 0.2, 0.1,
    0.3, 0.1, -0.1, 0.2,
    0.2, -0.3, 0.1, 0.05,
    0.1, 0.2, 0.1, -0.2,
    -0.1, 0.3, 0.2, 0.1,
    0.2, 0.1, -0.2, 0.3
], dtype=np.float32)
BASE_W3 = np.array([
     0.10, -0.15,  0.05,  0.20,   # class 0 (dog)
    -0.10,  0.20, -0.05, -0.15,   # class 1 (cat)
     0.15, -0.10,  0.20, -0.05,   # class 2 (horse)
    -0.05,  0.05, -0.20,  0.10    # class 3 (other)
], dtype=np.float32)

# ── A MATRIX INIT ─────────────────────
def _lcg_init(n, scale, seed):
    """Deterministic pseudo-random init matching Arduino LCG."""
    values = []
    s = seed
    for i in range(n):
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        values.append((s / 0x7FFFFFFF - 0.5) * scale)
    return np.array(values, dtype=np.float32), s

_seed = 42
A1_INIT, _seed = _lcg_init(200, 0.4, _seed)  # 4×50
A2_INIT, _seed = _lcg_init(32, 0.4, _seed)   # 4×8
A3_INIT, _seed = _lcg_init(16, 0.4, _seed)   # 4×4

# ── BALANCED RANDOM PROJECTION ────────
def generate_projection_matrix():
    P = np.zeros((4, 50), dtype=np.float32)
    s = 42
    for c in range(4):
        for k in range(50):
            s = (s * 1103515245 + 12345) & 0x7FFFFFFF
            P[c, k] = (s / 0x7FFFFFFF) - 0.5
        P[c] -= np.mean(P[c])
    return P

PROJECTION_P = generate_projection_matrix()

# ── TEST DATA ─────────────────────────
def generate_test_data(n_samples=50):
    np.random.seed(42)
    features = np.random.rand(n_samples, 50).astype(np.float32)
    labels = np.zeros(n_samples, dtype=np.int32)
    for i in range(n_samples):
        scores = np.dot(PROJECTION_P, features[i])
        labels[i] = int(np.argmax(scores))
    return features, labels

test_features, test_labels = generate_test_data(50)

# ── EVALUATION ────────────────────────
def evaluate_global_model(weights, test_features, test_labels):
    """Evaluate accuracy on test data."""
    adapter = build_lora_adapter(weights)
    correct = 0
    for i in range(len(test_features)):
        pred = simulate_nn_forward(test_features[i], adapter)
        if pred == test_labels[i]:
            correct += 1
    return correct / len(test_features) if len(test_features) > 0 else 0

def build_lora_adapter(weights):
    return {
        'A1': A1_INIT.reshape(4, 50),
        'A2': A2_INIT.reshape(4, 8),
        'A3': A3_INIT.reshape(4, 4),
        'B1': weights[0:32].reshape(8, 4),
        'B2': weights[32:48].reshape(4, 4),
        'B3': weights[48:64].reshape(4, 4),
    }

def simulate_nn_forward(x, adapter):
    """Forward pass matching Arduino: 4-class softmax output."""
    # Pre-calc A1 @ x
    a1_lora_in = np.zeros(4, dtype=np.float32)
    for r in range(4):
        for k in range(50):
            a1_lora_in[r] += adapter['A1'][r, k] * x[k]

    # Layer 1: 50 -> 8
    a1 = np.zeros(8, dtype=np.float32)
    w1 = BASE_W1.reshape(8, 50)
    for i in range(8):
        base = 0.0
        for k in range(50):
            base += w1[i, k] * x[k]
        lora_c = 0.0
        for r in range(4):
            lora_c += adapter['B1'][i, r] * a1_lora_in[r]
        a1[i] = max(0.0, base + LORA_ALPHA * lora_c)

    # Layer 2: 8 -> 4
    a2 = np.zeros(4, dtype=np.float32)
    for j in range(4):
        base = 0.0
        lora_c = 0.0
        for i in range(8):
            base += BASE_W2[j * 8 + i] * a1[i]
            inner = 0.0
            for r in range(4):
                inner += adapter['B2'][j, r] * adapter['A2'][r, i]
            lora_c += inner * a1[i]
        a2[j] = max(0.0, base + LORA_ALPHA * lora_c)

    # Layer 3: 4 -> 4 (OUTPUT_SIZE)
    logits = np.zeros(4, dtype=np.float32)
    for o in range(4):
        base = 0.0
        lora_c = 0.0
        for j in range(4):
            base += BASE_W3[o * 4 + j] * a2[j]
            inner = 0.0
            for r in range(4):
                inner += adapter['B3'][o, r] * adapter['A3'][r, j]
            lora_c += inner * a2[j]
        logits[o] = base + LORA_ALPHA * lora_c

    # Softmax
    logits -= np.max(logits)
    exp_l = np.exp(logits)
    probs = exp_l / np.sum(exp_l)

    return int(np.argmax(probs))

# ── FL STATE ──────────────────────────
client_weights_buffer = []
client_samples_buffer = []
update_queue = asyncio.Queue()

SIMULATION_MODE = True
SIM_CLIENTS = 3

# ── CALLBACK ──────────────────────────
def notification_handler(sender, data):
    global client_weights_buffer, client_samples_buffer, fl_round

    expected_bytes = NUM_WEIGHTS + 4 + 4  # 64 + 4 + 4 = 72

    if len(data) != expected_bytes:
        print(f"[ERROR] Bad payload: {len(data)} bytes, expected {expected_bytes}")
        return

    unpacked = struct.unpack(f'<{NUM_WEIGHTS}bif', data)

    quantized = np.array(unpacked[:NUM_WEIGHTS], dtype=np.int8)
    weights = quantized.astype(np.float32) / (127.0 * QUANT_SCALE)

    n_samples = unpacked[NUM_WEIGHTS]
    client_acc = unpacked[NUM_WEIGHTS + 1]

    print(f"\n[CLIENT] Received | samples={n_samples} | node accuracy={client_acc:.1%}")
    print(f"[CLIENT] Received weights: min={np.min(weights):.4f}, max={np.max(weights):.4f}, mean={np.mean(weights):.4f}")

    if SIMULATION_MODE:
        client_types = [
            {"noise": 0.001, "samples_mult": 1.0},
            {"noise": 0.002, "samples_mult": 0.8},
            {"noise": 0.003, "samples_mult": 1.2},
        ]
        for cid, ct in enumerate(client_types):
            noise = np.random.normal(0, ct["noise"], size=weights.shape)
            client_weights_buffer.append(weights + noise)
            client_samples_buffer.append(int(n_samples * ct["samples_mult"]))
        print(f"[SIM] Created {SIM_CLIENTS} heterogeneous clients")
    else:
        client_weights_buffer.append(weights)
        client_samples_buffer.append(n_samples)

    # ── FEDAVG ───────────────────────
    if len(client_weights_buffer) >= SIM_CLIENTS:
        fl_round += 1
        print(f"\n[FL] Round {fl_round}: Federated Averaging...")

        w_stack = np.stack(client_weights_buffer)
        s = np.array(client_samples_buffer)
        total = np.sum(s)

        fedavg = np.sum(w_stack * s[:, None], axis=0) / total

        # EMA smoothing
        if len(global_model_history) > 0:
            global_weights = SERVER_MOMENTUM * global_model_history[-1] + \
                             (1 - SERVER_MOMENTUM) * fedavg
        else:
            global_weights = fedavg

        print(f"[FL] Aggregated {total} samples from {SIM_CLIENTS} clients")

        global_model_history.append(global_weights.copy())

        # Evaluate
        accuracy = evaluate_global_model(global_weights, test_features, test_labels)
        round_accuracies.append(accuracy)

        print(f"[ACCURACY] Global accuracy: {accuracy:.1%}")
        if len(round_accuracies) > 1:
            delta = round_accuracies[-1] - round_accuracies[-2]
            print(f"[ACCURACY] Change: {delta:+.1%}")

            if len(round_accuracies) >= 4 and all(a > 0.80 for a in round_accuracies[-3:]):
                print(f"[CONVERGENCE] Converged at {accuracy:.1%} after {fl_round} rounds!")
        else:
            print(f"[ACCURACY] Baseline: {accuracy:.1%}")

        # Reset
        client_weights_buffer.clear()
        client_samples_buffer.clear()

        # Re-quantize & send
        q = np.clip(np.round(global_weights * QUANT_SCALE * 127.0), -128, 127).astype(np.int8)
        print(f"[FL] Global weights to send: min={np.min(global_weights):.4f}, max={np.max(global_weights):.4f}, mean={np.mean(global_weights):.4f}")
        global_payload = struct.pack(f'<{NUM_WEIGHTS}b', *q)
        update_queue.put_nowait(global_payload)

# ── SERVER LOOP ───────────────────────
async def run_server():
    while True:
        try:
            print("[BLE] Scanning for TinyFL-Node...")
            device = await BleakScanner.find_device_by_name("TinyFL-Node")

            if not device:
                print("[ERROR] Device not found, retrying...")
                await asyncio.sleep(2)
                continue

            print(f"[BLE] Connecting to {device.name} [{device.address}]")

            async with BleakClient(device.address) as client:
                print("[BLE] Connected")
                await client.start_notify(WEIGHT_CHAR_UUID, notification_handler)
                print("[BLE] Listening for FL updates...\n")

                while True:
                    global_payload = await update_queue.get()
                    print("[FL] Sending global model...")
                    await client.write_gatt_char(
                        GLOBAL_CHAR_UUID, global_payload, response=True
                    )
                    print("[FL] Global model sent successfully\n")

        except Exception as e:
            print(f"[ERROR] {e}")
            print("[BLE] Reconnecting...\n")
            await asyncio.sleep(2)


if __name__ == "__main__":
    asyncio.run(run_server())
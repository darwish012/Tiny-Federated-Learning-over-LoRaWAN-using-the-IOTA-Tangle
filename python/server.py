import asyncio
from bleak import BleakClient, BleakScanner
import struct
import numpy as np

np.random.seed(42)

# ── UUIDs ─────────────────────────────
WEIGHT_CHAR_UUID = "12345678-1234-1234-1234-123456789001"
GLOBAL_CHAR_UUID = "12345678-1234-1234-1234-123456789002"

# ── MODEL SIZE (IMPORTANT) ────────────
# LoRA B-only rank-4: B1(32) + B2(16) + B3(4) = 52 weights
NUM_WEIGHTS = 52   # LoRA B matrices only

# ── ACCURACY TRACKING ───────────────────
global_model_history = []
round_accuracies = []
fl_round = 0

# ── SYNTHETIC TEST DATA ─────────────────
LORA_ALPHA = 0.5

BASE_W1 = np.array([0.2, -0.1, 0.3, -0.2, 0.1, 0.05, -0.3, 0.4], dtype=np.float32)
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
BASE_W3 = np.array([0.4, -0.3, 0.2, -0.1], dtype=np.float32)

A1_INIT = np.array([0.1 + 0.01 * i for i in range(4)], dtype=np.float32)
A2_INIT = np.array([0.1 + 0.01 * i for i in range(32)], dtype=np.float32)
A3_INIT = np.array([0.1 + 0.01 * i for i in range(16)], dtype=np.float32)


def target_function(t):
    """A deterministic function combining sinusoids, polynomial trend, and offset."""
    return (
        25.0
        + 2.0 * np.sin(0.5 * t)
        + 1.2 * np.sin(1.3 * t + 0.5)
        + 0.8 * np.cos(0.25 * t)
        + 0.03 * t
        - 0.002 * t * t
    )


def generate_test_data(n_samples=50):
    """Generate a synthetic sequence from a known function, with noise."""
    np.random.seed(42)  # For reproducible results

    temps = []
    for i in range(n_samples):
        t = i * 0.5
        value = target_function(t)
        noise = np.random.normal(0, 0.2)
        temps.append(value + noise)

    return np.array(temps)


test_data = generate_test_data(50)  # Generate once for consistency


def evaluate_global_model(weights, test_data):
    """Evaluate model accuracy on test data"""
    lora_adapter = build_lora_adapter(weights)

    mse = 0
    count = 0

    for i in range(len(test_data) - 1):
        x = (test_data[i] - 25.0) / 5.0  # Normalize
        y_true = (test_data[i+1] - 25.0) / 5.0
        y_pred = simulate_nn_forward(x, lora_adapter)

        error = y_pred - y_true
        mse += error * error
        count += 1

    return mse / count if count > 0 else 0


def build_lora_adapter(weights):
    """Build a LoRA adapter with fixed A matrices and shared B matrices."""
    return {
        'A1': A1_INIT,
        'A2': A2_INIT.reshape(4, 8),
        'A3': A3_INIT.reshape(4, 4),
        'B1': weights[0:32].reshape(8, 4),
        'B2': weights[32:48].reshape(4, 4),
        'B3': weights[48:52].reshape(4),
    }


def simulate_nn_forward(x, adapter):
    """Forward pass matching the Arduino model and LoRA adapter math."""
    # Layer 1: 1 -> 8
    a1 = np.zeros(8, dtype=np.float32)
    for i in range(8):
        base = BASE_W1[i] * x
        lora_contrib = 0.0
        for r in range(4):
            lora_contrib += adapter['B1'][i, r] * adapter['A1'][r]
        z1 = base + LORA_ALPHA * lora_contrib * x
        a1[i] = max(0.0, z1)

    # Layer 2: 8 -> 4
    a2 = np.zeros(4, dtype=np.float32)
    for j in range(4):
        base = 0.0
        for i in range(8):
            base += BASE_W2[j * 8 + i] * a1[i]

        lora_contrib = 0.0
        for i in range(8):
            inner = 0.0
            for r in range(4):
                inner += adapter['B2'][j, r] * adapter['A2'][r, i]
            lora_contrib += inner * a1[i]

        z2 = base + LORA_ALPHA * lora_contrib
        a2[j] = max(0.0, z2)

    # Layer 3: 4 -> 1
    out = 0.0
    for j in range(4):
        out += BASE_W3[j] * a2[j]

    lora_contrib = 0.0
    for j in range(4):
        inner = 0.0
        for r in range(4):
            inner += adapter['B3'][r] * adapter['A3'][r, j]
        lora_contrib += inner * a2[j]

    out += LORA_ALPHA * lora_contrib
    return out

client_weights_buffer = []
client_samples_buffer = []
update_queue = asyncio.Queue()

# ── OPTIONAL SIMULATION ───────────────
SIMULATION_MODE = True
SIM_CLIENTS = 3


# ───────────────────────────────────────
# CALLBACK
# ───────────────────────────────────────
def notification_handler(sender, data):

    global client_weights_buffer, client_samples_buffer, fl_round

    expected_bytes = NUM_WEIGHTS * 4 + 4 + 4  # floats + int32 + float

    if len(data) != expected_bytes:
        print(f"[ERROR] Bad payload size: {len(data)} expected {expected_bytes}")
        return

    # unpack floats + int + float
    unpacked = struct.unpack(f'<{NUM_WEIGHTS}fif', data)

    weights = np.array(unpacked[:NUM_WEIGHTS], dtype=np.float32)
    n_samples = unpacked[NUM_WEIGHTS]
    client_mse = unpacked[NUM_WEIGHTS + 1]

    print(f"\n[CLIENT] Received model | samples = {n_samples} | node MSE = {client_mse:.5f}")

    # ── SIMULATION (optional) ──
    if SIMULATION_MODE:
        # Create heterogeneous clients with different characteristics
        client_types = [
            {"noise": 0.001, "samples_mult": 1.0},  # Good client
            {"noise": 0.002, "samples_mult": 0.8},  # Average client  
            {"noise": 0.003, "samples_mult": 1.2},  # Noisy but data-rich client
        ]
        
        for cid, client_type in enumerate(client_types):
            noise_level = client_type["noise"]
            samples_for_client = int(n_samples * client_type["samples_mult"])
            
            noise = np.random.normal(0, noise_level, size=weights.shape)
            client_weights_buffer.append(weights + noise)
            client_samples_buffer.append(samples_for_client)

        print(f"[SIM] Created {SIM_CLIENTS} heterogeneous clients with varying quality")
    else:
        client_weights_buffer.append(weights)
        client_samples_buffer.append(n_samples)

    # ── FEDAVG ───────────────────────
    if len(client_weights_buffer) >= SIM_CLIENTS:

        fl_round += 1
        print(f"\n[FL] Round {fl_round}: Performing weighted Federated Averaging...")

        weights_stack = np.stack(client_weights_buffer)
        samples = np.array(client_samples_buffer)

        total_samples = np.sum(samples)

        global_weights = np.sum(
            weights_stack * samples[:, None],
            axis=0
        ) / total_samples

        print(f"[FL] Total samples aggregated: {total_samples}")
        
        # Log individual client contributions
        client_contributions = [f"{samples} samples" for samples in client_samples_buffer]
        print(f"[FL] Client contributions: {client_contributions}")
        
        # Store global model for history
        global_model_history.append(global_weights.copy())
        
        # Evaluate global model accuracy
        global_mse = evaluate_global_model(global_weights, test_data)
        round_accuracies.append(global_mse)
        
        print(f"[ACCURACY] Global model MSE: {global_mse:.5f}")
        if len(round_accuracies) > 1:
            improvement = round_accuracies[-2] - round_accuracies[-1]
            print(f"[ACCURACY] Improvement: {improvement:+.5f}")
            
            # Check for convergence (if improvement < 0.0001 for 3 rounds)
            if len(round_accuracies) >= 4:
                recent_improvements = [round_accuracies[i-1] - round_accuracies[i] 
                                     for i in range(-3, 0)]
                if all(imp < 0.0001 for imp in recent_improvements):
                    print(f"[CONVERGENCE] Model converged after {fl_round} rounds!")
        else:
            print(f"[ACCURACY] Baseline MSE: {global_mse:.5f}")
        
        # reset
        client_weights_buffer.clear()
        client_samples_buffer.clear()

        # send back (52 floats only)
        global_payload = struct.pack(f'<{NUM_WEIGHTS}f', *global_weights)

        update_queue.put_nowait(global_payload)


# ───────────────────────────────────────
# SERVER LOOP
# ───────────────────────────────────────
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

                await client.start_notify(
                    WEIGHT_CHAR_UUID,
                    notification_handler
                )

                print("[BLE] Listening for FL updates...\n")

                while True:
                    global_payload = await update_queue.get()

                    print("[FL] Sending global model...")

                    await client.write_gatt_char(
                        GLOBAL_CHAR_UUID,
                        global_payload,
                        response=True
                    )

                    print("[FL] Global model sent successfully\n")

        except Exception as e:
            print(f"[ERROR] {e}")
            print("[BLE] Reconnecting...\n")
            await asyncio.sleep(2)


if __name__ == "__main__":
    asyncio.run(run_server())
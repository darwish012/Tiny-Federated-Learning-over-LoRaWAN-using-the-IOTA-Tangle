import numpy as np

# Model Constants
INPUT_SIZE = 50
LORA_ALPHA = 0.5
H1 = 8
H2 = 4
OUTPUT_SIZE = 4
LORA_RANK = 4

# Base weights matching ESP32
BASE_W1 = np.sin(np.arange(400, dtype=np.float32) * 0.1).reshape(8, 50) * 0.1
BASE_W2 = np.array([
    0.1, 0.2, -0.1, 0.05,
    -0.2, 0.1, 0.3, -0.1,
    0.05, -0.2, 0.2, 0.1,
    0.3, 0.1, -0.1, 0.2,
    0.2, -0.3, 0.1, 0.05,
    0.1, 0.2, 0.1, -0.2,
    -0.1, 0.3, 0.2, 0.1,
    0.2, 0.1, -0.2, 0.3
], dtype=np.float32).reshape(4, 8)
BASE_W3 = np.array([
     0.10, -0.15,  0.05,  0.20,
    -0.10,  0.20, -0.05, -0.15,
     0.15, -0.10,  0.20, -0.05,
    -0.05,  0.05, -0.20,  0.10
], dtype=np.float32).reshape(4, 4)

def _lcg_init(n, scale, seed):
    values = []
    s = seed
    for i in range(n):
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        values.append((s / 0x7FFFFFFF - 0.5) * scale)
    return np.array(values, dtype=np.float32), s

_seed = 42
A1_INIT, _seed = _lcg_init(200, 0.4, _seed)
A1_INIT = A1_INIT.reshape(4, 50)
A2_INIT, _seed = _lcg_init(32, 0.4, _seed)
A2_INIT = A2_INIT.reshape(4, 8)
A3_INIT, _seed = _lcg_init(16, 0.4, _seed)
A3_INIT = A3_INIT.reshape(4, 4)

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

def generate_dataset(n_samples):
    np.random.seed(42)
    features = np.random.rand(n_samples, INPUT_SIZE).astype(np.float32)
    labels = np.zeros(n_samples, dtype=np.int32)
    for i in range(n_samples):
        scores = np.dot(PROJECTION_P, features[i])
        labels[i] = int(np.argmax(scores))
    return features, labels

X_train, y_train = generate_dataset(800)
X_val, y_val = generate_dataset(200)

def to_one_hot(y, num_classes=4):
    one_hot = np.zeros((len(y), num_classes), dtype=np.float32)
    one_hot[np.arange(len(y)), y] = 1.0
    return one_hot

Y_train_oh = to_one_hot(y_train, 4)
Y_val_oh = to_one_hot(y_val, 4)

class NumpyAdam:
    def __init__(self, params, lr=0.02, beta1=0.9, beta2=0.999, eps=1e-8, wd=0.001):
        self.params = params
        self.lr = lr
        self.beta1 = beta1
        self.beta2 = beta2
        self.eps = eps
        self.wd = wd
        self.t = 0
        self.m = [np.zeros_like(p) for p in params]
        self.v = [np.zeros_like(p) for p in params]

    def step(self, grads):
        self.t += 1
        for i in range(len(self.params)):
            # Clip gradients to [-1.0, 1.0] matching C++
            clipped_grad = np.clip(grads[i], -1.0, 1.0)
            self.m[i] = self.beta1 * self.m[i] + (1.0 - self.beta1) * clipped_grad
            self.v[i] = self.beta2 * self.v[i] + (1.0 - self.beta2) * (clipped_grad ** 2)
            m_hat = self.m[i] / (1.0 - self.beta1 ** self.t)
            v_hat = self.v[i] / (1.0 - self.beta2 ** self.t)
            # Update with weight decay
            self.params[i] -= self.lr * (m_hat / (np.sqrt(v_hat) + self.eps) + self.wd * self.params[i])

class NumpyLoraModel:
    def __init__(self):
        self.W1 = BASE_W1.copy()
        self.W2 = BASE_W2.copy()
        self.W3 = BASE_W3.copy()
        self.A1 = A1_INIT.copy()
        self.A2 = A2_INIT.copy()
        self.A3 = A3_INIT.copy()
        self.alpha = LORA_ALPHA
        self.B1 = np.zeros((8, 4), dtype=np.float32)
        self.B2 = np.zeros((4, 4), dtype=np.float32)
        self.B3 = np.zeros((4, 4), dtype=np.float32)
        self.params = [self.B1, self.B2, self.B3]
        # Use exact C++ learning rate (0.02) and weight decay (0.001)
        self.opt = NumpyAdam(self.params, lr=0.02, wd=0.001)

    def forward(self, X):
        self.W1_eff = self.W1 + self.alpha * (self.B1 @ self.A1)
        self.W2_eff = self.W2 + self.alpha * (self.B2 @ self.A2)
        self.W3_eff = self.W3 + self.alpha * (self.B3 @ self.A3)
        h1 = np.maximum(0.0, X @ self.W1_eff.T)
        h2 = np.maximum(0.0, h1 @ self.W2_eff.T)
        logits = h2 @ self.W3_eff.T
        exp_l = np.exp(logits - np.max(logits, axis=-1, keepdims=True))
        probs = exp_l / np.sum(exp_l, axis=-1, keepdims=True)
        return h1, h2, probs

    def train_step_sgd(self, x, y_oh):
        # Forward pass on 1 sample
        # x is [50], y_oh is [4]
        # Compute activations
        a1_lora_in = self.A1 @ x
        h1 = np.maximum(0.0, (self.W1 + self.alpha * (self.B1 @ self.A1)) @ x)
        h2 = np.maximum(0.0, (self.W2 + self.alpha * (self.B2 @ self.A2)) @ h1)
        logits = (self.W3 + self.alpha * (self.B3 @ self.A3)) @ h2
        
        # Softmax
        exp_l = np.exp(logits - np.max(logits))
        probs = exp_l / np.sum(exp_l)
        
        # Backprop
        d_logits = probs - y_oh # [4]
        
        # Layer 3
        d_B3 = self.alpha * np.outer(d_logits, a1_lora_in if LORA_RANK == INPUT_SIZE else (self.A3 @ h2)) # Wait, shape check!
        # Let's derive index by index:
        # B3 shape is (4, 4). B3[o, r] grad is alpha * d3[o] * (A3 @ h2)[r]
        # So d_B3 = alpha * d_logits (shape 4, 1) @ (A3 @ h2)^T (shape 1, 4)
        inner3 = self.A3 @ h2
        d_B3 = self.alpha * np.outer(d_logits, inner3)
        
        # Backprop to h2
        W3_eff = self.W3 + self.alpha * (self.B3 @ self.A3)
        d_h2 = W3_eff.T @ d_logits
        d_z2 = d_h2 * (h2 > 0)
        
        # Layer 2
        # B2 shape is (4, 4). B2[j, r] grad is alpha * d_z2[j] * (A2 @ h1)[r]
        inner2 = self.A2 @ h1
        d_B2 = self.alpha * np.outer(d_z2, inner2)
        
        # Backprop to h1
        W2_eff = self.W2 + self.alpha * (self.B2 @ self.A2)
        d_h1 = W2_eff.T @ d_z2
        d_z1 = d_h1 * (h1 > 0)
        
        # Layer 1
        # B1 shape is (8, 4). B1[i, r] grad is alpha * d_z1[i] * (A1 @ x)[r]
        d_B1 = self.alpha * np.outer(d_z1, a1_lora_in)
        
        self.opt.step([d_B1, d_B2, d_B3])

lora_mlp = NumpyLoraModel()
EPOCHS = 15
BATCH_SIZE = 32

print("Starting SGD training matching ESP32 implementation...")
for epoch in range(1, EPOCHS + 1):
    # Train sample by sample
    for idx in range(len(X_train)):
        lora_mlp.train_step_sgd(X_train[idx], Y_train_oh[idx])
    
    # Print stats of B1, B2, B3
    print(f"Epoch {epoch:2d}:")
    for name, B in [("B1", lora_mlp.B1), ("B2", lora_mlp.B2), ("B3", lora_mlp.B3)]:
        print(f"  {name} - min: {np.min(B):.4f}, max: {np.max(B):.4f}, mean: {np.mean(B):.4f}")

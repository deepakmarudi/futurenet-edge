# Experimental Validation Report: futurenet-edge (Module M06)
**Document Deliverable:** D7 & D8  
**Author:** FutureNet Module Team (Policy & Validation Lead)  
**Target Simulator:** ns-3.46.1 (`ns-allinone-3.46.1`)  

---

## 1. Overview & Validation Objectives
Software testing asks: *"Does the implementation behave as specified?"*  
Validation asks: *"Is the modeled physical and computational behavior sensible?"*

In accordance with **M06 SRS Section 9** and **Common Guidelines Section 10**, this report documents the three mandatory validation experiments:
- **V1 (Sanity Baseline):** Compute Capacity Scaling ($T_{\text{compute}} = \text{RequiredCycles} / \text{Capacity}$).
- **V2 (Parameter Variation):** Network Transmission Time Scaling ($T_{\text{network}} \propto \text{InputBytes}$).
- **V3 (Extreme Boundary Scenario):** Queue Buildup & Congestion Mitigation (`NearestEdgePolicy` vs. `LowestEstimatedCompletionTimePolicy`).

---

## 2. Reproducibility Metadata (CG Section 8.3)
All simulation experiments were conducted under fixed, reproducible configurations:

| Parameter | Value |
| :--- | :--- |
| **Simulator Version** | ns-3.46.1 |
| **Operating System** | Linux / Ubuntu 22.04 LTS (WSL2) |
| **RNG Seed** | 1 (`RngSeedManager::SetSeed(1)`) |
| **RNG Run Number** | 1 (`RngSeedManager::SetRun(1)`) |
| **Transport Protocol** | TCP (`SndBufSize` = `RcvBufSize` = 16 MB) |
| **Compiler** | GCC 11.4.0 / Clang 14.0 |

---

## 3. Experiment V1: Compute Capacity Scaling (Sanity Baseline)

### Objective:
Verify that in the absence of network transmission bottlenecks (near-zero delay: 10 Gbps, 0.01 ms), execution time strictly follows the theoretical closed-form formula:
$$T_{\text{compute}} = \frac{\text{RequiredCycles}}{\text{Allocated Compute Capacity (cycles/s)}}$$

### Setup:
- Fixed Task: `RequiredCycles` = $1 \times 10^9$ cycles, `InputBytes` = $1\text{ kB}$.
- Varying Compute Targets:
  1. Local Phone Node ($1 \times 10^9\text{ cycles/s}$)
  2. Edge Server ($1 \times 10^{10}\text{ cycles/s}$)
  3. Cloud Server ($1 \times 10^{11}\text{ cycles/s}$)

### Execution Command:
```bash
./ns3 run "example-basic --inputBytes=1000 --requiredCycles=1e9"
```

### Results & Theoretical Comparison:
| Target Node | Compute Capacity (cycles/s) | Theoretical $T_{\text{compute}}$ | Measured $T_{\text{compute}}$ | Absolute Error |
| :--- | :--- | :--- | :--- | :--- |
| **Local Device** | $1 \times 10^9$ | 1.0000 s (1000.0 ms) | 1000.000 ms | 0.000% |
| **Edge Server**  | $1 \times 10^{10}$ | 0.1000 s (100.0 ms)  | 100.000 ms  | 0.000% |
| **Cloud Server** | $1 \times 10^{11}$ | 0.0100 s (10.0 ms)   | 10.000 ms   | 0.000% |

**Conclusion V1:** The simulated CPU execution model achieves exact, zero-error alignment with theoretical expectations.

---

## 4. Experiment V2: Input Data Size Scaling (Parameter Variation)

### Objective:
Verify that increasing task input volume linearly increases the remote communication latency ($T_{\text{network}}$) under fixed network bandwidth, while pure compute time remains invariant.

### Setup:
- Fixed Link: Point-to-point 10 Mbps, 5 ms propagation delay.
- Fixed Workload: `RequiredCycles` = $1 \times 10^9$ cycles on Edge Server ($1 \times 10^{10}\text{ cycles/s}$).
- Varied `TaskInputBytes`: $100\text{ kB}$, $500\text{ kB}$, $1\text{ MB}$, $5\text{ MB}$, $10\text{ MB}$.

### Results:
| Input Size (bytes) | Measured Network Latency (`network_ms`) | Measured Compute Latency (`compute_ms`) | Total Latency (`e2e_ms`) |
| :--- | :--- | :--- | :--- |
| **100,000 (100 kB)** | 85.2 ms | 100.0 ms | 185.2 ms |
| **500,000 (500 kB)** | 412.8 ms | 100.0 ms | 512.8 ms |
| **1,000,000 (1 MB)** | 824.5 ms | 100.0 ms | 924.5 ms |
| **5,000,000 (5 MB)** | 4,118.0 ms | 100.0 ms | 4,218.0 ms |
| **10,000,000 (10 MB)**| 8,235.2 ms | 100.0 ms | 8,335.2 ms |

**Conclusion V2:** As expected by Shannon-Hartley and packet transmission physics, network transfer time scales linearly with data payload size ($\approx 0.82\text{ ms per kB}$ on 10 Mbps link), confirming that real ns-3 socket serialization and framing are actively utilized.

---

## 5. Experiment V3: Edge Queue Congestion & Policy Comparison (Boundary Scenario)

### Objective:
Simulate high-load congestion where 25 client devices generate task bursts targeting edge infrastructure. Compare the blind proximity policy (`NearestEdgePolicy`) against the latency-optimal policy (`LowestEstimatedCompletionTimePolicy`) across 3 heterogeneous edge servers as specified in M06 SRS Section 10 and demonstrated in `example-multiedge.cc`.

### Setup:
- **Clients:** 25 mobile client nodes (Nodes 0–24), each generating 4 tasks (100 tasks total) at 10 ms staggered intervals.
- **Task Parameters:** 100 kB input payload, 25 kB output payload, $5.0 \times 10^9$ abstract CPU cycles per task.
- **Edge Servers (3 Heterogeneous Instances):**
  - **Edge Node 26 (`10.1.0.101`):** Baseline compute capacity $1.0 \times 10^{10}$ cycles/s, nearest link (5 ms delay, 50 Mbps).
  - **Edge Node 27 (`10.1.0.105`):** Medium compute capacity $1.5 \times 10^{10}$ cycles/s, intermediate link (12 ms delay, 100 Mbps).
  - **Edge Node 28 (`10.1.0.109`):** High compute capacity $2.0 \times 10^{10}$ cycles/s, distant high-speed link (20 ms delay, 200 Mbps).
- **Queue Limits:** `MaxQueuedTasks = 100` per edge server.

### Execution Commands:
```bash
# 1. Baseline: Nearest Edge Policy
./ns3 run "example-multiedge --clientCount=25 --edgeCount=3 --tasksPerClient=4 --policy=nearest --csvPath=contrib/futurenet-edge/doc/results/v3-multiedge-nearest.csv"

# 2. Optimized: Lowest Estimated Completion Time Policy
./ns3 run "example-multiedge --clientCount=25 --edgeCount=3 --tasksPerClient=4 --policy=lowest --csvPath=contrib/futurenet-edge/doc/results/v3-multiedge-lowest.csv"
```

### Empirical Comparative Results:
*Measured from simulation runs preserved in `contrib/futurenet-edge/doc/results/v3-multiedge-nearest.csv` and `v3-multiedge-lowest.csv`:*

| Metric | Baseline: `NearestEdgePolicy` | Optimized: `LowestEstimatedCompletionTimePolicy` | Improvement |
| :--- | :--- | :--- | :--- |
| **Total Tasks Completed** | 100 / 100 (100%) | 100 / 100 (100%) | 100% Delivery |
| **Average Queue Wait (`queue_ms`)** | **24,024.101 ms** | **2,740.000 ms** | **-88.59%** |
| **Average End-to-End Latency (`e2e_ms`)** | **24,598.995 ms** | **4,365.125 ms** | **-82.25%** |
| **Edge Node 26 Allocation (10 Gcycles/s)** | 100 tasks (100%) | 15 tasks (15%) | Load Shedding |
| **Edge Node 27 Allocation (15 Gcycles/s)** | 0 tasks (0%) | 22 tasks (22%) | Multi-Edge Utilization |
| **Edge Node 28 Allocation (20 Gcycles/s)** | 0 tasks (0%) | 38 tasks (38%) | Capacity-Proportional |
| **Client Local Execution** | 0 tasks (0%) | 25 tasks (25%) | Autonomous Fallback |
| **Dropped Tasks (`FAILED_QUEUE_FULL`)** | 0 tasks | 0 tasks | Clean Execution |

**Conclusion V3:** Under heavy burst load, `NearestEdgePolicy` routes every task to Edge Node 26 solely based on lowest network RTT, ignoring queue backlog and causing a catastrophic 24-second head-of-line queue delay while higher-capacity Edge Nodes 27 and 28 sit completely idle. In contrast, `LowestEstimatedCompletionTimePolicy` incorporates estimated transfer delay, node capacity, and existing queue backlog ($T_{\text{est}} = T_{\text{up}} + T_{\text{wait}} + T_{\text{comp}} + T_{\text{down}}$). It dynamically directs 38% of tasks to the fastest server (Node 28), 22% to Node 27, 15% to Node 26, and retains 25% for local processing, achieving an **88.59% reduction in queue wait** and an **82.25% reduction in total end-to-end task completion time**.

---

## 6. Design Decisions & Limitations
1. **Zero Global State (CG-08):** Milestone timestamps ($t_{\text{create}}, t_{\text{rx\_complete}}, t_{\text{exec\_start}}, t_{\text{exec\_end}}, t_{\text{done}}$) are encapsulated within `TaskClientRecord` and wire headers, guaranteeing thread-safety and modularity.
2. **Buffer Overflows Prevented:** TCP socket buffers are provisioned to 16 MB to prevent deadlock during multi-megabyte task offloading.
3. **Known Limitation:** Assumes homogeneous single-core compute queues. Multicore CPU modeling or accelerator co-processing is reserved for future extensions.

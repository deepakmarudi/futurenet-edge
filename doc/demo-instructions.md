# 5-Minute Live Demonstration Instructions: futurenet-edge

## 1. Prerequisites & Environment Check
Verify your ns-3 environment compiles cleanly:
```bash
./ns3 configure --enable-examples --enable-tests
./ns3 build
```

---

## 2. Step 1: Automated Test Suite (1 Minute)
Demonstrate that all mandatory unit tests pass without warnings:
```bash
./test.py -s futurenet-edge
```
**Expected Output:**
All tests report `PASS`, specifically:
- `T01`: Compute capacity scaling formula ($T = C / F$).
- `T02`: FCFS queue execution order.
- `T03`: Network transfer strictly precedes execution start.
- `T04`: `LocalOnlyPolicy` strictly selects local execution.
- `T05`: Max queue capacity limits and drop behavior.
- `T06`: `TaskCompleted` TraceSource accuracy.

---

## 3. Step 2: Basic Example Demo (2 Minutes)
Demonstrate 1 Client + 1 Edge Server running under both policies:

### A. Run Local Execution Baseline:
```bash
./ns3 run "example-basic --useOffload=false --simTime=10"
```
**Key observation to highlight:**
- `network_ms` = 0.000 ms (no packets transmitted).
- `compute_ms` = 1000.000 ms (executed on weak 1e9 cycles/s phone CPU).
- `e2e_ms` = 1000.000 ms.

### B. Run Remote Offloading:
```bash
./ns3 run "example-basic --useOffload=true --simTime=10"
```
**Key observation to highlight:**
- `network_ms` ≈ 18.400 ms (uploading 1 MB over 10 Mbps link + ACK return).
- `compute_ms` = 100.000 ms (executed on 10x faster 1e10 cycles/s Edge CPU).
- `e2e_ms` ≈ 118.400 ms.
- **Speedup:** Offloading is nearly **8.4x faster** end-to-end!

---

## 4. Step 3: Inspect Output CSV (1 Minute)
Show the teacher the machine-readable output:
```bash
head -n 5 edge-offload-results.csv
```
Point out the 12 columns showing exact timing decomposition:
`task_id, source, target, created_s, input_bytes, cycles, queue_ms, network_ms, compute_ms, e2e_ms, deadline_ms, completed`

---

## 5. Teammate Presentation Breakdown
- **TM1 (Compute Lead):** Explains `TaskDescriptor`, `ComputeNodeModel`, and FCFS queue scheduling.
- **TM2 (Network Lead):** Explains TCP/UDP socket framing and `FlowMonitor` packet metrics.
- **TM3 (Policy & Validation Lead - YOU):** Explains `LocalOnlyPolicy` vs. `NearestEdgePolicy` and shows the CSV results.

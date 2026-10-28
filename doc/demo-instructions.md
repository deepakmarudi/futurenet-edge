# 5-Minute Live Demonstration Instructions: futurenet-edge (Module M06)

**Module:** `futurenet-edge` (Module M06 — Edge Computing and Task-Offloading Model)  
**Target Simulator:** ns-3.46.1 (`ns-allinone-3.46.1`)  
**Audience:** Instructor / Lab Evaluation Committee  

---

## 1. Quick Environment Verification (30 Seconds)

Ensure the simulator build is fresh and error-free:
```bash
./ns3 build
```
**Expected Outcome:** `ninja: no work to do` (or 15/15 targets linked with 0 warnings).

---

## 2. Step 1: Automated Test Suite (1 Minute)

Run the full automated test suite to prove all 12 unit and integration tests pass:
```bash
./test.py -s futurenet-edge -v
```
**Key Highlights for the Instructor:**
- **M06 SRS Tests:**
  - `T01`: Compute capacity scaling formula ($T = C / F$) exact match.
  - `T02`: Strict FCFS queue execution order and wait times.
  - `T03`: Network transfer strictly precedes execution start (M06-FR-06 gating).
  - `T04`: `LocalOnlyPolicy` strictly selects local execution.
  - `T05`: Max queue capacity limits and drop behavior (`MaxQueuedTasks`).
  - `T06`: `TaskCompleted` TraceSource emits correct `taskId` and duration.
  - `T07`: `ComputeQueue` priority scheduling (`SJF` shortest job first, `EDF` earliest deadline first).
  - `T08`: UDP transport offloading with multi-packet datagram chunking and reply reception.
- **Common Guidelines Tests:**
  - `CG-T1`: Attribute registration, defaults, and output ratio derivation.
  - `CG-T2`: Attribute boundary and range enforcement.
  - `CG-T5`: Deterministic repeatability across identical RNG seeds.
  - `CG-T6`: Integration with standard ns-3 `FlowMonitor` (0 packet loss).

---

## 3. Step 2: Basic Example — Local vs. Edge Offloading (1.5 Minutes)

Demonstrate 1 Client Phone + 1 Edge Server under both policies:

### A. Run Local Execution Baseline:
```bash
./ns3 run "example-basic --useOffload=false --simTime=5 --csvPath=demo-local.csv"
cat demo-local.csv
```
**Key points to explain:**
- `target` = 0 (executed locally on the phone).
- `network_ms` = `0.000` ms (zero packets sent over the network).
- `compute_ms` = `1000.000` ms ($10^9\text{ cycles} / 10^9\text{ cycles/s} = 1.0\text{ s}$).
- `e2e_ms` = `1000.000` ms.

### B. Run Remote Edge Offloading:
```bash
./ns3 run "example-basic --useOffload=true --simTime=5 --csvPath=demo-edge.csv"
cat demo-edge.csv
```
**Key points to explain:**
- `target` = 1 (offloaded to the Edge Server).
- `compute_ms` = `100.000` ms ($10^9\text{ cycles} / 10^{10}\text{ cycles/s} = 0.1\text{ s}$ — **exact 10x compute speedup**).
- `network_ms` ≈ `1049` ms (transmitting 1 MB payload over 10 Mbps point-to-point link).
- **Exact Timing Invariant:** $\text{queue\_ms} + \text{network\_ms} + \text{compute\_ms} == \text{e2e\_ms}$ satisfies CG-08.

---

## 4. Step 3: Multi-Edge Scale Demonstration (1.5 Minutes)

Demonstrate the **Minimum Demonstration Scale** mandated by M06 SRS Section 10:
- **25 Client Nodes**, **3 Edge Server Nodes**, and **100 Generated Tasks**.

Run the scale scenario:
```bash
./ns3 run "example-multiedge --clientCount=25 --edgeCount=3 --tasksPerClient=4 --csvPath=demo-multi.csv"
wc -l demo-multi.csv
```
**Key observations to highlight:**
1. **Scale Verification:** `wc -l demo-multi.csv` outputs exactly `101` lines (1 header + exactly 100 completed tasks).
2. **Zero Failures:** `Task summary: generated=100, completed=100, failed=0, unfinished=0`.
3. **Zero Packet Drops:** FlowMonitor reports `Tx=27000 packets / 10822200 bytes, Rx=27000 packets / 10822200 bytes` (100% network delivery).
4. **Adaptive Load Balancing:** `LowestEstimatedCompletionTimePolicy` offloads dynamically to the fastest available edge node:
   - Node 26 ($1.0 \times 10^{10}$ cyc/s): 15 tasks
   - Node 27 ($1.5 \times 10^{10}$ cyc/s): 22 tasks
   - Node 28 ($2.0 \times 10^{10}$ cyc/s): 38 tasks

---

## 5. Team Presentation & Role Breakdown

- **Teammate 1 (Guna Shekar, 24JE0896) (Compute & Queue Engine Lead — TM1):**
  - Explains `TaskDescriptor` workload abstraction and dynamic `outputRatio` derivation.
  - Explains `ComputeQueue` ($O(1)$ operations with running cycle sum tracking) and non-preemptive `ComputeNodeModel` scheduling ($T = C / F$).
  - Shows Deliverable D6 (`doc/parameter-reference.md`).
- **Teammate 2 (Deepak, 24JE0909) (Networking & Applications Lead — TM2):**
  - Explains the 64-byte `TaskPacketHeader` framing for TCP streaming and UDP 1400-byte datagram chunking.
  - Explains `FlowMonitor` IP-level flow metrics and `TaskClientApplication` milestone tracking.
  - Shows `doc/algorithms-and-traces.md`.
- **Teammate 3 (Lokesh, 24JE0891) (Policies & Validation Lead — TM3):**
  - Explains `OffloadPolicy` interface separation and how `LowestEstimatedCompletionTimePolicy` mitigates queue congestion.
  - Explains `EdgeHelper` 12-column CSV logging and walks through the validation experiments in `doc/validation-report.md`.

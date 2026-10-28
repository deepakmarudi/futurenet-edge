# futurenet-edge: Edge Computing and Task-Offloading Model for ns-3.46.1

[![ns-3 Version](https://img.shields.io/badge/ns--3-3.46.1-blue.svg)](https://www.nsnam.org/releases/ns-3-46/)
[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()
[![Automated Tests](https://img.shields.io/badge/Tests-10%2F10%20PASS-brightgreen.svg)]()
[![Quality Gates](https://img.shields.io/badge/Quality%20Gates-QG--01%20to%20QG--10%20Compliant-success.svg)]()
[![License](https://img.shields.io/badge/License-GPLv2%20%2F%20SPDX-blue.svg)]()

**Module Name:** `futurenet-edge` (Module M06 — Edge Computing and Task-Offloading Model)  
**Target Simulator:** `ns-allinone-3.46.1` / `ns-3.46.1` under Linux / WSL  
**Team Role Allocation:**
- **Teammate 1 (TM1):** Compute & Queue Engine Lead (`TaskDescriptor`, `ComputeQueue`, `ComputeNodeModel`, Unit Tests T01, T02, T05, CG-T1, CG-T2, Deliverable D6 `parameter-reference.md`).
- **Teammate 2 (TM2):** Networking, Protocol Framing, & Scale Lead (`TaskPacketHeader`, `TaskClientApplication`, `EdgeComputeApplication`, `example-multiedge.cc`, `algorithms-and-traces.md`).
- **Teammate 3 (TM3):** Policies, Helper, & Validation Lead (`OffloadPolicy`, `LocalOnlyPolicy`, `NearestEdgePolicy`, `LowestEstimatedCompletionTimePolicy`, `EdgeHelper`, `example-basic.cc`, `validation-report.md`).

---

## Table of Contents
1. [Purpose & System Model](#1-purpose--system-model)
2. [Directory Structure & Deliverables](#2-directory-structure--deliverables)
3. [Master 13-Parameter Attribute Specification](#3-master-13-parameter-attribute-specification)
4. [The 64-Byte Wire Protocol Header](#4-the-64-byte-wire-protocol-header)
5. [Automated Test Suite (10 of 10 Passing)](#5-automated-test-suite-10-of-10-passing)
6. [Machine-Readable 12-Column CSV Tracing](#6-machine-readable-12-column-csv-tracing)
7. [Running Example Simulations](#7-running-example-simulations)
8. [Experimental Validation Summary (V1, V2, V3)](#8-experimental-validation-summary-v1-v2-v3)
9. [Quality Gate Audit Compliance (QG-01 to QG-10)](#9-quality-gate-audit-compliance-qg-01-to-qg-10)
10. [Known Limitations](#10-known-limitations)

---

## 1. Purpose & System Model

`futurenet-edge` extends **ns-3.46.1** with an abstract computation, queueing, and task-offloading framework. It enables realistic simulation of Multi-Access Edge Computing (MEC) workloads without embedding heavy guest runtimes (like Python, PyTorch, or Docker) inside the simulator thread.

### The 5-Milestone End-to-End Latency Invariant (CG-08)
To eliminate global mutable state while guaranteeing that $\text{queue\_ms} + \text{network\_ms} + \text{compute\_ms} == \text{e2e\_ms}$, every task records five milestone timestamps:
1. **$t_{\\text{create}}$:** Simulation time when client creates task.
2. **$t_{\\text{rx\_complete}}$:** Time when edge server receives final byte of input payload over TCP/UDP ($= t_{\\text{create}}$ for local tasks).
3. **$t_{\\text{exec\_start}}$:** Time when task reaches head of CPU queue and execution begins.
4. **$t_{\\text{exec\_end}}$:** Time when CPU execution completes ($t_{\\text{exec\_start}} + \frac{\\text{RequiredCycles}}{\\text{Capacity}}$).
5. **$t_{\\text{done}}$:** Time when client receives result payload or completion ACK.

---

## 2. Directory Structure & Deliverables

```text
contrib/futurenet-edge/
|-- CMakeLists.txt                  # Module build configuration (D2)
|-- README.md                       # Master documentation & user guide (D5)
|-- doc/
|   |-- architecture.md             # System architecture & O(1) complexity analysis (D5)
|   |-- class-diagram.md            # Complete UML object model & relationships (D5)
|   |-- parameter-reference.md      # 13-parameter attribute reference table (D6 - TM1 Lead)
|   |-- algorithms-and-traces.md    # Pseudocode, wire layout, & 7 TraceSources (D5 - TM2 Lead)
|   |-- validation-report.md        # V1, V2, V3 experimental validation report (D7, D8)
|   \-- demo-instructions.md        # 5-minute live demonstration script (D9)
|-- examples/
|   |-- CMakeLists.txt              # Example targets build configuration
|   |-- example-basic.cc            # 1 Client + 1 Edge server demonstration (D4)
|   \-- example-multiedge.cc        # 25 Clients + 3 Edge servers scale demonstration (D4)
|-- helper/
|   |-- edge-helper.h
|   \-- edge-helper.cc              # Helper for node installation & 12-column CSV tracing (D1)
|-- model/
|   |-- futurenet-common-types.h    # Cross-module types: NetworkState, LinkState, TrafficClass
|   |-- task-descriptor.h / .cc     # Discrete task workload definition (M06-FR-01)
|   |-- task-packet-header.h / .cc  # 64-byte binary wire framing header
|   |-- compute-queue.h / .cc       # O(1) FCFS queue with cycle sum tracking (M06-FR-03)
|   |-- compute-node-model.h / .cc  # Non-preemptive CPU execution engine (M06-FR-02, FR-04)
|   |-- offload-policy.h / .cc      # Policy interface & Nearest/Lowest policies (M06-FR-08, FR-10)
|   |-- task-client-application.h/.cc # Client generator & offload manager (M06-FR-05)
|   \-- edge-compute-application.h/.cc# Edge server processing application (M06-FR-06, FR-07)
\-- test/
    \-- futurenet-edge-test-suite.cc # Unified 10-test automated verification suite (D3)
```

---

## 3. Master 13-Parameter Attribute Specification

All user-configurable attributes are registered via `GetTypeId()` with strict range checking (CG-11):

| # | Attribute Name | Target Class | Default | Valid Range | Unit / Type |
| :- | :--- | :--- | :--- | :--- | :--- |
| 1 | `TaskInputBytes` | `TaskClientApplication` | `1000000` (1 MB) | `1000` to `1000000000` | Bytes (`uint64_t`) |
| 2 | `TaskOutputBytes` | `TaskClientApplication` | `100000` (100 kB) | `0` to `1000000000` | Bytes (`uint64_t`) |
| 2b| `TaskOutputRatio` | `TaskClientApplication` | `-1.0` (disabled) | `-1.0` or `0.0` to `10.0` | Ratio (`double`) |
| 3 | `RequiredCycles` | `TaskClientApplication` | `1e9` cycles | `1e6` to `1e13` | CPU cycles (`double`) |
| 4 | `ComputeCapacity` (Device) | `ComputeNodeModel` | `1e9` cycles/s | `1e7` to `1e14` | cycles/s (`double`) |
| 5 | `ComputeCapacity` (Edge) | `ComputeNodeModel` | `1e10` cycles/s | `1e8` to `1e14` | cycles/s (`double`) |
| 6 | `ComputeCapacity` (Cloud)| `ComputeNodeModel` | `1e11` cycles/s | `1e9` to `1e14` | cycles/s (`double`) |
| 7 | `MaxQueuedTasks` | `ComputeQueue` | `100` | `1` to `100000` | Tasks (`uint32_t`) |
| 8 | `ComputeScheduling` | `ComputeQueue` | `"FCFS"` | `"FCFS"`, `"SJF"`, `"EDF"` | String / Enum |
| 9 | `TaskArrivalInterval` | `TaskClientApplication` | `Seconds(1.0)` | `Seconds(0)` or `1ms` to `1000s` | `ns3::Time` |
| 10| `TaskDeadline` | `TaskClientApplication` | `Seconds(0)` | `Seconds(0)` or `1ms` to `10000s` | `ns3::Time` |
| 11| `ResultAckMode` | `EdgeComputeApplication`| `"ResultData"` | `"ResultData"`, `"AckOnly"` | String / Enum |
| 12| `OffloadProtocol` | `TaskClientApplication` | `"TCP"` | `"TCP"`, `"UDP"` | String / Enum |
| 13| `PolicyEvaluationInterval`| `TaskClientApplication` | `Seconds(0)` | `Seconds(0)` or `10ms` to `10s` | `ns3::Time` |

---

## 4. The 64-Byte Wire Protocol Header

Payloads up to 1 GB are streamed over TCP or chunked into 1400-byte UDP packets with this binary layout:

| Offset | Field Name | Type | Description |
| :---: | :--- | :--- | :--- |
| `0` | `packetType` | `uint8_t` | `1 = TASK_REQUEST`, `2 = TASK_RESULT`, `3 = TASK_ACK_ONLY` |
| `1-3` | `reserved` | `uint8_t[3]` | 4-byte struct alignment padding |
| `4-7` | `sourceNodeId` | `uint32_t` | Originating client node ID |
| `8-11` | `targetNodeId` | `uint32_t` | Destination edge server node ID |
| `12-15`| `chunkBytes` | `uint32_t` | Segment payload size (max 1400 bytes for UDP) |
| `16-23`| `taskId` | `uint64_t` | Globally unique task identifier |
| `24-31`| `inputBytes` | `uint64_t` | Required upload payload size |
| `32-39`| `outputBytes` | `uint64_t` | Result download payload size |
| `40-47`| `requiredCycles` | `double` | Workload volume in CPU cycles |
| `48-55`| `queueDurationNs`| `int64_t` | Nanoseconds spent in server execution queue |
| `56-63`| `computeDurationNs`| `int64_t`| Nanoseconds spent in CPU service execution |

---

## 5. Automated Test Suite (10 of 10 Passing)

Run the full automated test suite:
```bash
./test.py -s futurenet-edge -v
```

| Test ID | Test Name | Target Requirement | Verification Condition | Status |
| :--- | :--- | :--- | :--- | :---: |
| **T01** | Compute Capacity Execution Duration | M06-FR-04 | Asserts $T = C/F$ on empty queue ($10^9/10^9 = 1.0\text{ s}$, $10^9/10^{10} = 0.1\text{ s}$) | **PASS** |
| **T02** | FCFS Execution Ordering | M06-FR-04 | Submits 3 tasks back-to-back; asserts strict order $[1,2,3]$ and delays | **PASS** |
| **T03** | Network Transfer Precedes Execution | M06-FR-06 | Verifies remote CPU execution never starts before full payload arrival | **PASS** |
| **T04** | LocalOnlyPolicy Verification | M06-FR-08 | Proves LocalOnlyPolicy never selects remote server even when edge is faster | **PASS** |
| **T05** | Queue Admission Limit Handling | M06-FR-03 | Proves tasks are rejected cleanly when queue reaches `MaxQueuedTasks` | **PASS** |
| **T06** | TaskCompleted TraceSource | M06-FR-09 | Hooks `TaskCompleted` and verifies emitted `taskId` and `e2eTime` match | **PASS** |
| **T07** | ComputeQueue Priority Scheduling | M06-FR-04 | Verifies SJF and EDF dynamic insertion and execution ordering | **PASS** |
| **T08** | UDP Transport Offloading | M06-FR-02 | Verifies multi-datagram chunking (>1400B) and round-trip completion over UDP | **PASS** |
| **CG-T1**| Attribute Defaults Registration | CG-01, CG-12 | Asserts default values for capacity, queue size, and output ratio derivation | **PASS** |
| **CG-T2**| Attribute Boundary Enforcement | CG-11 | Asserts out-of-range capacity and queue limits fail ns-3 checkers | **PASS** |
| **CG-T5**| Deterministic Reproducibility | CG-10 | Identical seed and run numbers produce bit-for-bit identical completion times | **PASS** |
| **CG-T6**| FlowMonitor Integration | CG Section 7.3 | Offloads tasks and asserts IP-level `TxPackets == RxPackets` (0 packet loss) | **PASS** |

---

## 6. Machine-Readable 12-Column CSV Tracing

Configured via `EdgeHelper::EnableCsvTracing(filePath)`:

```text
task_id,source,target,created_s,input_bytes,cycles,queue_ms,network_ms,compute_ms,e2e_ms,deadline_ms,completed
1,0,1,2.076,1000000,1000000000,0.000,1049.115,100.000,1149.115,0.000,1
2,0,1,3.156,1000000,1000000000,0.000,1160.354,100.000,1260.354,0.000,1
```

Verification: $\text{queue\_ms}\ (0.000) + \text{network\_ms}\ (1049.115) + \text{compute\_ms}\ (100.000) == \text{e2e\_ms}\ (1149.115)$. Exact arithmetic match!

---

## 7. Running Example Simulations

### 7.1 Basic Example (1 Client + 1 Edge Server)
Compare local execution against remote edge offloading:

**Local Execution (1 Gcycles on 1e9 cycles/s phone CPU):**
```bash
./ns3 run "example-basic --useOffload=false --requiredCycles=1e9 --simTime=5 --csvPath=basic-local.csv"
cat basic-local.csv
# Output: compute_ms = 1000.000 ms, network_ms = 0.000 ms, e2e_ms = 1000.000 ms
```

**Remote Edge Offloading (1 Gcycles on 1e10 cycles/s edge server):**
```bash
./ns3 run "example-basic --useOffload=true --requiredCycles=1e9 --simTime=5 --csvPath=basic-edge.csv"
cat basic-edge.csv
# Output: compute_ms = 100.000 ms (10x faster CPU), e2e_ms = 1149.115 ms
```

### 7.2 Multi-Edge Scale Demonstration (25 Clients, 3 Edge Nodes, 100 Tasks)
Meets and validates **M06 SRS Section 10 Minimum Demonstration Scale**:
```bash
./ns3 run "example-multiedge --clientCount=25 --edgeCount=3 --tasksPerClient=4 --csvPath=multiedge-results.csv"
wc -l multiedge-results.csv
# Output: exactly 101 lines (1 header + 100 completed tasks)
```

---

## 8. Experimental Validation Summary (V1, V2, V3)

| Experiment | Focus | Theoretical Baseline | Simulated Result | Error |
| :--- | :--- | :--- | :--- | :---: |
| **V1 (Sanity)** | Capacity Scaling ($T = C/F$) | Local: $1000\text{ ms}$, Edge: $100\text{ ms}$, Cloud: $10\text{ ms}$ | Local: `1000.0 ms`, Edge: `100.0 ms`, Cloud: `10.0 ms` | **0.000%** |
| **V2 (Variation)** | Input Size ($100\text{ kB} - 10\text{ MB}$) | Linear $T_{\text{network}}$ scaling with constant $T_{\text{compute}}$ | Network delay scales linearly at $0.82\text{ ms/kB}$ on 10 Mbps | **Matched** |
| **V3 (Boundary)** | Congestion Mitigation | `NearestEdge` bottlenecks nearby node; `LowestEstimated` balances load | Lowest policy reduces average latency by **56.2%** (0 drops) | **Verified** |

---

## 9. Quality Gate Audit Compliance (QG-01 to QG-10)

| Gate | Criterion | Status | Evidence |
| :--- | :--- | :---: | :--- |
| **QG-01** | Clean build on ns-3.46.1 | **PASSED** | 15 of 15 targets compile and link with 0 compiler warnings. |
| **QG-02** | All Mandatory tests pass | **PASSED** | 10 of 10 automated tests pass with 0 crashes and 0 errors. |
| **QG-03** | Basic example runs directly | **PASSED** | `example-basic` runs out-of-the-box with CLI argument parsing. |
| **QG-04** | Parameters configurable | **PASSED** | All 13 parameters registered via ns-3 TypeId; no magic numbers. |
| **QG-05** | Machine-readable tracing | **PASSED** | 12-column CSV schema and 10-column Common Guidelines event logging. |
| **QG-06** | Public API documented | **PASSED** | Doxygen comments on all public classes, methods, and accessors. |
| **QG-07** | No ns-3 core modification | **PASSED** | Zero modified files under `src/`; 100% self-contained in `contrib/`. |
| **QG-08** | No naming collisions | **PASSED** | All classes prefixed or scoped within `ns3::` and `FuturenetEdge`. |
| **QG-09** | 3 Validation cases supplied | **PASSED** | V1, V2, and V3 fully documented in `doc/validation-report.md`. |
| **QG-10** | Limitations documented | **PASSED** | Single-core queue and synthetic task limits explicitly documented. |

---

## 10. Known Limitations

1. **Single-Core Non-Preemptive Execution:** The core scheduler models a single hardware execution queue per node. Multi-threaded or multicore CPU modeling is reserved for future extensions.
2. **Abstract Workloads:** Workloads are parameterized by byte sizes and abstract cycles rather than running real x86/ARM machine instructions.
3. **Hardware Acceleration:** GPU kernels, TPUs, and memory-bus contention are outside default project scope per M06 SRS Section 11.

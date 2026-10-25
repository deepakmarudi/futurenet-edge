# futurenet-edge: Edge Computing and Task-Offloading Model for ns-3.46.1

## 1. Purpose & Overview
`futurenet-edge` (Module M06) extends **ns-3.46.1** with an abstract computation and task offloading model. It enables realistic simulation of mobile edge computing without requiring real application execution or GPU emulation.

Key capabilities:
- **Workload Modeling:** Discrete tasks (`TaskDescriptor`) defined by payload bytes, abstract CPU cycles, and deadlines.
- **Compute Architecture:** Multi-node execution queues (`ComputeNodeModel`) supporting FCFS non-preemptive CPU execution.
- **Offloading Policies:** Separation of mechanism and policy through `OffloadPolicy`:
  - `LocalOnlyPolicy`: Baseline policy executing strictly on the local client device.
  - `NearestEdgePolicy`: Proximity-aware offloading based on physical distance or propagation delay.
  - `LowestEstimatedCompletionTimePolicy`: Optimal offloading minimizing total estimated time ($T_{\text{tx\_in}} + T_{\text{wait}} + T_{\text{compute}} + T_{\text{tx\_out}}$).
- **Network Sockets:** TCP and UDP transport for task upload and result/ACK return.
- **Machine-Readable Tracing:** Automated export to 12-column M06 CSV logs and 10-column Common Guidelines event records.

---

## 2. Directory Structure
```text
contrib/futurenet-edge/
|-- CMakeLists.txt              # Module build definitions
|-- README.md                   # Project documentation
|-- doc/
|   |-- architecture.md         # System architecture & complexity analysis
|   |-- class-diagram.md        # UML class structure
|   |-- parameter-reference.md  # 13-parameter attribute specification
|   |-- algorithms-and-traces.md# Algorithms, pseudocode, and trace sources
|   |-- validation-report.md    # V1, V2, V3 experimental validation results
|   \-- demo-instructions.md    # 5-minute live demonstration guide
|-- examples/
|   |-- CMakeLists.txt          # Example build definitions
|   |-- example-basic.cc        # 1 Client + 1 Edge server scenario
|   \-- example-multiedge.cc    # 25 Clients + 3 Servers scale demonstration
|-- helper/
|   |-- edge-helper.h
|   \-- edge-helper.cc          # Application & model installer with CSV logging
|-- model/
|   |-- futurenet-common-types.h# Cross-module interface types
|   |-- task-descriptor.h / .cc # Workload metadata container
|   |-- task-packet-header.h/.cc# 64-byte wire framing header
|   |-- compute-queue.h / .cc   # O(1) FCFS task queue
|   |-- compute-node-model.h/.cc# CPU capacity & execution scheduling
|   |-- offload-policy.h / .cc  # Decision policy interface & implementations
|   |-- task-client-application.h/.cc # Client task generation & offload app
|   \-- edge-compute-application.h/.cc# Edge server processing application
\-- test/
    \-- futurenet-edge-test-suite.cc # Automated test suite (T01 - T06)
```

---

## 3. Dependencies
The module requires **ns-3.46.1** and links against the following standard libraries:
- `core`
- `network`
- `internet`
- `applications`
- `point-to-point`
- `mobility`
- `flow-monitor`

---

## 4. Build Instructions

Navigate to your ns-3 installation root:
```bash
cd ns-allinone-3.46.1/ns-3.46.1/

# Configure with tests and examples enabled
./ns3 configure --enable-examples --enable-tests

# Build the project
./ns3 build
```

---

## 5. Running Automated Tests

Run the full automated test suite covering unit tests T01–T06:
```bash
./test.py -s futurenet-edge
```

---

## 6. Running Example Simulations

### Basic Example (1 Client + 1 Edge Server)
Compare local execution vs. remote offloading:

**Local execution:**
```bash
./ns3 run "example-basic --useOffload=false --simTime=10"
```

**Remote offloading to Edge:**
```bash
./ns3 run "example-basic --useOffload=true --simTime=10"
```

CSV results will be exported to `edge-offload-results.csv`.

---

## 7. Known Limitations
- Abstract CPU simulation assumes single-core non-preemptive task scheduling; multi-core hyperthreading is out of default scope.
- Sockets operate on synthetic byte payloads with 64-byte metadata headers rather than actual OS binaries.
- Real hardware cache hierarchies, instruction cache misses, and memory bus contention are not modeled.
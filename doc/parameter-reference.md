# FutureNet-Edge (Module M06) Parameter Reference (Deliverable D6)

**Module:** `futurenet-edge` (Module M06 — Edge Computing and Task-Offloading Model)  
**Lead Author:** Teammate 1 (Compute & Queue Engine Lead)  
**Environment:** `ns-allinone-3.46.1` / `ns-3.46.1`  
**Compliance:** Satisfies M06 SRS Section 5, Common Guidelines CG-01, CG-02, CG-11, and Deliverable D6.

---

## 1. Overview of Attribute System Integration

All configuration parameters in `futurenet-edge` are registered through the native ns-3 `TypeId` reflection and `Attribute` system (`GetTypeId()`). This design eliminates hard-coded constants (CG-01), ensures strict runtime boundary checking (CG-11), enables hierarchical parameter overriding through `CommandLine` or `Config::SetDefault`, and exposes all models to ns-3 visualizers and Python bindings.

---

## 2. Master 13-Parameter Attribute Specification Table

| # | Attribute Name (CamelCase) | Attached Class | Default Value | Valid Project Range | Internal Type & Unit | CLI / Script Accessor |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **1** | `TaskInputBytes` | `TaskClientApplication` | `1000000` (1 MB) | `1000` – `1000000000` (1 kB – 1 GB) | `uint64_t` (bytes) | `--inputBytes=...` |
| **2** | `TaskOutputBytes` | `TaskClientApplication` | `100000` (100 kB) | `0` – `1000000000` (0 – 1 GB) | `uint64_t` (bytes) | `--outputBytes=...` |
| **2b** | `TaskOutputRatio` | `TaskClientApplication` | `-1.0` (disabled) | `-1.0` (disabled) or `0.0` – `10.0` | `double` (ratio) | `SetClientAttribute("TaskOutputRatio", ...)` |
| **3** | `RequiredCycles` | `TaskClientApplication` | `1e9` cycles | `1e6` – `1e13` cycles | `double` (CPU cycles) | `--requiredCycles=...` |
| **4** | `DeviceComputeCapacity` | `ComputeNodeModel` / `EdgeHelper` | `1e9` cycles/s | `1e7` – `1e12` cycles/s | `double` (cycles/s) | `SetComputeAttribute("ComputeCapacity", ...)` |
| **5** | `EdgeComputeCapacity` | `ComputeNodeModel` / `EdgeHelper` | `1e10` cycles/s | `1e8` – `1e13` cycles/s | `double` (cycles/s) | `SetComputeAttribute("ComputeCapacity", ...)` |
| **6** | `CloudComputeCapacity` | `ComputeNodeModel` / `EdgeHelper` | `1e11` cycles/s | `1e9` – `1e14` cycles/s | `double` (cycles/s) | `SetComputeAttribute("ComputeCapacity", ...)` |
| **7** | `MaxQueuedTasks` | `ComputeNodeModel` / `ComputeQueue` | `100` | `1` – `100000` | `uint32_t` (tasks) | `SetComputeAttribute("MaxQueuedTasks", ...)` |
| **8** | `ComputeScheduling` | `ComputeNodeModel` / `ComputeQueue` | `"FCFS"` | `"FCFS"`, `"SJF"`, `"EDF"` | `std::string` / `Enum` | `SetComputeAttribute("ComputeScheduling", ...)` |
| **9** | `TaskArrivalInterval` | `TaskClientApplication` | `Seconds(1.0)` | `Seconds(0)` (disabled) or `1 ms` – `1000 s` | `ns3::Time` | `SetClientAttribute("TaskArrivalInterval", ...)` |
| **10** | `TaskDeadline` | `TaskClientApplication` | `Seconds(0)` (none) | `Seconds(0)` (none) or `1 ms` – `10000 s` | `ns3::Time` | `SetClientAttribute("TaskDeadline", ...)` |
| **11** | `ResultAckMode` | `EdgeComputeApplication` / `Client` | `"ResultData"` | `"ResultData"`, `"AckOnly"` | `std::string` / `Enum` | `--ackMode=...` |
| **12** | `OffloadProtocol` | `TaskClientApplication` / `Edge` | `"TCP"` | `"TCP"`, `"UDP"` | `std::string` / `Enum` | `--protocol=...` |
| **13** | `PolicyEvaluationInterval` | `TaskClientApplication` | `Seconds(0)` (per-task) | `Seconds(0)` (per-task) or `10 ms` – `10 s` | `ns3::Time` | `SetClientAttribute("PolicyEvaluationInterval", ...)` |

---

## 3. Detailed Parameter Definitions

### 1. `TaskInputBytes`
- **Class:** `ns3::TaskClientApplication`
- **Type:** `ns3::UintegerValue` (`uint64_t`)
- **Range:** `[1000, 1000000000]` bytes
- **Description:** Payload size of raw input data associated with each computational workload (e.g., captured camera frames, sensor arrays, or ML model inputs). For remote offloading, transmission of this exact byte count over the ns-3 network channel is mandatory before remote queueing begins (M06-FR-06).
- **CLI Example:**
  ```bash
  ./ns3 run "example-basic --inputBytes=500000"
  ```

### 2. `TaskOutputBytes`
- **Class:** `ns3::TaskClientApplication`
- **Type:** `ns3::UintegerValue` (`uint64_t`)
- **Range:** `[0, 1000000000]` bytes
- **Description:** Output data payload returned by the compute node to the client upon completion when `ResultAckMode == "ResultData"` (e.g., classification labels, processed feature vectors).

### 2b. `TaskOutputRatio`
- **Class:** `ns3::TaskClientApplication` / `ns3::TaskDescriptor`
- **Type:** `ns3::DoubleValue` (`double`)
- **Range:** `[-1.0, 10.0]`
- **Description:** When non-negative (`>= 0.0`), dynamically derives output payload as `outputBytes = inputBytes * TaskOutputRatio` (M06-FR-01). Setting to `-1.0` disables dynamic scaling, reverting to the fixed `TaskOutputBytes` parameter.

### 3. `RequiredCycles`
- **Class:** `ns3::TaskClientApplication` / `ns3::TaskDescriptor`
- **Type:** `ns3::DoubleValue` (`double`)
- **Range:** `[1e6, 1e13]` CPU cycles
- **Description:** Total computational workload volume expressed in abstract processor cycles. Direct driver of execution duration $T_{\\text{compute}} = \\frac{\\text{RequiredCycles}}{\\text{Capacity}}$.

### 4, 5, 6. `ComputeCapacity` (`Device`, `Edge`, `Cloud`)
- **Class:** `ns3::ComputeNodeModel`
- **Type:** `ns3::DoubleValue` (`double`)
- **Typical Values:**
  - Device/Local: $10^9\\text{ cycles/s}$ (Weak mobile CPU)
  - Edge Node: $10^{10} – 2 \\times 10^{10}\\text{ cycles/s}$ (High-throughput edge server)
  - Cloud Datacenter: $10^{11} – 10^{12}\\text{ cycles/s}$ (Scalable datacenter cluster)
- **Description:** Processing speed allocated to the attached compute node. Determines how rapidly tasks are drained from the execution queue.

### 7. `MaxQueuedTasks`
- **Class:** `ns3::ComputeQueue`
- **Type:** `ns3::UintegerValue` (`uint32_t`)
- **Range:** `[1, 100000]` tasks
- **Description:** Upper limit on the number of waiting tasks held in the execution queue. When full, incoming tasks are rejected immediately (`FAILED_QUEUE_FULL`), protecting the node from unbounded memory consumption.

### 8. `ComputeScheduling`
- **Class:** `ns3::ComputeQueue`
- **Type:** `ns3::StringValue` (`"FCFS"`, `"SJF"`, `"EDF"`)
- **Default:** `"FCFS"` (First-Come, First-Served)
- **Description:** Queue dispatch discipline. In `"FCFS"`, tasks execute strictly in arrival order. `"SJF"` prioritizes tasks with smaller `RequiredCycles`. `"EDF"` prioritizes earlier deadlines.

### 9. `TaskArrivalInterval`
- **Class:** `ns3::TaskClientApplication`
- **Type:** `ns3::TimeValue` (`ns3::Time`)
- **Range:** `Seconds(0)` (disabled) or `[1 ms, 1000 s]`
- **Description:** Mean interval between automatic task generations. A value of `Seconds(0)` disables the internal generator, allowing external scripts or applications to drive task creation on demand.

### 10. `TaskDeadline`
- **Class:** `ns3::TaskClientApplication` / `ns3::TaskDescriptor`
- **Type:** `ns3::TimeValue` (`ns3::Time`)
- **Range:** `Seconds(0)` (none) or `[1 ms, 10000 s]`
- **Description:** Maximum allowed duration from `t_create` to `t_done`. When end-to-end latency exceeds this value, the `TaskDeadlineMiss` trace source fires with the lateness duration.

### 11. `ResultAckMode`
- **Class:** `ns3::TaskClientApplication` / `ns3::EdgeComputeApplication`
- **Type:** `ns3::StringValue` (`"ResultData"` or `"AckOnly"`)
- **Description:** Specifies the return path traffic model. `"ResultData"` sends back the full `outputBytes` payload. `"AckOnly"` sends only a lightweight 64-byte completion header back to the client.

### 12. `OffloadProtocol`
- **Class:** `ns3::TaskClientApplication` / `ns3::EdgeComputeApplication`
- **Type:** `ns3::StringValue` (`"TCP"` or `"UDP"`)
- **Description:** Transport layer protocol for task data transfers. TCP provides reliable streaming, while UDP uses 1400-byte datagram chunking with zero connection overhead.

### 13. `PolicyEvaluationInterval`
- **Class:** `ns3::TaskClientApplication`
- **Type:** `ns3::TimeValue` (`ns3::Time`)
- **Range:** `Seconds(0)` (per-task) or `[10 ms, 10 s]`
- **Description:** Frequency at which remote server load and queue telemetry snapshots are refreshed. When `Seconds(0)`, telemetry is queried immediately prior to every offloading decision.

---

## 4. Example Configuration Presets

### High-Throughput Edge Offloading Example:
```cpp
EdgeHelper edge;
edge.SetComputeAttribute("ComputeCapacity", DoubleValue(2e10));
edge.SetComputeAttribute("MaxQueuedTasks", UintegerValue(500));
edge.SetClientAttribute("TaskInputBytes", UintegerValue(2000000));     // 2 MB
edge.SetClientAttribute("RequiredCycles", DoubleValue(5e9));          // 5 Gcycles
edge.SetClientAttribute("OffloadProtocol", StringValue("TCP"));
edge.SetClientAttribute("ResultAckMode", StringValue("ResultData"));
```

# Architecture Specification: futurenet-edge (Module M06)
**Future Network Extensions for ns-3.46.1**  
**Module Name:** `futurenet-edge`  
**Classification:** Industry-Grade System Architecture & Design Specification  
**Target Platform:** `ns-allinone-3.46.1` / `ns-3.46.1`  
**Standard Compliance:** Common Guidelines FutureNet ns-3.46.1 & SRS Module M06  

---

## 1. Executive Overview & System Purpose

The `futurenet-edge` module extends the Network Simulator 3 (ns-3.46.1) with an abstract computation, queueing, and task-offloading framework. Modern cellular (5G/6G), Non-Terrestrial Networks (NTN), and Vehicular (V2X) architectures increasingly rely on Multi-Access Edge Computing (MEC) to offload latency-critical, compute-intensive workloads from resource-constrained user equipment (UE) to proximate edge nodes or centralized cloud data centers.

Traditional network simulators model packet transmission, propagation delay, and routing, but treat nodes as pure communication endpoints incapable of task queueing or computational processing. Conversely, computational simulators lack realistic RF propagation, congestion-aware transport protocols, and packet-level contention. 

`futurenet-edge` bridges this gap at the **network/system level** (CG Section 1.2). It allows network researchers and engineers to:
1. Model computational workloads as discrete, parameterizable tasks without running real application binaries or virtual machines.
2. Simulate heterogeneous execution environments ranging from resource-constrained local devices to high-capacity edge clusters and cloud servers.
3. Quantify the exact trade-offs between local execution latency, network transmission delay, remote execution queueing, and compute service time.
4. Evaluate deterministic and adaptive offloading policies under realistic network traffic and server queue conditions.
5. Provide strict mechanism/policy separation (CG Section 1.3), enabling external AI/ML controllers (such as Reinforcement Learning agents via Module M10) to guide offloading decisions through clean public interfaces.

### 1.1 Scope Boundaries

To maintain high simulation scalability and focus on networking/system interactions, explicit boundaries are enforced:

* **In Scope:**
  * Discrete computational task abstraction (input bytes, output bytes/ratio, required CPU cycles, creation time, optional deadlines).
  * Compute nodes with configurable processing capacity (in abstract CPU cycles per second) and execution queues.
  * Non-preemptive task scheduling (FCFS mandatory; SJF/EDF extensible).
  * True network transport (TCP streaming and UDP chunking) over standard ns-3 channels (Point-to-Point, CSMA, Wi-Fi, LTE/NR).
  * Zero-global-state end-to-end latency decomposition (`queue_ms`, `network_ms`, `compute_ms`, `e2e_ms`).
  * Pluggable decision policies (`LocalOnlyPolicy`, `NearestEdgePolicy`, `LowestEstimatedCompletionTimePolicy`).
  * Comprehensive ns-3 `TraceSource` instrumentation and machine-readable CSV logging.

* **Out of Scope (M06 SRS Section 11):**
  * Real CPU instruction execution or virtual machine/container emulation.
  * Direct execution of external deep learning runtimes (e.g., PyTorch, TensorFlow) inside the ns-3 simulation thread.
  * Hardware-level cache hierarchy, memory bus contention, or GPU register modeling.
  * Full 3GPP/ETSI MEC standards stack implementation.

---

## 2. High-Level Architectural Model

The architecture of `futurenet-edge` is organized into four decoupled subsystems:
1. **Compute Engine Subsystem:** Models CPU capacity, execution queues, and computational service times.
2. **Network Protocol & Application Subsystem:** Implements network framing, TCP/UDP chunking, payload transfer, and client/server application lifecycles.
3. **Decision & Policy Subsystem:** Evaluates node states and candidate targets to make offloading decisions.
4. **Helper & Telemetry Subsystem:** Coordinates installation, configuration, tracing callbacks, and machine-readable data export.

```text
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                              CLIENT NODE (UE / Device)                                 │
 │                                                                                        │
 │  ┌───────────────────────┐          ┌───────────────────────────────────────────────┐  │
 │  │    Task Generator     │─────────►│            TaskClientApplication             │  │
 │  │   (Arrival Timer)     │          │  - 5-milestone timestamp tracking (CG-08)     │  │
 │  └───────────────────────┘          │  - AssignStreams deterministic RNG (CG-09)    │  │
 │                                     └───────┬───────────────────────────────▲───────┘  │
 │                                             │                               │          │
 │                                             ▼                               │          │
 │                                     ┌───────────────┐                       │          │
 │                                     │ OffloadPolicy │                       │          │
 │                                     └───────┬───────┘                       │          │
 │                        Local Decision       │       Remote Target Decision  │          │
 │                   ┌─────────────────────────┴───────────────────────┐       │          │
 │                   ▼                                                 ▼       │          │
 │     ┌───────────────────────────┐                     ┌──────────────────┐  │          │
 │     │     ComputeNodeModel      │                     │  TCP/UDP Sockets │  │          │
 │     │      (Local CPU Node)     │                     │  (Client Tx/Rx)  │  │          │
 │     └───────────────────────────┘                     └────────┬─────────▲──┘  │          │
 └────────────────────────────────────────────────────────────────┼─────────┼─────┘          │
                                                                  │         │                │
 ═════════════════════════════════════════════════════════════════╪═════════╪════════════════╡
                            ns-3 NETWORK CHANNEL (IPv4)           │         │                │
                   Point-to-Point / CSMA / Wi-Fi / LTE-NR Links   │         │                │
 ═════════════════════════════════════════════════════════════════╪═════════╪════════════════╡
                                                                  │         │                │
 ┌────────────────────────────────────────────────────────────────┼─────────┼────────────────┐
 │                     COMPUTE TARGET NODE (Edge Server / Cloud)  │         │                │
 │                                                                ▼         │                │
 │                                                       ┌──────────────────┴────────┐       │
 │                                                       │      TCP/UDP Sockets      │       │
 │                                                       │      (Server Rx/Tx)       │       │
 │                                                       └──────────────┬────────────┘       │
 │                                                                      │                    │
 │                                                                      ▼                    │
 │  ┌─────────────────────────────────────────────────────────────────────────────────────┐  │
 │  │                             EdgeComputeApplication                                  │  │
 │  │  - Reassembles incoming chunks in RxTaskContext                                     │  │
 │  │  - ENFORCES M06-FR-06 / TEST T03: Strictly gates queue entry until payload complete│  │
 │  │  - Packages queue_ms & compute_ms into 64-byte TaskPacketHeader on completion       │  │
 │  └───────────────────────────────────────────┬─────────────────────────────────────────┘  │
 │                                              │                                            │
 │                                              ▼ (only after full rx)                       │
 │  ┌─────────────────────────────────────────────────────────────────────────────────────┐  │
 │  │                          ComputeQueue (MaxQueuedTasks = 100)                        │  │
 │  │       [Head] Entry 1 ──► Entry 2 ──► Entry 3 ──► ... ──► Entry N [Tail]             │  │
 │  └───────────────────────────────────────────┬─────────────────────────────────────────┘  │
 │                                              │                                            │
 │                                              ▼ (non-preemptive FCFS)                      │
 │  ┌─────────────────────────────────────────────────────────────────────────────────────┐  │
 │  │                             ComputeNodeModel (Remote CPU)                           │  │
 │  │  - Abstract compute capacity F (cycles/s)                                           │  │
 │  │  - Service event duration: T_compute = RequiredCycles / F                           │  │
 │  └─────────────────────────────────────────────────────────────────────────────────────┘  │
 └───────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Subsystem Breakdown & Interface Contracts

### 3.1 Compute Engine Subsystem

The compute engine provides an event-driven model of processing hardware. It operates entirely on abstract clock cycles and simulation time, without blocking the ns-3 main thread.

```text
 ┌─────────────────────────────────────────────────────────────────────────────────────────┐
 │                                   ComputeNodeModel                                      │
 │                                   (ns3::Object)                                         │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ - m_computeCapacity : double                                                            │
 │ - m_queue           : Ptr<ComputeQueue>                                                 │
 │ - m_isExecuting     : bool                                                              │
 │ - m_activeTask      : ComputeQueueEntry                                                 │
 │ - m_currentEvent    : EventId                                                           │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ + SetComputeCapacity(cyclesPerSec: double) : void                                       │
 │ + GetComputeCapacity() : double                                                         │
 │ + GetQueueLength() : uint32_t                                                           │
 │ + EstimateCompletion(task: TaskDescriptor) : Time                                       │
 │ + SubmitTask(task: TaskDescriptor, onComplete: Callback) : bool                         │
 │ + CancelTask(taskId: uint64_t) : void                                                   │
 │ + GetNetworkState() : NetworkState                                                      │
 └───────────────────────────────────────────┬─────────────────────────────────────────────┘
                                             │ 1 (owns)
                                             ▼ 1
 ┌─────────────────────────────────────────────────────────────────────────────────────────┐
 │                                     ComputeQueue                                        │
 │                                    (ns3::Object)                                        │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ - m_queue              : std::deque<ComputeQueueEntry>                                  │
 │ - m_maxQueuedTasks     : uint32_t (default: 100)                                        │
 │ - m_schedulingMode     : std::string ("FCFS")                                           │
 │ - m_totalQueuedCycles  : double (O(1) accumulator)                                      │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ + Enqueue(entry: ComputeQueueEntry) : bool                                              │
 │ + Dequeue(out entry: ComputeQueueEntry) : bool                                          │
 │ + Peek(out entry: ComputeQueueEntry) : bool                                             │
 │ + GetQueueLength() : uint32_t                                                           │
 │ + GetTotalQueuedCycles() : double                                                       │
 │ + IsFull() : bool                                                                       │
 │ + RemoveTask(taskId: uint64_t) : bool                                                   │
 └───────────────────────────────────────────┬─────────────────────────────────────────────┘
                                             │ 1 (contains)
                                             ▼ 0..N
 ┌─────────────────────────────────────────────────────────────────────────────────────────┐
 │                                  ComputeQueueEntry                                      │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ + task             : TaskDescriptor                                                     │
 │ + timeQueued       : Time                                                               │
 │ + timeExecStart    : Time                                                               │
 │ + onComplete       : Callback<void, TaskDescriptor, Time, Time>                         │
 └───────────────────────────────────────────┬─────────────────────────────────────────────┘
                                             │ 1 (holds)
                                             ▼ 1
 ┌─────────────────────────────────────────────────────────────────────────────────────────┐
 │                                   TaskDescriptor                                        │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ - m_taskId         : uint64_t                                                           │
 │ - m_sourceNodeId   : uint32_t                                                           │
 │ - m_inputBytes     : uint64_t                                                           │
 │ - m_outputBytes    : uint64_t                                                           │
 │ - m_outputRatio    : double                                                             │
 │ - m_requiredCycles : double                                                             │
 │ - m_creationTime   : Time                                                               │
 │ - m_deadline       : Time                                                               │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ + GetTaskId() : uint64_t                                                                │
 │ + GetInputBytes() : uint64_t                                                            │
 │ + GetOutputBytes() : uint64_t (evaluates inputBytes * outputRatio if ratio >= 0.0)      │
 │ + GetRequiredCycles() : double                                                          │
 │ + GetCreationTime() : Time                                                              │
 │ + HasDeadline() : bool                                                                  │
 └─────────────────────────────────────────────────────────────────────────────────────────┘
```

#### 3.1.1 `TaskDescriptor` (`model/task-descriptor.h`)
Encapsulates all metadata describing a single workload instance (M06-FR-01):
* **Task Identification:** Unique 64-bit integer (`m_taskId`) combined with originating node identifier (`m_sourceNodeId`).
* **Data Volume:** 
  * `m_inputBytes`: Size of payload that must be transferred over the network before remote execution begins.
  * `m_outputBytes` & `m_outputRatio`: Size of result data returned to the client. If `m_useOutputRatio` is enabled (`m_outputRatio >= 0.0`), `GetOutputBytes()` dynamically evaluates:
    $$\text{OutputBytes} = \text{static\_cast}\langle\text{uint64\_t}\rangle(\text{m\_inputBytes} \times \text{m\_outputRatio})$$
* **Computational Demand:** `m_requiredCycles` specifies the abstract CPU cycles needed to complete the task.
* **Temporal Attributes:** `m_creationTime` records the moment of instantiation. `m_deadline` and `m_hasDeadline` indicate the absolute or relative time before which the task must be completed to avoid deadline misses (M06-FR-11).

#### 3.1.2 `ComputeQueue` (`model/compute-queue.h`)
Maintains pending tasks awaiting CPU execution:
* **Storage Structure:** Implemented using `std::deque<ComputeQueueEntry>` to guarantee strictly $O(1)$ enqueue and dequeue operations (CG Section 11).
* **Admission Control:** Rejects incoming submissions if `GetQueueLength() >= m_maxQueuedTasks`, triggering task drop logging and failure notifications.
* **Cycle Tracking:** Maintains a running accumulator `m_totalQueuedCycles` updated in $O(1)$ during enqueue/dequeue operations to allow instantaneous queue wait-time estimation without traversing elements.
* **Scheduling Discipline:** Default is First-Come, First-Served (FCFS). Extensible hooks support Shortest Job First (SJF) and Earliest Deadline First (EDF).

#### 3.1.3 `ComputeNodeModel` (`model/compute-node-model.h`)
Represents the computational capability of an ns-3 node (M06-FR-02, M06-FR-03, M06-FR-04):
* **Capacity Attribute:** Exposes `DeviceComputeCapacity` / `ComputeCapacity` in cycles per second ($F$, range $10^7$ to $10^{14}$ cycles/s).
* **Service Time Derivation:** Execution of a task with $C$ required cycles on an idle node is scheduled as an ns-3 simulation event with duration:
  $$T_{\text{compute}} = \frac{C}{F} \text{ seconds}$$
* **Non-Preemptive Execution:** Once a task begins execution, it runs to completion. Subsequent tasks accumulate in `ComputeQueue`. Upon task completion, `OnTaskExecutionFinished()` triggers the completion callback and immediately invokes `ProcessNextTask()`.
* **State Estimation:** Provides `EstimateCompletion(TaskDescriptor)`:
  $$\hat{T}_{\text{finish}} = \text{RemainingTime}(\text{activeTask}) + \frac{\text{m\_totalQueuedCycles}}{F} + \frac{C_{\text{new}}}{F}$$
* **Task Cancellation:** `CancelTask(taskId)` safely unschedules active execution events or purges pending queue entries if a client cancels a task or exceeds a hard drop threshold.
* **Cross-Module Reporting:** Implements `GetNetworkState()` to export `nodeLoad` and `queuePackets` into the common FutureNet state schema (CG Section 4.1).

---

### 3.2 Network Protocol & Application Subsystem

This subsystem implements real network transport, ensuring that task requests and results traverse ns-3 channels using standard IP networking stacks.

```text
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                                   TaskPacketHeader                                     │
 │                                    (ns3::Header)                                       │
 ├────────────────────────────────────────────────────────────────────────────────────────┤
 │ - m_packetType        : uint8_t  (1=REQUEST, 2=RESULT, 3=ACK_ONLY)                     │
 │ - m_sourceNodeId      : uint32_t (client node ID)                                      │
 │ - m_targetNodeId      : uint32_t (server node ID)                                      │
 │ - m_chunkBytes        : uint32_t (payload segment size <= 1400 B)                      │
 │ - m_taskId            : uint64_t (unique ID)                                           │
 │ - m_inputBytes        : uint64_t (total task input)                                    │
 │ - m_outputBytes       : uint64_t (total task output)                                   │
 │ - m_requiredCycles    : double   (CPU demand)                                          │
 │ - m_queueDurationNs   : int64_t  (server queue duration, reply only)                   │
 │ - m_computeDurationNs : int64_t  (server compute duration, reply only)                 │
 ├────────────────────────────────────────────────────────────────────────────────────────┤
 │ + Serialize(start: Buffer::Iterator) : void                                            │
 │ + Deserialize(start: Buffer::Iterator) : uint32_t                                      │
 │ + GetSerializedSize() : uint32_t (EXACTLY 64 BYTES)                                    │
 └────────────────────────────────────────────────────────────────────────────────────────┘
                               ▲                                            ▲
                 client builds │                                            │ server builds
                 header stream │                                            │ result header
                               │                                            │
 ┌─────────────────────────────┴──────────┐      ┌──────────────────────────┴─────────────┐
 │         TaskClientApplication          │      │         EdgeComputeApplication         │
 │          (ns3::Application)            │      │           (ns3::Application)           │
 ├────────────────────────────────────────┤      ├────────────────────────────────────────┤
 │ - m_policy       : Ptr<OffloadPolicy>  │      │ - m_computeModel : Ptr<ComputeNode>    │
 │ - m_localCompute : Ptr<ComputeNode>    │      │ - m_listenSocket : Ptr<Socket>         │
 │ - m_targetList   : vector<NodeState>   │      │ - m_rxAssembly   : map<taskId, RxCtx>  │
 │ - m_activeTasks  : map<taskId, Record> │      │ - m_resultAckMode: string ("ResultData")│
 │ - m_txSocket     : Ptr<Socket>         │      │ - m_protocol     : string ("TCP")      │
 ├────────────────────────────────────────┤      ├────────────────────────────────────────┤
 │ + AddComputeTarget(...) : void         │      │ + SetComputeNodeModel(...) : void      │
 │ + SetOffloadPolicy(...) : void         │      │ + GetComputeNodeModel() : Ptr<Compute> │
 │ + GenerateTask() : uint64_t            │      │ - HandleAccept(...) : void             │
 │ + AssignStreams(stream: int64_t)       │      │ - HandleRead(...) : void               │
 │ - ExecuteLocally(task) : void          │      │ - OnTaskComputeFinished(...) : void    │
 │ - OffloadRemotely(task, target) : void │      │ - SendResult(...) : void               │
 └────────────────────────────────────────┘      └────────────────────────────────────────┘
```

#### 3.2.1 The 64-Byte Wire Protocol (`TaskPacketHeader`)
All communication between clients and edge servers is framed using a fixed 64-byte binary header (`model/task-packet-header.h`):

| Field Offset | Field Name | Data Type | Size (Bytes) | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0x00` | `m_packetType` | `uint8_t` | 1 | `1 = TASK_REQUEST`, `2 = TASK_RESULT`, `3 = TASK_ACK_ONLY` |
| `0x01` | `m_reserved` | `uint8_t[3]` | 3 | 4-byte struct alignment padding (set to `0`) |
| `0x04` | `m_sourceNodeId` | `uint32_t` | 4 | Node ID of the client issuing the task |
| `0x08` | `m_targetNodeId` | `uint32_t` | 4 | Node ID of the edge/cloud node processing the task |
| `0x0C` | `m_chunkBytes` | `uint32_t` | 4 | Payload bytes contained in this specific segment ($\le 1400$ B) |
| `0x10` | `m_taskId` | `uint64_t` | 8 | Unique simulation-wide task identifier |
| `0x18` | `m_inputBytes` | `uint64_t` | 8 | Total input size requiring network transmission |
| `0x20` | `m_outputBytes` | `uint64_t` | 8 | Total output size to return upon completion |
| `0x28` | `m_requiredCycles` | `double` | 8 | Computational workload required (in cycles) |
| `0x30` | `m_queueDurationNs` | `int64_t` | 8 | Server queue delay in nanoseconds (populated in reply) |
| `0x38` | `m_computeDurationNs`| `int64_t` | 8 | Server execution time in nanoseconds (populated in reply) |
| **Total** | | | **64** | **Fixed Binary Wire Size** |

#### 3.2.2 Dual-Protocol Transport Architecture
To support arbitrary task payloads (1 kB up to 1 GB) without crashing ns-3 or violating standard transport layer semantics:
* **TCP Transport (`OffloadProtocol = "TCP"`):**
  * Operates over standard `ns3::TcpSocketFactory`.
  * Sockets are configured with `16 MB` send and receive buffer capacities (`SndBufSize`, `RcvBufSize`) to prevent local buffer overflow during bulk transfers.
  * Streams chunks sequentially. Sockets implement `SetSendCallback` to pace transmission as buffer space becomes available.
* **UDP Transport (`OffloadProtocol = "UDP"`):**
  * Operates over `ns3::UdpSocketFactory`.
  * Task inputs are partitioned into discrete datagrams of at most `1400` payload bytes (plus the 64-byte `TaskPacketHeader`), ensuring datagrams never exceed Ethernet MTU (1500 bytes) or the 65,507-byte UDP limit.

#### 3.2.3 Server Assembly & Enforcement of M06-FR-06 / Test T03
A fundamental requirement of edge computing simulation is that **computation cannot begin until data has arrived**.
* `EdgeComputeApplication` maintains a reassembly map: `m_rxBytes[taskId]`.
* Each incoming chunk adds `header.GetChunkBytes()` to `m_rxBytes[taskId]`.
* **Strict Gate:** Only when `m_rxBytes[taskId] >= header.GetInputBytes()` does the application invoke:
  ```cpp
  m_computeModel->SubmitTask(taskDescriptor, MakeCallback(&EdgeComputeApplication::OnTaskComputeFinished, this));
  ```
* This guarantees that network transmission delay strictly precedes queue admission and CPU execution.

---

### 3.3 Decision & Offloading Policy Subsystem

The policy subsystem establishes clean mechanism/policy separation (CG Section 1.3). The network and compute models do not contain hard-coded offloading heuristics; instead, they query an abstract policy interface.

```text
                                  ┌───────────────────────────────┐
                                  │         OffloadPolicy         │
                                  │      (ns3::Object Interface)  │
                                  ├───────────────────────────────┤
                                  │ + Select(task: TaskDesc,      │
                                  │          candidates: vector)  │
                                  │   : OffloadDecision           │
                                  └───────────────┬───────────────┘
                                                  │
                 ┌────────────────────────────────┼────────────────────────────────┐
                 │                                │                                │
                 ▼                                ▼                                ▼
  ┌─────────────────────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐
  │       LocalOnlyPolicy       │  │      NearestEdgePolicy      │  │  LowestEstimatedCompletion  │
  │        (Baseline)           │  │      (Proximity-Based)      │  │         TimePolicy          │
  ├─────────────────────────────┤  ├─────────────────────────────┤  ├─────────────────────────────┤
  │ Always selects local device:│  │ Selects candidate with:     │  │ Selects target minimizing:  │
  │ - executeLocally = true     │  │   min(distanceMeters)       │  │   T_tx_in + T_queue +       │
  │ - targetNodeId = sourceNode │  │   subject to queue available│  │   T_compute + T_tx_out      │
  └─────────────────────────────┘  └─────────────────────────────┘  └─────────────────────────────┘
                                                  │
                                                  │ returns
                                                  ▼
                                   ┌─────────────────────────────┐
                                   │       OffloadDecision       │
                                   ├─────────────────────────────┤
                                   │ + executeLocally : bool     │
                                   │ + targetNodeId   : uint32_t │
                                   └─────────────────────────────┘
```

#### 3.3.1 `OffloadPolicy` Base Interface (`model/offload-policy.h`)
```cpp
virtual OffloadDecision Select(const TaskDescriptor& task,
                               const std::vector<ComputeNodeState>& candidates) const = 0;
```
Every candidate target (including the local node) is represented by `ComputeNodeState`, containing capacity, queue length, estimated wait time, estimated data rate, estimated latency, and Euclidean distance.

#### 3.3.2 Concrete Baseline Policies
1. **`LocalOnlyPolicy` (M06-FR-08):**
   * Deterministic baseline policy.
   * Always returns `executeLocally = true` and `targetNodeId = sourceNodeId`.
   * Serves as the control benchmark for evaluating offloading gains.
2. **`NearestEdgePolicy` (M06-FR-08):**
   * Evaluates all non-local candidates.
   * Selects the candidate with the minimum Euclidean distance `distanceMeters` (obtained via `ns3::MobilityModel`) or minimum propagation delay whose queue is not full (`queueLength < maxQueueLength`).
   * Falls back to local execution if all remote queues are full.
3. **`LowestEstimatedCompletionTimePolicy` (M06-FR-10):**
   * Computes the estimated total completion time $\hat{T}_{\text{total}}$ for each candidate $i$:
     $$\hat{T}_{\text{total}, i} = \hat{T}_{\text{tx\_in}, i} + \hat{T}_{\text{queue}, i} + \hat{T}_{\text{compute}, i} + \hat{T}_{\text{tx\_out}, i}$$
     Where:
     $$\hat{T}_{\text{tx\_in}, i} = \frac{\text{InputBytes} \times 8}{R_i} + D_i$$
     $$\hat{T}_{\text{compute}, i} = \frac{\text{RequiredCycles}}{F_i}$$
     $$\hat{T}_{\text{tx\_out}, i} = \frac{\text{OutputBytes} \times 8}{R_i} + D_i$$
   * For the local node ($i = \text{local}$), network transmission times are zero ($\hat{T}_{\text{tx\_in}} = \hat{T}_{\text{tx\_out}} = 0$).
   * Selects $\arg\min_i \hat{T}_{\text{total}, i}$. If candidate queues are saturated, selects the lowest feasible option.

---

### 3.4 Helper & Management Subsystem (`helper/edge-helper.h`)

The `EdgeHelper` encapsulates boilerplate configuration and provides high-level deployment methods:
* **Installation Methods:**
  * `InstallComputeNode(NodeContainer nodes)`: Aggregates `ComputeNodeModel` onto specified nodes.
  * `InstallClient(NodeContainer nodes)`: Installs `TaskClientApplication` and configures default attributes.
  * `InstallServer(NodeContainer nodes)`: Installs `EdgeComputeApplication` and attaches to local `ComputeNodeModel`.
* **Telemetry Configuration:**
  * `EnableCsvTracing(std::string filePath)`: Binds to client and server `TraceSource`s and streams the 12-column M06 task records.
  * `EnableCommonEventLog(std::string filePath)`: Streams events formatted according to the FutureNet Common Guidelines schema.

---

## 4. End-to-End Latency Model & Timestamp Decomposition

### 4.1 The 5 Milestone Timestamps

To ensure that timing statistics are mathematically consistent without using global state (which is prohibited under CG-08), each task is tracked across **5 milestone timestamps**:

```text
Simulation Timeline:
------------------------------------------------------------------------------------> Time
    |                  |                 |                  |                |
 t_create       t_rx_complete       t_exec_start       t_exec_end          t_done
    |                  |                 |                  |                |
    +--- Network In ---+--- Queue Wait --+--- CPU Service --+-- Network Out -+
    |                                                                        |
    +--------------------------- End-to-End Latency -------------------------+
```

1. **$t_{\text{create}}$:** Recorded at `TaskClientApplication` when the task is instantiated.
2. **$t_{\text{rx\_complete}}$:** Recorded at `EdgeComputeApplication` when the final byte of `TaskInputBytes` is received over the network. *(For local execution, $t_{\text{rx\_complete}} = t_{\text{create}}$).*
3. **$t_{\text{exec\_start}}$:** Recorded at `ComputeNodeModel` when the task moves from the queue to the CPU.
4. **$t_{\text{exec\_end}}$:** Recorded at `ComputeNodeModel` when CPU execution completes ($t_{\text{exec\_start}} + C/F$).
5. **$t_{\text{done}}$:** Recorded at `TaskClientApplication` when the final byte of result data or ACK packet arrives. *(For local execution, $t_{\text{done}} = t_{\text{exec\_end}}$).*

### 4.2 Exact Metric Decomposition Formulas

Every duration is computed directly from milestone differences and reported in milliseconds (`ms`):

$$\text{queue\_ms} = (t_{\text{exec\_start}} - t_{\text{rx\_complete}}) \times 1000.0$$

$$\text{compute\_ms} = (t_{\text{exec\_end}} - t_{\text{exec\_start}}) \times 1000.0$$

$$\text{network\_ms} = \left[ (t_{\text{rx\_complete}} - t_{\text{create}}) + (t_{\text{done}} - t_{\text{exec\_end}}) \right] \times 1000.0$$

$$\text{e2e\_ms} = (t_{\text{done}} - t_{\text{create}}) \times 1000.0$$

### 4.3 Conservation Law

Because intermediate terms cancel out identically:
$$\begin{aligned}
\text{queue} + \text{compute} + \text{network} &= (t_{\text{exec\_start}} - t_{\text{rx\_complete}}) + (t_{\text{exec\_end}} - t_{\text{exec\_start}}) \\
&\quad + (t_{\text{rx\_complete}} - t_{\text{create}}) + (t_{\text{done}} - t_{\text{exec\_end}}) \\
&= t_{\text{done}} - t_{\text{create}} \\
&= \text{e2e}
\end{aligned}$$

Thus:
$$\mathbf{queue\_ms + compute\_ms + network\_ms \equiv e2e\_ms}$$

This conservation holds for local tasks (where $\text{network\_ms} = 0$) and remote tasks in both `ResultData` and `AckOnly` modes.

### 4.4 End-to-End Sequence Flows

#### 4.4.1 Remote Offloading Sequence
```text
 TaskClientApp             Network Channel         EdgeComputeApp           ComputeQueue           ComputeNodeModel
      │                          │                       │                       │                        │
  (1) │ t_create recorded        │                       │                       │                        │
      ├─────────────────────────►│                       │                       │                        │
      │  TCP/UDP Input Chunks    │                       │                       │                        │
      │                          ├──────────────────────►│                       │                        │
      │                          │  Final chunk arrives  │                       │                        │
      │                          │                       │ (2) t_rx_complete     │                        │
      │                          │                       ├──────────────────────►│                        │
      │                          │                       │  SubmitTask(task)     │                        │
      │                          │                       │                       │ (3) Task Queued        │
      │                          │                       │                       ├───────────────────────►│
      │                          │                       │                       │  Reaches head of queue │
      │                          │                       │                       │                        │ (4) t_exec_start
      │                          │                       │                       │                        │     Simulating C / F
      │                          │                       │                       │                        │ (5) t_exec_end
      │                          │                       │◄───────────────────────────────────────────────┤
      │                          │                       │  OnTaskComputeFinished(waitTime, computeTime)  │
      │                          │                       │                                                │
      │                          │◄──────────────────────┤ (6) Embed waitTime & computeTime in reply      │
      │                          │  Result/Ack Packet    │                                                │
      │◄─────────────────────────┤                       │                                                │
      │  Reply packet arrives    │                       │                                                │
  (7) │  t_done recorded         │                       │                                                │
      │  queue_ms + network_ms + compute_ms == e2e_ms    │                                                │
      │  Emit TaskCompleted trace & Write to CSV         │                                                │
      ▼                          ▼                       ▼                       ▼                        ▼
```

#### 4.4.2 Local Execution Sequence
```text
 TaskClientApp                                     Local ComputeQueue               Local ComputeNodeModel
      │                                                     │                                  │
  (1) │ t_create recorded                                   │                                  │
      │ (t_rx_complete = t_create)                          │                                  │
      ├────────────────────────────────────────────────────►│                                  │
      │ SubmitTask(task) locally                            │                                  │
      │                                                     │ (2) Task Enqueued                │
      │                                                     ├─────────────────────────────────►│
      │                                                     │ Reaches head of queue            │
      │                                                     │                                  │ (3) t_exec_start
      │                                                     │                                  │     Simulating C / F
      │                                                     │                                  │ (4) t_exec_end (t_done = t_exec_end)
      │◄───────────────────────────────────────────────────────────────────────────────────────┤
      │ OnLocalExecutionComplete(task, waitTime, computeTime)                                  │
  (5) │ network_ms = 0.000 ms                                                                  │
      │ queue_ms = waitTime, compute_ms = computeTime, e2e_ms = waitTime + computeTime         │
      │ Emit TaskCompleted trace & Write to CSV                                                │
      ▼                                                     ▼                                  ▼
```

---

## 5. Algorithmic Complexity & Scalability Analysis (CG Section 11)

In compliance with Common Guidelines Section 11, the module is designed to prevent performance bottlenecks. Simulation scalability is preserved by adhering to strictly bounded complexity models.

### 5.1 Complexity Matrix

| Operation | Implementation Mechanism | Time Complexity | Justification |
| :--- | :--- | :--- | :--- |
| **Task Generation** | Periodic timer / `Simulator::Schedule` | $O(1)$ | Timer insertion into ns-3 calendar queue |
| **Queue Insertion** | `std::deque::push_back` | $O(1)$ amortized | Standard deque double-ended buffer |
| **Queue Deletion** | `std::deque::pop_front` | $O(1)$ | Direct head removal |
| **Queue Wait Estimation** | `m_totalQueuedCycles / F` | $O(1)$ | Running cycle accumulator updated on push/pop |
| **Target Policy Selection** | Linear scan of candidate list | $O(M)$ | Evaluates $M$ candidate nodes ($M \ll N$, typically $M \le 5$) |
| **Network Reception** | Socket read & byte counter update | $O(1)$ | Constant-time accumulator per incoming chunk |
| **CPU Execution Event** | `Simulator::Schedule(C/F, ...)` | $O(\log E)$ | Standard ns-3 discrete event calendar scheduling |
| **Task Cancellation** | `std::deque::erase` | $O(Q)$ | Linear scan over queued tasks $Q$ on explicit cancellation |

### 5.2 Proof of Avoidance of $O(N^2)$ Per-Tick Scans

1. **Pure Event-Driven Mechanics:** The module contains no polling loops or periodic sweeps across all nodes.
2. **Local Candidate Sets:** The policy engine evaluates only the candidate targets registered via `AddComputeTarget()` ($M$ nodes), rather than scanning the entire simulation node container ($N$ nodes).
3. **Decoupled State Updates:** Server states are queried on-demand during task creation ($O(M)$) or periodically cached via `PolicyEvaluationInterval`, ensuring work is proportional to task generation rate rather than simulation step size.

### 5.3 Memory Scalability

* **Task State Footprint:** Memory allocation is proportional to the number of concurrent in-flight tasks $T_{\text{active}}$:
  $$\text{Memory} = O(N + M + T_{\text{active}})$$
* **Garbage Collection:** Once a task completes and its record is emitted to CSV, the client and server immediately erase its reassembly buffers and tracking state, preventing long-running simulation memory leaks.

---

## 6. Cross-Module Interoperability (Family C Alignment)

`futurenet-edge` is a cornerstone of **Family C (Edge and Distributed Intelligence Workloads)** and provides public interfaces for cross-module integration (CG Section 15):

```text
 ┌──────────────────────────────────────────────┐
 │         M08 Federated Learning               │
 │  - Training workloads (C cycles)             │
 │  - Model update transfers (Sin, Sout bytes)  │
 └──────────────────────┬───────────────────────┘
                        │ generates compute tasks
                        ▼
 ┌──────────────────────────────────────────────┐          passes tagged packets     ┌───────────────────────────────┐
 │            M06 Edge Computing                │───────────────────────────────────►│          M05 QoS Queueing     │
 │            (futurenet-edge)                  │◄──┐       (FutureNetTrafficClass)  │  - Priority-aware queues (0-7)│
 └──────────────────────▲───────────────────────┘   │                                │  - Deadline-sensitive dropping│
                        │                           │                                └───────────────────────────────┘
   offloads telemetry & │                           │
   sensing tasks        │                           │
 ┌──────────────────────┴───────────────────────┐   │ observations
 │             M09 V2X Mobility                 │   │ (ComputeNodeState)
 │  - Road-network vehicles & RSU discovery     │   │
 │  - NearestEdge offloading to roadside units  │   │
 └──────────────────────────────────────────────┘   │
                                                    ▼
                                 ┌─────────────────────────────────────┐
                                 │       M10 Agentic AI Control        │
                                 │  - External Python / Gym AI agent   │
                                 │  - Selects optimal OffloadDecision  │
                                 └─────────────────────────────────────┘
```

1. **Integration with M08 (Federated Learning):**
   * M08 generates distributed training tasks across participating nodes.
   * `TaskDescriptor` represents local model training rounds ($C$ cycles) and gradient exchange ($S_{\text{in}}, S_{\text{out}}$ bytes).
2. **Integration with M05 (Deadline/Priority-Aware Queueing):**
   * Tasks convert to `FutureNetTrafficClass` (CG Section 4.4).
   * Packets carry priority tags ($0$–$7$) and task deadlines so underlying switches prioritize offloading traffic.
3. **Integration with M09 (V2X Mobility):**
   * Moving vehicles running `TaskClientApplication` dynamically discover roadside units (RSUs) running `EdgeComputeApplication`.
   * `NearestEdgePolicy` utilizes vehicle mobility models to offload to the nearest RSU.
4. **Integration with M10 (Agentic AI Control):**
   * An external Python agent (via Gym or ns3-ai) acts as an external policy.
   * The agent consumes `ComputeNodeState` observations and outputs `OffloadDecision` choices per task.

---

## 7. Telemetry, Tracing & Logging Architecture

### 7.1 ns-3 TraceSources (CG Section 7.1)

All state transitions are exposed via official `ns3::TracedCallback` attributes. Direct output using `std::cout` is prohibited (CG-03).

| TraceSource Name | Host Class | Argument Signature | Trigger Condition |
| :--- | :--- | :--- | :--- |
| `TaskCreated` | `TaskClientApplication` | `(uint64_t taskId, uint32_t sourceId)` | Task instantiated by generator |
| `TaskOffloaded` | `TaskClientApplication` | `(uint64_t taskId, uint32_t sourceId, uint32_t targetId)` | Task committed to network transmission |
| `TaskQueued` | `ComputeNodeModel` | `(uint64_t taskId, uint32_t targetId, uint32_t queueLen)` | Task admitted into execution queue |
| `TaskExecutionStart` | `ComputeNodeModel` | `(uint64_t taskId, uint32_t targetId)` | CPU begins processing task |
| `TaskExecutionComplete` | `ComputeNodeModel` | `(uint64_t taskId, uint32_t targetId)` | CPU finishes processing task |
| `TaskCompleted` | `TaskClientApplication` | `(uint64_t taskId, Time e2eTime)` | Client finishes task (local or remote) |
| `TaskDeadlineMiss` | `TaskClientApplication` | `(uint64_t taskId, Time lateness)` | Task completes after deadline ($t_{\text{done}} > t_{\text{deadline}}$) |

### 7.2 Machine-Readable Output Schemas

#### 7.2.1 Mandatory 12-Column M06 Task CSV Schema
Exported via `EdgeHelper::EnableCsvTracing(filePath)`:
```csv
task_id,source,target,created_s,input_bytes,cycles,queue_ms,network_ms,compute_ms,e2e_ms,deadline_ms,completed
1,0,1,1.000,1000000,1000000000,0.000,18.400,100.000,118.400,500.000,1
```

#### 7.2.2 Common Guidelines 10-Column Event Log Schema
Exported via `EdgeHelper::EnableCommonEventLog(filePath)` (CG Section 4.5):
```csv
time_s,module,event,node_id,peer_id,flow_id,value1,value2,unit,detail
1.000,futurenet-edge,TaskCreated,0,0,1,1000000,1e9,bytes_cycles,task_id=1
```

---

## 8. Engineering Standards Compliance

The architecture enforces all software engineering mandates outlined in Common Guidelines Section 3:
* **CG-01 (GetTypeId & Attributes):** Every configurable parameter is registered in the ns-3 Attribute system.
* **CG-02 (No Hardcoding):** Buffer sizes, chunk sizes, and port numbers are attribute-driven.
* **CG-03 (Tracing):** Instrumentation strictly uses `TracedCallback`.
* **CG-06 (ns-3 Native Types):** Interfaces utilize `ns3::Time`, `ns3::DataRate`, `ns3::Ptr<T>`, and `ns3::NodeContainer`.
* **CG-08 (Zero Global State):** All state resides inside object instances; intermediate timing travels through packet headers.
* **CG-09 & CG-10 (Deterministic Reproducibility):** Task generators utilize `ns3::RandomVariableStream` with explicit `AssignStreams()` methods.
* **CG-11 (Parameter Validation):** Attributes enforce bounds checks; invalid inputs result in assertions or fatal errors.
* **CG-15 (Minimal Core Modification):** The module is completely self-contained in `contrib/futurenet-edge/` and requires zero modifications to the ns-3 `src/` core.

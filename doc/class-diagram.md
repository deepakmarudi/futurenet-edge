# Class Diagram & Object Model: futurenet-edge (Module M06)
**Future Network Extensions for ns-3.46.1**  
**Module Name:** `futurenet-edge`  
**Classification:** Industry-Grade Object Model & Interface Specification  
**Target Platform:** `ns-allinone-3.46.1` / `ns-3.46.1`  
**Standard Compliance:** Common Guidelines FutureNet ns-3.46.1 & SRS Module M06  

---

## 1. Executive Summary & Object-Oriented Principles

This document provides the formal object-oriented design and structural class specification for the **`futurenet-edge`** module in ns-3.46.1. The system architecture adheres strictly to ns-3 development best practices and the FutureNet Common Guidelines:

1. **ns-3 Object Model Integration (CG-01):** All core entities representing long-lived simulation components inherit from `ns3::Object` (or `ns3::Application`) and register a unique `ns3::TypeId`. This enables runtime attribute configuration, dynamic type discovery, aggregation (`AggregateObject`), and trace source registration.
2. **Composition Over Inheritance (CG-07):** Deep inheritance hierarchies are avoided. Subsystems are built through composition (e.g., `ComputeNodeModel` aggregates `ComputeQueue`, `EdgeComputeApplication` references `ComputeNodeModel`, and `TaskClientApplication` composes `OffloadPolicy`).
3. **Strict Mechanism/Policy Separation (CG Section 1.3):** The decision logic governing where a task executes is encapsulated in the `OffloadPolicy` interface, completely decoupled from the execution queueing (`ComputeNodeModel`) and network framing (`EdgeComputeApplication`).
4. **Const-Correctness and Strong Typing (CG-06, CG Section 12.3):** Method signatures enforce const-correctness on inspection methods and use standard ns-3 semantic types (`ns3::Time`, `ns3::DataRate`, `ns3::Address`, `ns3::Ptr<T>`).
5. **Zero Global Mutable State (CG-08):** No global variables or static state accumulators exist. All simulation state is scoped within object instances or encapsulated inside the wire protocol (`TaskPacketHeader`).

---

## 2. Global Unified Class Hierarchy

```text
 ══════════════════════════════════════════════════════════════════════════════════════════════════
                                    ns-3 CORE FRAMEWORK BASE CLASSES
 ══════════════════════════════════════════════════════════════════════════════════════════════════
               ┌───────────────────────┐                    ┌───────────────────────┐
               │      ns3::Object      │                    │   ns3::Application    │
               │ + GetTypeId() : TypeId│                    │ # StartApplication()  │
               └───────────┬───────────┘                    │ # StopApplication()   │
                           │                                └───────────┬───────────┘
       ┌───────────────────┼───────────────────┐                        │
       │                   │                   │                        │
       ▼                   ▼                   ▼                        ▼
 ┌───────────────┐   ┌───────────┐   ┌───────────────────┐   ┌───────────────────────┐
 │ ComputeNode   │   │ Compute   │   │   OffloadPolicy   │   │ TaskClientApplication │
 │ Model         │   │ Queue     │   │   (Interface)     │   │ - local execution     │
 └───────┬───────┘   └─────┬─────┘   └─────────┬─────────┘   │ - remote offload      │
         │                 │                   │             │ - 5-milestone records │
         │ owns            │ manages           │             └───────────────────────┘
         ▼                 ▼                   │                        │
   ┌───────────┐     ┌───────────┐             │                        ▼
   │ std::deque│     │ Compute   │             │             ┌───────────────────────┐
   │ of tasks  │     │ QueueEntry│             │             │ EdgeComputeApplication│
   └───────────┘     └─────┬─────┘             │             │ - chunk reassembly    │
                           │ holds             │             │ - gates queue entry   │
                           ▼                   │             │ - returns result/ack  │
                     ┌───────────┐             │             └───────────────────────┘
                     │ Task      │             │
                     │ Descriptor│             │
                     └───────────┘             │
                                               ├───────────────────┬───────────────────┐
                                               ▼                   ▼                   ▼
                                       ┌───────────────┐   ┌───────────────┐   ┌───────────────┐
                                       │LocalOnlyPolicy│   │NearestEdge    │   │LowestEstimated│
                                       │ (deterministic│   │Policy         │   │CompletionTime │
                                       │  baseline)    │   │ (proximity)   │   │Policy (QoS)   │
                                       └───────────────┘   └───────────────┘   └───────────────┘
 ══════════════════════════════════════════════════════════════════════════════════════════════════
                                 PACKET & PROTOCOL FRAMING
 ══════════════════════════════════════════════════════════════════════════════════════════════════
               ┌───────────────────────┐
               │      ns3::Header      │
               │ + Serialize()         │
               │ + Deserialize()       │
               └───────────┬───────────┘
                           │ inherits
                           ▼
               ┌───────────────────────────────────────────────────────────────┐
               │                       TaskPacketHeader                        │
               │ - Fixed 64-Byte Application Wire Header                       │
               │ - PacketType, SourceId, TargetId, ChunkBytes, TaskId          │
               │ - InputBytes, OutputBytes, RequiredCycles                     │
               │ - QueueDurationNs, ComputeDurationNs (embedded in reply)      │
               └───────────────────────────────────────────────────────────────┘
 ══════════════════════════════════════════════════════════════════════════════════════════════════
                                 HELPER & TELEMETRY SUBSYSTEM
 ══════════════════════════════════════════════════════════════════════════════════════════════════
               ┌───────────────────────────────────────────────────────────────┐
               │                          EdgeHelper                           │
               │ - InstallComputeNode(nodes: NodeContainer) : void             │
               │ - InstallClient(nodes: NodeContainer) : ApplicationContainer  │
               │ - InstallServer(nodes: NodeContainer) : ApplicationContainer  │
               │ - EnableCsvTracing(filePath: string) : void (12-column CSV)   │
               │ - EnableCommonEventLog(filePath: string) : void (10-col log)  │
               └───────────────────────────────────────────────────────────────┘
```

---

## 3. Subsystem Detailed Class Specifications

### 3.1 Compute & Queue Modeling Subsystem

```text
 ┌─────────────────────────────────────────────────────────────────────────────────────────┐
 │                                   ComputeNodeModel                                      │
 │                                   (ns3::Object)                                         │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ - m_computeCapacity        : double (cycles/s; DeviceComputeCapacity attribute)         │
 │ - m_queue                  : Ptr<ComputeQueue> (execution buffer)                       │
 │ - m_isExecuting            : bool (CPU busy flag)                                       │
 │ - m_currentRunningTask     : ComputeQueueEntry (active workload)                        │
 │ - m_currentExecutionEvent  : EventId (scheduled simulation event)                       │
 │ - m_taskQueuedTrace        : TracedCallback<uint64_t, uint32_t, uint32_t>               │
 │ - m_taskExecStartTrace     : TracedCallback<uint64_t, uint32_t>                         │
 │ - m_taskExecCompleteTrace  : TracedCallback<uint64_t, uint32_t>                         │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ + GetTypeId() : TypeId [static]                                                         │
 │ + SetComputeCapacity(cyclesPerSec: double) : void                                       │
 │ + GetComputeCapacity() const : double                                                   │
 │ + GetQueueLength() const : uint32_t                                                     │
 │ + EstimateCompletion(task: const TaskDescriptor&) const : Time                          │
 │ + SubmitTask(task: const TaskDescriptor&, onComplete: Callback) : bool                  │
 │ + CancelTask(taskId: uint64_t) : void                                                   │
 │ + GetNetworkState() const : NetworkState                                                │
 │ - ProcessNextTask() : void                                                              │
 │ - OnTaskExecutionFinished() : void                                                      │
 └───────────────────────────────────────────┬─────────────────────────────────────────────┘
                                             │ 1 (owns)
                                             ▼ 1
 ┌─────────────────────────────────────────────────────────────────────────────────────────┐
 │                                     ComputeQueue                                        │
 │                                    (ns3::Object)                                        │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ - m_queue              : std::deque<ComputeQueueEntry> (O(1) push/pop)                  │
 │ - m_maxQueuedTasks     : uint32_t (MaxQueuedTasks attribute, default 100)               │
 │ - m_scheduling         : std::string (ComputeScheduling attribute, "FCFS")              │
 │ - m_totalQueuedCycles  : double (running cycle accumulator)                             │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ + GetTypeId() : TypeId [static]                                                         │
 │ + Enqueue(entry: const ComputeQueueEntry&) : bool                                       │
 │ + Dequeue(outEntry: ComputeQueueEntry&) : bool                                          │
 │ + Peek(outEntry: ComputeQueueEntry&) const : bool                                       │
 │ + GetQueueLength() const : uint32_t                                                     │
 │ + GetMaxQueueLength() const : uint32_t                                                  │
 │ + GetTotalQueuedCycles() const : double                                                 │
 │ + IsFull() const : bool                                                                 │
 │ + IsEmpty() const : bool                                                                │
 │ + RemoveTask(taskId: uint64_t) : bool                                                   │
 └───────────────────────────────────────────┬─────────────────────────────────────────────┘
                                             │ 1 (manages)
                                             ▼ 0..N
 ┌─────────────────────────────────────────────────────────────────────────────────────────┐
 │                                  ComputeQueueEntry                                      │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ + task               : TaskDescriptor                                                   │
 │ + timeQueued         : Time                                                             │
 │ + timeExecStart      : Time                                                             │
 │ + onCompleteCallback : Callback<void, const TaskDescriptor&, Time, Time>                │
 └───────────────────────────────────────────┬─────────────────────────────────────────────┘
                                             │ 1 (contains)
                                             ▼ 1
 ┌─────────────────────────────────────────────────────────────────────────────────────────┐
 │                                   TaskDescriptor                                        │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ - m_taskId         : uint64_t (unique simulation ID)                                    │
 │ - m_sourceNodeId   : uint32_t (client node ID)                                          │
 │ - m_inputBytes     : uint64_t (upload payload size)                                     │
 │ - m_outputBytes    : uint64_t (result payload size)                                     │
 │ - m_outputRatio    : double (ratio of input size, default -1.0 disabled)                │
 │ - m_useOutputRatio : bool                                                               │
 │ - m_requiredCycles : double (computational cycles)                                      │
 │ - m_creationTime   : Time (instantiation moment)                                        │
 │ - m_deadline       : Time (optional QoS deadline)                                       │
 │ - m_hasDeadline    : bool                                                               │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ + TaskDescriptor()                                                                      │
 │ + TaskDescriptor(taskId, sourceId, inBytes, outBytes, cycles)                           │
 │ + GetTaskId() const : uint64_t                                                          │
 │ + GetSourceNodeId() const : uint32_t                                                    │
 │ + GetInputBytes() const : uint64_t                                                      │
 │ + GetOutputBytes() const : uint64_t  (evaluates inBytes * outputRatio if ratio >= 0.0)  │
 │ + GetRequiredCycles() const : double                                                    │
 │ + GetCreationTime() const : Time                                                        │
 │ + GetDeadline() const : Time                                                            │
 │ + HasDeadline() const : bool                                                            │
 │ + SetOutputRatio(ratio: double) : void                                                  │
 │ + SetDeadline(deadline: Time) : void                                                    │
 │ + ToTrafficClass() const : FutureNetTrafficClass                                        │
 └─────────────────────────────────────────────────────────────────────────────────────────┘
```

#### 3.1.1 `TaskDescriptor`
* **File:** `model/task-descriptor.h`, `model/task-descriptor.cc`
* **Base Class:** None (Plain data object with value semantics).
* **Key Responsibilities:**
  * Encapsulates complete task specification as mandated by **M06-FR-01**.
  * Dynamically calculates `GetOutputBytes()`:
    ```cpp
    uint64_t TaskDescriptor::GetOutputBytes() const {
        if (m_useOutputRatio) {
            return static_cast<uint64_t>(static_cast<double>(m_inputBytes) * m_outputRatio);
        }
        return m_outputBytes;
    }
    ```
  * Interoperates with Family B QoS via `ToTrafficClass()`.

#### 3.1.2 `ComputeQueue`
* **File:** `model/compute-queue.h`, `model/compute-queue.cc`
* **Base Class:** `ns3::Object`
* **Key Responsibilities:**
  * Manages waiting tasks using `std::deque<ComputeQueueEntry>`.
  * Enforces maximum queue capacity attribute (`MaxQueuedTasks`, default 100, range 1 to 100,000).
  * Maintains running total of queued cycles (`m_totalQueuedCycles`) updated in $O(1)$ time upon insertion and removal.

#### 3.1.3 `ComputeNodeModel`
* **File:** `model/compute-node-model.h`, `model/compute-node-model.cc`
* **Base Class:** `ns3::Object`
* **Key Responsibilities:**
  * Models execution unit with processing capacity $F$ (`DeviceComputeCapacity`, cycles/s).
  * Executes non-preemptive tasks using event scheduling:
    ```cpp
    Time serviceTime = Seconds(task.GetRequiredCycles() / m_computeCapacity);
    m_currentExecutionEvent = Simulator::Schedule(serviceTime, &ComputeNodeModel::OnTaskExecutionFinished, this);
    ```
  * Emits `TaskQueued`, `TaskExecutionStart`, and `TaskExecutionComplete` trace events.
  * Implements `EstimateCompletion()` for policy guidance.
  * Safely cancels pending or executing tasks via `CancelTask()`.

---

### 3.2 Network Protocol & Wire Framing Subsystem

```text
 ┌─────────────────────────────────────────────────────────────────────────────────────────┐
 │                                   TaskPacketHeader                                      │
 │                                     (ns3::Header)                                       │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ - m_packetType        : uint8_t  (1=REQUEST, 2=RESULT, 3=ACK_ONLY)                      │
 │ - m_reserved[3]       : uint8_t  (3 bytes 4-byte struct padding)                        │
 │ - m_sourceNodeId      : uint32_t (source client node ID)                                │
 │ - m_targetNodeId      : uint32_t (destination server node ID)                           │
 │ - m_chunkBytes        : uint32_t (segment chunk size, <= 1400 bytes)                     │
 │ - m_taskId            : uint64_t (unique task identifier)                               │
 │ - m_inputBytes        : uint64_t (total required input payload)                         │
 │ - m_outputBytes       : uint64_t (total result output payload)                          │
 │ - m_requiredCycles    : double   (computational demand)                                 │
 │ - m_queueDurationNs   : int64_t  (server queue delay in nanoseconds)                    │
 │ - m_computeDurationNs : int64_t  (server execution service time in nanoseconds)         │
 ├─────────────────────────────────────────────────────────────────────────────────────────┤
 │ + GetTypeId() : TypeId [static]                                                         │
 │ + GetInstanceTypeId() const : TypeId                                                    │
 │ + Serialize(start: Buffer::Iterator) const : void                                       │
 │ + Deserialize(start: Buffer::Iterator) : uint32_t                                      │
 │ + GetSerializedSize() const : uint32_t  [RETURNS EXACTLY 64 BYTES]                     │
 │ + Print(os: std::ostream&) const : void                                                 │
 │ + SetPacketType(type: uint8_t) : void                                                   │
 │ + SetSourceNodeId(id: uint32_t) : void                                                  │
 │ + SetTargetNodeId(id: uint32_t) : void                                                  │
 │ + SetChunkBytes(bytes: uint32_t) : void                                                 │
 │ + SetTaskId(id: uint64_t) : void                                                        │
 │ + SetInputBytes(bytes: uint64_t) : void                                                 │
 │ + SetOutputBytes(bytes: uint64_t) : void                                                │
 │ + SetRequiredCycles(cycles: double) : void                                              │
 │ + SetQueueDuration(duration: Time) : void                                               │
 │ + SetComputeDuration(duration: Time) : void                                             │
 │ + GetPacketType() const : uint8_t                                                       │
 │ + GetSourceNodeId() const : uint32_t                                                    │
 │ + GetTargetNodeId() const : uint32_t                                                    │
 │ + GetChunkBytes() const : uint32_t                                                      │
 │ + GetTaskId() const : uint64_t                                                          │
 │ + GetInputBytes() const : uint64_t                                                      │
 │ + GetOutputBytes() const : uint64_t                                                     │
 │ + GetRequiredCycles() const : double                                                    │
 │ + GetQueueDuration() const : Time                                                       │
 │ + GetComputeDuration() const : Time                                                     │
 └─────────────────────────────────────────────────────────────────────────────────────────┘
```

#### 3.2.1 `TaskPacketHeader`
* **File:** `model/task-packet-header.h`, `model/task-packet-header.cc`
* **Base Class:** `ns3::Header`
* **Wire Protocol Memory Map (64 Bytes Exact):**
```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  PacketType   |                   Reserved                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         SourceNodeId                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         TargetNodeId                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          ChunkBytes                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                            TaskId                             |
|                          (64-bit)                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          InputBytes                           |
|                          (64-bit)                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          OutputBytes                          |
|                          (64-bit)                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        RequiredCycles                         |
|                       (double, 64-bit)                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       QueueDurationNs                         |
|                       (int64, 64-bit)                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      ComputeDurationNs                        |
|                       (int64, 64-bit)                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

---

### 3.3 Applications Subsystem

```text
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                                 TaskClientApplication                                  │
 │                                   (ns3::Application)                                   │
 ├────────────────────────────────────────────────────────────────────────────────────────┤
 │ - m_policy              : Ptr<OffloadPolicy>                                           │
 │ - m_localCompute        : Ptr<ComputeNodeModel> (optional local CPU)                   │
 │ - m_candidateTargets    : std::vector<ComputeNodeState>                                │
 │ - m_records             : std::map<uint64_t, TaskClientRecord> (5-milestone tracker)   │
 │ - m_clientTxSocket      : Ptr<Socket> (TCP stream or UDP datagram socket)              │
 │ - m_clientRxSocket      : Ptr<Socket>                                                  │
 │ - m_nextTaskId          : uint64_t                                                     │
 │ - m_taskArrivalInterval : Time (default: Seconds(1.0))                                 │
 │ - m_nextTaskEvent       : EventId                                                      │
 │ - m_policyInterval      : Time (per-task or periodic caching)                          │
 │ - m_policyRefreshEvent  : EventId                                                      │
 │ - m_arrivalRng          : Ptr<RandomVariableStream> (CG-09)                            │
 │ - m_traceTaskCreated    : TracedCallback<uint64_t, uint32_t>                           │
 │ - m_traceTaskOffloaded  : TracedCallback<uint64_t, uint32_t, uint32_t>                 │
 │ - m_traceTaskCompleted  : TracedCallback<uint64_t, Time>                               │
 │ - m_traceTaskDeadline   : TracedCallback<uint64_t, Time>                               │
 ├────────────────────────────────────────────────────────────────────────────────────────┤
 │ + GetTypeId() : TypeId [static]                                                        │
 │ + AddComputeTarget(node, addr, rateMbps, delayMs) : void                               │
 │ + SetOffloadPolicy(policy: Ptr<OffloadPolicy>) : void                                  │
 │ + GenerateTask() : uint64_t                                                            │
 │ + GetTaskStatus(taskId: uint64_t) const : TaskStatus                                   │
 │ + AssignStreams(stream: int64_t) : int64_t                                             │
 │ - StartApplication() : void                                                            │
 │ - StopApplication() : void                                                             │
 │ - ScheduleNextTask() : void                                                            │
 │ - ExecuteLocally(task: const TaskDescriptor&) : void                                   │
 │ - OffloadRemotely(task: const TaskDescriptor&, target: const ComputeNodeState&) : void │
 │ - OnLocalExecutionComplete(task, waitTime, computeTime) : void                         │
 │ - HandleRead(socket: Ptr<Socket>) : void                                               │
 │ - FinalizeTask(taskId: uint64_t) : void                                                │
 └───────────────────────────────────────────┬────────────────────────────────────────────┘
                                             │ manages
                                             ▼
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                                   TaskClientRecord                                     │
 ├────────────────────────────────────────────────────────────────────────────────────────┤
 │ + task                  : TaskDescriptor                                               │
 │ + timeCreated           : Time (t_create)                                              │
 │ + timeRxComplete        : Time (t_rx_complete)                                         │
 │ + timeExecStart         : Time (t_exec_start)                                          │
 │ + timeExecEnd           : Time (t_exec_end)                                            │
 │ + timeDone              : Time (t_done)                                                │
 │ + remoteQueueDuration   : Time                                                         │
 │ + remoteComputeDuration : Time                                                         │
 │ + targetNodeId          : uint32_t                                                     │
 │ + status                : TaskStatus                                                   │
 └────────────────────────────────────────────────────────────────────────────────────────┘

 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                                 EdgeComputeApplication                                 │
 │                                   (ns3::Application)                                   │
 ├────────────────────────────────────────────────────────────────────────────────────────┤
 │ - m_computeModel : Ptr<ComputeNodeModel>                                               │
 │ - m_listenSocket : Ptr<Socket> (TCP listener or UDP bound socket)                      │
 │ - m_assemblyMap  : std::map<uint64_t, RxTaskContext> (in-flight chunk reassembly)      │
 │ - m_port         : uint16_t (default: 9090)                                            │
 │ - m_protocol     : std::string ("TCP" or "UDP")                                        │
 │ - m_ackMode      : std::string ("ResultData" or "AckOnly")                             │
 ├────────────────────────────────────────────────────────────────────────────────────────┤
 │ + GetTypeId() : TypeId [static]                                                        │
 │ + SetComputeNodeModel(model: Ptr<ComputeNodeModel>) : void                             │
 │ + GetComputeNodeModel() const : Ptr<ComputeNodeModel>                                  │
 │ - StartApplication() : void                                                            │
 │ - StopApplication() : void                                                             │
 │ - HandleAccept(socket: Ptr<Socket>, from: const Address&) : void                       │
 │ - HandleRead(socket: Ptr<Socket>) : void                                               │
 │ - ProcessIncomingChunk(header, packet, clientAddr) : void                              │
 │ - OnTaskComputeFinished(task, waitTime, computeTime) : void                            │
 │ - SendResult(clientAddr, task, waitTime, computeTime) : void                           │
 └───────────────────────────────────────────┬────────────────────────────────────────────┘
                                             │ manages
                                             ▼
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                                     RxTaskContext                                      │
 ├────────────────────────────────────────────────────────────────────────────────────────┤
 │ + task               : TaskDescriptor                                                  │
 │ + bytesReceived      : uint64_t (accumulates chunkBytes)                               │
 │ + totalExpectedBytes : uint64_t (inputBytes threshold)                                 │
 │ + timeFirstChunkRx   : Time                                                            │
 │ + clientAddress      : Address (return endpoint for result/ack)                        │
 └────────────────────────────────────────────────────────────────────────────────────────┘
```

#### 3.3.1 `TaskClientApplication`
* **File:** `model/task-client-application.h`, `model/task-client-application.cc`
* **Base Class:** `ns3::Application`
* **Key Responsibilities:**
  * Generates tasks periodically based on `TaskArrivalInterval`.
  * Integrates `AssignStreams(int64_t stream)` for deterministic repeatability (CG-09, CG-10).
  * Performs offload evaluation using `OffloadPolicy::Select()`.
  * Maintains the 5-milestone records per task and verifies latency conservation.
  * Detects deadline misses and fires `TaskDeadlineMiss(taskId, lateness)` trace source.

#### 3.3.2 `EdgeComputeApplication`
* **File:** `model/edge-compute-application.h`, `model/edge-compute-application.cc`
* **Base Class:** `ns3::Application`
* **Key Responsibilities:**
  * Listens for inbound TCP streams or UDP datagrams on configured port (default 9090).
  * Reassembles chunks in `RxTaskContext`.
  * **Enforces M06-FR-06 / Test T03:** Strictly holds task outside `ComputeNodeModel` until `bytesReceived >= totalExpectedBytes`.
  * Upon compute completion, dispatches result traffic (`ResultData` with output bytes or 64-byte `AckOnly` header) back to client address.

---

### 3.4 Decision & Policy Subsystem

```text
                                  ┌───────────────────────────────┐
                                  │         OffloadPolicy         │
                                  │      (ns3::Object Interface)  │
                                  ├───────────────────────────────┤
                                  │ + GetTypeId() : TypeId [stat] │
                                  │ + Select(task: TaskDesc,      │
                                  │          candidates: vector)  │
                                  │   const : OffloadDecision     │
                                  └───────────────┬───────────────┘
                                                  │ inherits
                 ┌────────────────────────────────┼────────────────────────────────┐
                 │                                │                                │
                 ▼                                ▼                                ▼
  ┌─────────────────────────────┐  ┌─────────────────────────────┐  ┌─────────────────────────────┐
  │       LocalOnlyPolicy       │  │      NearestEdgePolicy      │  │  LowestEstimatedCompletion  │
  │        (Baseline)           │  │      (Proximity-Based)      │  │         TimePolicy          │
  ├─────────────────────────────┤  ├─────────────────────────────┤  ├─────────────────────────────┤
  │ + GetTypeId() : TypeId      │  │ + GetTypeId() : TypeId      │  │ + GetTypeId() : TypeId      │
  │ + Select(...) const         │  │ + Select(...) const         │  │ + Select(...) const         │
  │   - executeLocally = true   │  │   - argmin(distanceMeters)  │  │   - argmin(T_est_total)     │
  │   - targetNodeId = sourceId │  │   - subject to Q < Qmax     │  │   - subject to Q < Qmax     │
  └─────────────────────────────┘  └─────────────────────────────┘  └─────────────────────────────┘
                                                  │
                                                  │ outputs
                                                  ▼
                                   ┌─────────────────────────────┐
                                   │       OffloadDecision       │
                                   ├─────────────────────────────┤
                                   │ + executeLocally : bool     │
                                   │ + targetNodeId   : uint32_t │
                                   └─────────────────────────────┘
```

#### 3.4.1 Policy Interface Contracts
* **File:** `model/offload-policy.h`, `model/offload-policy.cc`
* **Base Class:** `ns3::Object`
* **Mathematical Selection Formulations:**
  * **`LocalOnlyPolicy`:**
    $$\text{Decision} = \langle \text{executeLocally} = \text{true}, \text{targetNodeId} = \text{task.GetSourceNodeId()} \rangle$$
  * **`NearestEdgePolicy`:**
    $$\text{Target} = \arg\min_{i \in \text{remotes}} \left\{ d_i \mid Q_i < Q_{\max, i} \right\}$$
    where $d_i$ is distance or estimated propagation latency.
  * **`LowestEstimatedCompletionTimePolicy`:**
    $$\text{Target} = \arg\min_{i \in \text{all}} \left\{ \hat{T}_{\text{tx\_in}, i} + \hat{T}_{\text{queue}, i} + \hat{T}_{\text{compute}, i} + \hat{T}_{\text{tx\_out}, i} \right\}$$
    subject to $Q_i < Q_{\max, i}$.

---

### 3.5 Helper & Telemetry Subsystem

```text
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                                       EdgeHelper                                       │
 ├────────────────────────────────────────────────────────────────────────────────────────┤
 │ - m_computeFactory : ObjectFactory                                                     │
 │ - m_clientFactory  : ObjectFactory                                                     │
 │ - m_serverFactory  : ObjectFactory                                                     │
 │ - m_defaultPolicy  : Ptr<OffloadPolicy>                                                │
 │ - m_csvTaskStream  : Ptr<OutputStreamWrapper>                                          │
 │ - m_csvEventStream : Ptr<OutputStreamWrapper>                                          │
 ├────────────────────────────────────────────────────────────────────────────────────────┤
 │ + EdgeHelper()                                                                         │
 │ + InstallComputeNode(nodes: NodeContainer) : void                                      │
 │ + InstallClient(nodes: NodeContainer) : ApplicationContainer                          │
 │ + InstallServer(nodes: NodeContainer) : ApplicationContainer                          │
 │ + SetComputeAttribute(name: string, val: const AttributeValue&) : void                 │
 │ + SetClientAttribute(name: string, val: const AttributeValue&) : void                  │
 │ + SetServerAttribute(name: string, val: const AttributeValue&) : void                  │
 │ + SetPolicy(policy: Ptr<OffloadPolicy>) : void                                         │
 │ + EnableCsvTracing(filePath: string) : void                                            │
 │ + EnableCommonEventLog(filePath: string) : void                                        │
 │ - TaskCreatedSink(taskId: uint64_t, sourceId: uint32_t) : void                         │
 │ - TaskOffloadedSink(taskId: uint64_t, sourceId: uint32_t, targetId: uint32_t) : void   │
 │ - TaskCompletedSink(taskId: uint64_t, e2eTime: Time) : void                            │
 │ - TaskDeadlineMissSink(taskId: uint64_t, lateness: Time) : void                        │
 └────────────────────────────────────────────────────────────────────────────────────────┘
```

#### 3.5.1 `EdgeHelper`
* **File:** `helper/edge-helper.h`, `helper/edge-helper.cc`
* **Base Class:** None (Standard ns-3 Helper pattern).
* **Key Responsibilities:**
  * Provides fluent API for configuring parameters on node containers.
  * Connects TraceSources to dedicated ASCII/CSV streams.
  * Guarantees strict formatting of the 12-column M06 task records and 10-column FutureNet common event records.

---

## 4. State Machine & Task Lifecycle

The discrete lifecycle of a computational task in `futurenet-edge` is governed by the following finite state machine:

```text
                   ┌──────────────┐
                   │  [Initial]   │
                   └──────┬───────┘
                          │ TaskClientApplication::GenerateTask()
                          ▼
                   ┌──────────────┐
                   │   CREATED    │
                   └──────┬───────┘
                          │
          ┌───────────────┴─────────────────────────┐
          │ Local Execution                         │ Remote Offload
          ▼ Decision                                ▼ Decision
   ┌──────────────┐                         ┌────────────────┐
   │    QUEUED    │                         │OFFLOADING_INPUT│
   │ (Local Node) │                         │ (Network Tx)   │
   └──────┬───────┘                         └───────┬────────┘
          │                                         │
          │                                         ├────────────────────────┐
          │                                         │ All input bytes        │ Socket Error /
          │                                         │ received               │ Network Failure
          │                                         ▼                        ▼
          │                                 ┌────────────────┐       ┌────────────────┐
          │                                 │     QUEUED     │       │ FAILED_NETWORK │
          │                                 │ (Server Queue) │       └───────┬────────┘
          │                                 └───────┬────────┘               │
          │                                         │                        ▼
          ├─────────────────────────────────────────┤                   ┌─────────┐
          │ Head of queue reached                   │                   │ [Final] │
          ▼                                         ▼                   └─────────┘
   ┌──────────────┐                         ┌────────────────┐
   │  EXECUTING   │                         │   EXECUTING    │
   │ (Local CPU)  │                         │  (Remote CPU)  │
   └──────┬───────┘                         └───────┬────────┘
          │                                         │
          │ CPU cycles                              │ CPU cycles
          │ finished                                │ finished
          │                                         ▼
          │                                 ┌────────────────┐
          │                                 │RETURNING_RESULT│
          │                                 │(Result/Ack Tx) │
          │                                 └───────┬────────┘
          │                                         │
          │                                         ├────────────────────────┐
          │                                         │ Result/Ack             │ Return Socket
          │                                         │ received               │ Failure
          ▼                                         ▼                        ▼
   ┌──────────────┐                         ┌────────────────┐       ┌────────────────┐
   │  COMPLETED   │◄────────────────────────┤   COMPLETED    │       │ FAILED_NETWORK │
   │ (Local Task) │                         │ (Remote Task)  │       └───────┬────────┘
   └──────┬───────┘                         └───────┬────────┘               │
          │                                         │                        ▼
          └────────────────────┬────────────────────┘                   ┌─────────┐
                               │                                        │ [Final] │
                               ▼                                        └─────────┘
                          ┌─────────┐
                          │ [Final] │
                          └─────────┘

 Alternative Transitions:
  - If queue limit (MaxQueuedTasks) is reached upon arrival ──► FAILED_QUEUE_FULL ──► [Final]
  - If CancelTask(taskId) is invoked during QUEUED/EXECUTING ─► CANCELLED         ──► [Final]
```

---

## 5. Object Relationship Matrix

| Source Class | Target Class | Relationship Type | Cardinality | Purpose / Semantics |
| :--- | :--- | :--- | :--- | :--- |
| `ComputeNodeModel` | `ComputeQueue` | Composition (`*--`) | $1 \rightarrow 1$ | Manages pending tasks awaiting execution |
| `ComputeQueue` | `ComputeQueueEntry` | Composition (`*--`) | $1 \rightarrow 0..N$ | Storage of queued task records |
| `ComputeQueueEntry`| `TaskDescriptor` | Aggregation (`o--`) | $1 \rightarrow 1$ | Value descriptor holding workload parameters |
| `TaskClientApplication` | `OffloadPolicy` | Aggregation (`o--`) | $1 \rightarrow 1$ | Pluggable policy query for task offloading |
| `TaskClientApplication` | `ComputeNodeModel` | Association (`-->`) | $1 \rightarrow 0..1$ | Local execution node (if configured) |
| `TaskClientApplication` | `TaskPacketHeader` | Dependency (`..>`) | $1 \rightarrow \text{transient}$ | Encapsulates request headers |
| `EdgeComputeApplication`| `ComputeNodeModel` | Aggregation (`o--`) | $1 \rightarrow 1$ | Submits received workloads to local CPU |
| `EdgeComputeApplication`| `TaskPacketHeader` | Dependency (`..>`) | $1 \rightarrow \text{transient}$ | Parses request headers & builds replies |
| `EdgeHelper` | `ComputeNodeModel` | Dependency (`..>`) | $1 \rightarrow \text{many}$ | Installs compute capabilities onto nodes |
| `EdgeHelper` | `TaskClientApplication`| Dependency (`..>`) | $1 \rightarrow \text{many}$ | Installs client applications onto nodes |
| `EdgeHelper` | `EdgeComputeApplication`| Dependency (`..>`) | $1 \rightarrow \text{many}$ | Installs server applications onto nodes |

---

## 6. Software Engineering & API Integrity Checklist

1. **Header Guards & Namespaces:** All files reside strictly inside `namespace ns3` and use standard `#ifndef FUTURENET_EDGE_CLASS_NAME_H` guards.
2. **Doxygen Documentation (QG-06):** Every class, public method, attribute, and trace source is documented with standard Doxygen tags (`@brief`, `@param`, `@return`).
3. **TraceSource Signatures:** Callback typedefs use exact standard types (`uint64_t`, `uint32_t`, `ns3::Time`).
4. **Const-Correctness:** All getters and inspection methods are declared `const`.
5. **No Core ns-3 Modifications (CG-15):** All classes are strictly contained within `contrib/futurenet-edge/`.

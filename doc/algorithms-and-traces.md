# FutureNet-Edge (Module M06) Algorithms, Wire Framing, and Trace Reference

**Module:** `futurenet-edge` (Module M06 — Edge Computing and Task-Offloading Model)  
**Lead Author:** Teammate 2 (Networking, Protocol Framing, & Scale Lead)  
**Environment:** `ns-allinone-3.46.1` / `ns-3.46.1`  
**Compliance:** M06 SRS Section 6 & 7, Common Guidelines CG-08, CG-11.

---

## 1. Non-Preemptive Queue Scheduling & Execution Engine

Execution time on any compute node is strictly non-preemptive. Once execution of task $i$ begins at $t_{\\text{exec\\_start}}$, it monopolizes the allocated processing capacity $F$ until $t_{\\text{exec\\_end}}$:

$$T_{\\text{compute}} = t_{\\text{exec\\_end}} - t_{\\text{exec\\_start}} = \\frac{C_i}{F}$$

Where $C_i$ is `RequiredCycles` and $F$ is `ComputeCapacity` (cycles/s).

### $O(1)$ Queue Scheduling Pseudocode
```text
Algorithm 1: O(1) Non-Preemptive Queue Management
Input: TaskDescriptor task, Capacity F, MaxQueuedTasks L_max
State: std::deque<ComputeQueueEntry> queue, uint64_t totalQueuedCycles, EventId activeTimer

Procedure SubmitTask(task, onCompleteCallback):
    if queue.size() >= L_max then:
        return false // Reject task (FAILED_QUEUE_FULL)
    
    totalQueuedCycles += task.RequiredCycles
    queue.push_back(task, onCompleteCallback, Simulator::Now())
    
    if not activeTimer.IsPending() then:
        ScheduleNextTask()
    return true

Procedure ScheduleNextTask():
    if queue.empty() then:
        return
    
    head = queue.front()
    totalQueuedCycles -= head.task.RequiredCycles
    duration = Seconds(head.task.RequiredCycles / F)
    activeTimer = Simulator::Schedule(duration, &OnTaskComplete, head)

Procedure OnTaskComplete(entry):
    queue.pop_front()
    entry.callback(entry.task, waitTime, computeTime)
    ScheduleNextTask()
```

---

## 2. 64-Byte Application Wire Header Layout (`TaskPacketHeader`)

To transmit tasks up to 1 GB over TCP and UDP without exceeding buffer limits or datagram limits, `futurenet-edge` uses a fixed 64-byte binary wire header serialized via `ns3::Header`:

| Field Name | C++ Type | Offset (Bytes) | Size | Semantic Purpose |
| :--- | :--- | :---: | :---: | :--- |
| `packetType` | `uint8_t` | 0 | 1 B | `1 = TASK_REQUEST`, `2 = TASK_RESULT`, `3 = TASK_ACK_ONLY` |
| `reserved` | `uint8_t[3]` | 1 | 3 B | 4-byte struct alignment padding |
| `sourceNodeId` | `uint32_t` | 4 | 4 B | Client origin node ID |
| `targetNodeId` | `uint32_t` | 8 | 4 B | Destination edge server node ID |
| `chunkBytes` | `uint32_t` | 12 | 4 B | Payload bytes in this specific segment (max 1400 B for UDP) |
| `taskId` | `uint64_t` | 16 | 8 B | Globally unique task identifier |
| `inputBytes` | `uint64_t` | 24 | 8 B | Total required payload size before execution is admitted |
| `outputBytes` | `uint64_t` | 32 | 8 B | Total result payload size to return to client |
| `requiredCycles` | `double` | 40 | 8 B | Computation workload volume (cycles) |
| `queueDurationNs` | `int64_t` | 48 | 8 B | Populated by server on reply: remote queue wait time (ns) |
| `computeDurationNs` | `int64_t` | 56 | 8 B | Populated by server on reply: remote CPU compute time (ns) |
| **Total** | | | **64 Bytes** | **Serialized and deserialized via `TaskPacketHeader`** |

---

## 3. Offloading Decision Policies

### Lowest Estimated Completion Time Policy (M06-FR-10)

For each candidate target $j \\in \\mathcal{K}$ (including local node and remote edge nodes), the client calculates estimated latency:

$$\\hat{T}_{\\text{e2e}, j} = \\hat{T}_{\\text{tx}, j} + \\hat{T}_{\\text{wait}, j} + \\hat{T}_{\\text{compute}, j} + \\hat{T}_{\\text{rx}, j}$$

Where:
- $\\hat{T}_{\\text{tx}, j} = \\frac{\\text{TaskInputBytes} \\times 8}{R_j} + D_j$ ($R_j$ = bandwidth bps, $D_j$ = one-way delay). For local execution, $\\hat{T}_{\\text{tx}} = 0$.
- $\\hat{T}_{\\text{wait}, j} = \\frac{\\text{QueuedCycles}_j + \\text{RemainingActiveCycles}_j}{F_j}$
- $\\hat{T}_{\\text{compute}, j} = \\frac{\\text{RequiredCycles}}{F_j}$
- $\\hat{T}_{\\text{rx}, j} = \\frac{\\text{TaskOutputBytes} \\times 8}{R_j} + D_j$ (0 for `AckOnly` or local execution).

The policy selects target $j^* = \\arg\\min_j \\hat{T}_{\\text{e2e}, j}$.

---

## 4. Complete 7 ns-3 `TraceSource` Reference

| # | TraceSource Name | Hosting Class | Callback Signature | Firing Trigger Condition |
| :--- | :--- | :--- | :--- | :--- |
| **1** | `TaskCreated` | `TaskClientApplication` | `void(uint64_t taskId, uint32_t sourceId)` | Fired immediately when a new task is instantiated. |
| **2** | `TaskOffloaded` | `TaskClientApplication` | `void(uint64_t taskId, uint32_t sourceId, uint32_t targetId)` | Fired when offload policy selects a remote edge server. |
| **3** | `TaskQueued` | `ComputeNodeModel` | `void(uint64_t taskId, uint32_t targetId, uint32_t queueLength)` | Fired when a task enters the waiting execution queue. |
| **4** | `TaskExecutionStart`| `ComputeNodeModel` | `void(uint64_t taskId, uint32_t targetId)` | Fired when the task reaches head of queue and CPU starts. |
| **5** | `TaskExecutionComplete`| `ComputeNodeModel`| `void(uint64_t taskId, uint32_t targetId)` | Fired when CPU execution finishes ($T = C/F$). |
| **6** | `TaskCompleted` | `TaskClientApplication` | `void(uint64_t taskId, Time e2eTime)` | Fired when client receives result or finishes local task. |
| **7** | `TaskDeadlineMiss` | `TaskClientApplication` | `void(uint64_t taskId, Time lateness)` | Fired when completion time exceeds `TaskDeadline`. |

### Trace Connection Example
```cpp
void OnTaskCompletedTrace(uint64_t taskId, Time e2eTime)
{
    std::cout << "Task " << taskId << " completed in " << e2eTime.GetMilliSeconds() << " ms\n";
}

// Connecting in ns-3 simulation script:
clientApp->TraceConnectWithoutContext("TaskCompleted", MakeCallback(&OnTaskCompletedTrace));
```

/**
 * \file compute-node-model.cc
 * \brief Implementation of ComputeNodeModel for M06 futurenet-edge module.
 * \author FutureNet Module Team
 */

#include "compute-node-model.h"

#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FutureNetComputeNodeModel");
NS_OBJECT_ENSURE_REGISTERED(ComputeNodeModel);

TypeId
ComputeNodeModel::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::ComputeNodeModel")
            .SetParent<Object>()
            .SetGroupName("FutureNet")
            .AddConstructor<ComputeNodeModel>()
            .AddAttribute("ComputeCapacity",
                          "Compute processing capacity in abstract cycles per second.",
                          DoubleValue(1e9),
                          MakeDoubleAccessor(&ComputeNodeModel::SetComputeCapacity,
                                             &ComputeNodeModel::GetComputeCapacity),
                          MakeDoubleChecker<double>(1e7, 1e14))
            .AddTraceSource("TaskQueued",
                            "Fired when a task is admitted to the execution queue.",
                            MakeTraceSourceAccessor(&ComputeNodeModel::m_taskQueuedTrace),
                            "ns3::TracedCallback::Uint64Uint32Uint32")
            .AddTraceSource("TaskExecutionStart",
                            "Fired when CPU execution begins for a task.",
                            MakeTraceSourceAccessor(&ComputeNodeModel::m_taskExecutionStartTrace),
                            "ns3::TracedCallback::Uint64Uint32")
            .AddTraceSource("TaskExecutionComplete",
                            "Fired when CPU execution finishes for a task.",
                            MakeTraceSourceAccessor(&ComputeNodeModel::m_taskExecutionCompleteTrace),
                            "ns3::TracedCallback::Uint64Uint32");
    return tid;
}

ComputeNodeModel::ComputeNodeModel()
    : m_computeCapacity(1e9),
      m_isExecuting(false),
      m_activeTaskStartTime(Seconds(0)),
      m_activeTaskExpectedDuration(Seconds(0))
{
    NS_LOG_FUNCTION(this);
    m_queue = CreateObject<ComputeQueue>();
}

ComputeNodeModel::~ComputeNodeModel()
{
    NS_LOG_FUNCTION(this);
}

void
ComputeNodeModel::DoDispose()
{
    NS_LOG_FUNCTION(this);
    if (m_currentExecutionEvent.IsPending())
    {
        Simulator::Cancel(m_currentExecutionEvent);
    }
    m_queue = nullptr;
    Object::DoDispose();
}

void
ComputeNodeModel::SetComputeCapacity(double cyclesPerSecond)
{
    NS_LOG_FUNCTION(this << cyclesPerSecond);
    m_computeCapacity = cyclesPerSecond;
}

double
ComputeNodeModel::GetComputeCapacity() const
{
    return m_computeCapacity;
}

uint32_t
ComputeNodeModel::GetQueueLength() const
{
    return m_queue ? m_queue->GetQueueLength() : 0;
}

Ptr<ComputeQueue>
ComputeNodeModel::GetQueue() const
{
    return m_queue;
}

Time
ComputeNodeModel::EstimateCompletion(const TaskDescriptor& task) const
{
    NS_LOG_FUNCTION(this << task.GetTaskId());

    // 1. Time remaining on the currently running task (if any)
    Time remainingActive = Seconds(0);
    if (m_isExecuting)
    {
        Time elapsed = Simulator::Now() - m_activeTaskStartTime;
        if (elapsed < m_activeTaskExpectedDuration)
        {
            remainingActive = m_activeTaskExpectedDuration - elapsed;
        }
    }

    // 2. Time to process all tasks already waiting in the queue
    Time queuedDuration = Seconds(0);
    if (m_queue && m_computeCapacity > 0.0)
    {
        queuedDuration = Seconds(m_queue->GetTotalQueuedCycles() / m_computeCapacity);
    }

    // 3. Time to process the new task itself
    Time taskDuration = Seconds(0);
    if (m_computeCapacity > 0.0)
    {
        taskDuration = Seconds(task.GetRequiredCycles() / m_computeCapacity);
    }

    return remainingActive + queuedDuration + taskDuration;
}

bool
ComputeNodeModel::SubmitTask(const TaskDescriptor& task,
                             Callback<void, const TaskDescriptor&, Time, Time> onComplete)
{
    NS_LOG_FUNCTION(this << task.GetTaskId());

    ComputeQueueEntry entry;
    entry.task = task;
    entry.timeQueued = Simulator::Now();
    entry.onCompleteCallback = onComplete;

    // Attempt to admit the task into the execution queue
    if (!m_queue || !m_queue->Enqueue(entry))
    {
        NS_LOG_WARN("Task " << task.GetTaskId() << " rejected: ComputeQueue is full");
        return false;
    }

    // Determine node ID for tracing (safe fallback if not attached to a Node)
    Ptr<Node> node = GetObject<Node>();
    uint32_t nodeId = node ? node->GetId() : 0;

    m_taskQueuedTrace(task.GetTaskId(), nodeId, m_queue->GetQueueLength());

    // If the CPU is currently idle, start executing the next task right away
    if (!m_isExecuting)
    {
        ProcessNextTask();
    }

    return true;
}

void
ComputeNodeModel::ProcessNextTask()
{
    NS_LOG_FUNCTION(this);

    if (m_isExecuting || !m_queue || m_queue->IsEmpty())
    {
        return;
    }

    if (!m_queue->Dequeue(m_currentRunningTask))
    {
        return;
    }

    m_isExecuting = true;
    m_currentRunningTask.timeExecStart = Simulator::Now();

    // M06-FR-04: Non-preemptive execution duration = RequiredCycles / Capacity
    double execSeconds = m_currentRunningTask.task.GetRequiredCycles() / m_computeCapacity;
    m_activeTaskStartTime = Simulator::Now();
    m_activeTaskExpectedDuration = Seconds(execSeconds);

    Ptr<Node> node = GetObject<Node>();
    uint32_t nodeId = node ? node->GetId() : 0;
    m_taskExecutionStartTrace(m_currentRunningTask.task.GetTaskId(), nodeId);

    NS_LOG_INFO("Started CPU execution for task " << m_currentRunningTask.task.GetTaskId()
                                                  << " on node " << nodeId
                                                  << " for " << execSeconds << " s");

    // Schedule the completion event with the ns-3 event simulator
    m_currentExecutionEvent = Simulator::Schedule(
        m_activeTaskExpectedDuration,
        &ComputeNodeModel::OnTaskExecutionFinished,
        this);
}

void
ComputeNodeModel::OnTaskExecutionFinished()
{
    NS_LOG_FUNCTION(this);

    // Calculate exact queue delay and compute execution time
    Time queueWaitTime = m_currentRunningTask.timeExecStart - m_currentRunningTask.timeQueued;
    Time computeTime = Simulator::Now() - m_currentRunningTask.timeExecStart;

    Ptr<Node> node = GetObject<Node>();
    uint32_t nodeId = node ? node->GetId() : 0;
    m_taskExecutionCompleteTrace(m_currentRunningTask.task.GetTaskId(), nodeId);

    NS_LOG_INFO("Finished task " << m_currentRunningTask.task.GetTaskId()
                                 << ": queueWaitTime=" << queueWaitTime.GetSeconds() << "s"
                                 << ", computeTime=" << computeTime.GetSeconds() << "s");

    // Copy the entry so the callback receives valid references before we advance
    ComputeQueueEntry completed = m_currentRunningTask;
    m_isExecuting = false;

    if (!completed.onCompleteCallback.IsNull())
    {
        completed.onCompleteCallback(completed.task, queueWaitTime, computeTime);
    }

    // If there are more tasks waiting on the queue, pick up the next one
    if (m_queue && !m_queue->IsEmpty())
    {
        ProcessNextTask();
    }
}

void
ComputeNodeModel::CancelTask(uint64_t taskId)
{
    NS_LOG_FUNCTION(this << taskId);

    // If the task is currently on the CPU, abort it immediately
    if (m_isExecuting && m_currentRunningTask.task.GetTaskId() == taskId)
    {
        Simulator::Cancel(m_currentExecutionEvent);
        m_isExecuting = false;
        NS_LOG_INFO("Cancelled actively running task " << taskId);
        if (m_queue && !m_queue->IsEmpty())
        {
            ProcessNextTask();
        }
        return;
    }

    // Otherwise, remove it from the waiting queue
    if (m_queue)
    {
        m_queue->RemoveTask(taskId);
    }
}

NetworkState
ComputeNodeModel::GetNetworkState() const
{
    NS_LOG_FUNCTION(this);

    NetworkState state;
    state.timestamp = Simulator::Now();
    Ptr<Node> node = GetObject<Node>();
    state.nodeId = node ? node->GetId() : 0;
    state.queuePackets = GetQueueLength();

    if (m_queue && m_queue->GetMaxQueueLength() > 0)
    {
        state.nodeLoad =
            std::min(1.0, static_cast<double>(GetQueueLength()) / m_queue->GetMaxQueueLength());
    }
    return state;
}

} // namespace ns3

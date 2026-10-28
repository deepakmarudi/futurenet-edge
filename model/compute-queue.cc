/**
 * @file compute-queue.cc
 * @brief Implementation of ComputeQueue for M06 futurenet-edge module.
 * @author FutureNet Module Team
 */

#include "compute-queue.h"

#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FutureNetComputeQueue");
NS_OBJECT_ENSURE_REGISTERED(ComputeQueue);

TypeId
ComputeQueue::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::ComputeQueue")
            .SetParent<Object>()
            .SetGroupName("FutureNet")
            .AddConstructor<ComputeQueue>()
            .AddAttribute("MaxQueuedTasks",
                          "Maximum number of tasks permitted in queue before dropping.",
                          UintegerValue(100),
                          MakeUintegerAccessor(&ComputeQueue::m_maxQueuedTasks),
                          MakeUintegerChecker<uint32_t>(1, 100000))
            .AddAttribute("ComputeScheduling",
                          "Task scheduling discipline (FCFS, SJF, EDF).",
                          StringValue("FCFS"),
                          MakeStringAccessor(&ComputeQueue::m_scheduling),
                          MakeStringChecker());
    return tid;
}

ComputeQueue::ComputeQueue()
    : m_maxQueuedTasks(100),
      m_scheduling("FCFS"),
      m_totalQueuedCycles(0.0)
{
    NS_LOG_FUNCTION(this);
}

bool
ComputeQueue::Enqueue(const ComputeQueueEntry& entry)
{
    NS_LOG_FUNCTION(this << entry.task.GetTaskId());

    // M06-FR-03: Enforce queue capacity limit (MaxQueuedTasks)
    if (m_queue.size() >= m_maxQueuedTasks)
    {
        NS_LOG_WARN("ComputeQueue full (size=" << m_queue.size()
                                               << ", max=" << m_maxQueuedTasks
                                               << "). Dropping task "
                                               << entry.task.GetTaskId());
        return false;
    }

    if (m_scheduling == "SJF")
    {
        // Shortest Job First: ascending order of requiredCycles
        auto it = std::upper_bound(
            m_queue.begin(),
            m_queue.end(),
            entry,
            [](const ComputeQueueEntry& a, const ComputeQueueEntry& b) {
                return a.task.GetRequiredCycles() < b.task.GetRequiredCycles();
            });
        m_queue.insert(it, entry);
    }
    else if (m_scheduling == "EDF")
    {
        // Earliest Deadline First: tasks with deadlines prioritized by earliest deadline
        auto it = std::upper_bound(
            m_queue.begin(),
            m_queue.end(),
            entry,
            [](const ComputeQueueEntry& a, const ComputeQueueEntry& b) {
                if (a.task.HasDeadline() && b.task.HasDeadline())
                {
                    return a.task.GetDeadline() < b.task.GetDeadline();
                }
                return a.task.HasDeadline() && !b.task.HasDeadline();
            });
        m_queue.insert(it, entry);
    }
    else
    {
        // Default: FCFS (First-Come, First-Served)
        m_queue.push_back(entry);
    }

    m_totalQueuedCycles += entry.task.GetRequiredCycles();

    NS_LOG_DEBUG("Enqueued task " << entry.task.GetTaskId()
                                  << " (scheduling=" << m_scheduling << ")"
                                  << ", queue length=" << m_queue.size()
                                  << ", totalQueuedCycles=" << m_totalQueuedCycles);
    return true;
}

bool
ComputeQueue::Dequeue(ComputeQueueEntry& outEntry)
{
    NS_LOG_FUNCTION(this);

    if (m_queue.empty())
    {
        return false;
    }

    outEntry = m_queue.front();
    m_queue.pop_front();

    // Maintain O(1) cycle tracking (prevent floating point underflow below 0)
    m_totalQueuedCycles = std::max(0.0, m_totalQueuedCycles - outEntry.task.GetRequiredCycles());

    NS_LOG_DEBUG("Dequeued task " << outEntry.task.GetTaskId()
                                  << ", remaining queue length=" << m_queue.size()
                                  << ", totalQueuedCycles=" << m_totalQueuedCycles);
    return true;
}

bool
ComputeQueue::Peek(ComputeQueueEntry& outEntry) const
{
    NS_LOG_FUNCTION(this);

    if (m_queue.empty())
    {
        return false;
    }

    outEntry = m_queue.front();
    return true;
}

uint32_t
ComputeQueue::GetQueueLength() const
{
    return static_cast<uint32_t>(m_queue.size());
}

uint32_t
ComputeQueue::GetMaxQueueLength() const
{
    return m_maxQueuedTasks;
}

void
ComputeQueue::SetMaxQueueLength(uint32_t maxQueue)
{
    NS_LOG_FUNCTION(this << maxQueue);
    m_maxQueuedTasks = maxQueue;
}

void
ComputeQueue::SetScheduling(std::string scheduling)
{
    NS_LOG_FUNCTION(this << scheduling);
    m_scheduling = scheduling;
}

std::string
ComputeQueue::GetScheduling() const
{
    return m_scheduling;
}

double
ComputeQueue::GetTotalQueuedCycles() const
{
    return m_totalQueuedCycles;
}

bool
ComputeQueue::IsFull() const
{
    return m_queue.size() >= m_maxQueuedTasks;
}

bool
ComputeQueue::IsEmpty() const
{
    return m_queue.empty();
}

bool
ComputeQueue::RemoveTask(uint64_t taskId)
{
    NS_LOG_FUNCTION(this << taskId);

    for (auto it = m_queue.begin(); it != m_queue.end(); ++it)
    {
        if (it->task.GetTaskId() == taskId)
        {
            m_totalQueuedCycles = std::max(0.0, m_totalQueuedCycles - it->task.GetRequiredCycles());
            m_queue.erase(it);
            NS_LOG_INFO("Cancelled/removed task " << taskId << " from queue");
            return true;
        }
    }
    return false;
}

} // namespace ns3

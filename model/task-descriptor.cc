/**
 * @file task-descriptor.cc
 * @brief Implementation of TaskDescriptor for M06 futurenet-edge module.
 * @author FutureNet Module Team
 */

#include "task-descriptor.h"

#include "ns3/log.h"

#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FutureNetTaskDescriptor");

TaskDescriptor::TaskDescriptor()
    : m_taskId(0),
      m_sourceNodeId(0),
      m_inputBytes(0),
      m_outputBytes(0),
      m_outputRatio(-1.0),
      m_useOutputRatio(false),
      m_requiredCycles(0.0),
      m_creationTime(Seconds(0)),
      m_deadline(Seconds(0)),
      m_hasDeadline(false)
{
    NS_LOG_FUNCTION(this);
}

TaskDescriptor::TaskDescriptor(uint64_t taskId,
                               uint32_t sourceNodeId,
                               uint64_t inputBytes,
                               uint64_t outputBytes,
                               double requiredCycles,
                               Time creationTime)
    : m_taskId(taskId),
      m_sourceNodeId(sourceNodeId),
      m_inputBytes(inputBytes),
      m_outputBytes(outputBytes),
      m_outputRatio(-1.0),
      m_useOutputRatio(false),
      m_requiredCycles(requiredCycles),
      m_creationTime(creationTime),
      m_deadline(Seconds(0)),
      m_hasDeadline(false)
{
    NS_LOG_FUNCTION(this << taskId << sourceNodeId << inputBytes << outputBytes
                         << requiredCycles << creationTime.GetSeconds());
}

uint64_t
TaskDescriptor::GetTaskId() const
{
    return m_taskId;
}

uint32_t
TaskDescriptor::GetSourceNodeId() const
{
    return m_sourceNodeId;
}

uint64_t
TaskDescriptor::GetInputBytes() const
{
    return m_inputBytes;
}

uint64_t
TaskDescriptor::GetOutputBytes() const
{
    // M06-FR-01: If outputRatio is configured (>= 0.0), derive dynamically from inputBytes
    if (m_useOutputRatio && m_outputRatio >= 0.0)
    {
        return static_cast<uint64_t>(std::round(static_cast<double>(m_inputBytes) * m_outputRatio));
    }
    return m_outputBytes;
}

double
TaskDescriptor::GetOutputRatio() const
{
    return m_outputRatio;
}

bool
TaskDescriptor::HasOutputRatio() const
{
    return m_useOutputRatio;
}

double
TaskDescriptor::GetRequiredCycles() const
{
    return m_requiredCycles;
}

Time
TaskDescriptor::GetCreationTime() const
{
    return m_creationTime;
}

Time
TaskDescriptor::GetDeadline() const
{
    return m_deadline;
}

bool
TaskDescriptor::HasDeadline() const
{
    return m_hasDeadline;
}

void
TaskDescriptor::SetTaskId(uint64_t taskId)
{
    m_taskId = taskId;
}

void
TaskDescriptor::SetSourceNodeId(uint32_t sourceNodeId)
{
    m_sourceNodeId = sourceNodeId;
}

void
TaskDescriptor::SetInputBytes(uint64_t inputBytes)
{
    m_inputBytes = inputBytes;
}

void
TaskDescriptor::SetOutputBytes(uint64_t outputBytes)
{
    m_outputBytes = outputBytes;
    m_useOutputRatio = false; // Explicit output bytes override ratio
}

void
TaskDescriptor::SetOutputRatio(double outputRatio)
{
    m_outputRatio = outputRatio;
    m_useOutputRatio = (outputRatio >= 0.0);
}

void
TaskDescriptor::ClearOutputRatio()
{
    m_outputRatio = -1.0;
    m_useOutputRatio = false;
}

void
TaskDescriptor::SetRequiredCycles(double requiredCycles)
{
    m_requiredCycles = requiredCycles;
}

void
TaskDescriptor::SetCreationTime(Time creationTime)
{
    m_creationTime = creationTime;
}

void
TaskDescriptor::SetDeadline(Time deadline)
{
    m_deadline = deadline;
    m_hasDeadline = (deadline > Seconds(0));
}

void
TaskDescriptor::ClearDeadline()
{
    m_deadline = Seconds(0);
    m_hasDeadline = false;
}

FutureNetTrafficClass
TaskDescriptor::ToTrafficClass(uint8_t priority, double importance) const
{
    FutureNetTrafficClass tc;
    tc.priority = priority;
    tc.importance = importance;
    tc.deadline = m_deadline;
    tc.flowId = static_cast<uint32_t>(m_taskId & 0xFFFFFFFF);
    return tc;
}

} // namespace ns3

/**
 * \file task-packet-header.cc
 * \brief Serialization and accessors for the futurenet-edge wire header.
 */

#include "task-packet-header.h"

#include "ns3/abort.h"
#include "ns3/buffer.h"
#include "ns3/type-id.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace ns3
{

namespace
{

constexpr uint32_t SERIALIZED_SIZE = 64;
constexpr uint32_t MAX_CHUNK_BYTES = 1400;

uint64_t
EncodeDouble(double value)
{
    static_assert(sizeof(double) == sizeof(uint64_t), "TaskPacketHeader requires 64-bit doubles");
    static_assert(std::numeric_limits<double>::is_iec559,
                  "TaskPacketHeader requires IEEE-754 doubles");

    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double
DecodeDouble(uint64_t bits)
{
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

int64_t
DecodeInt64(uint64_t bits)
{
    if (bits <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    {
        return static_cast<int64_t>(bits);
    }

    // Convert the two's-complement wire representation without an
    // implementation-defined unsigned-to-signed conversion.
    return -1 - static_cast<int64_t>(~bits);
}

} // namespace

NS_OBJECT_ENSURE_REGISTERED(TaskPacketHeader);

TaskPacketHeader::TaskPacketHeader() = default;

TypeId
TaskPacketHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::TaskPacketHeader")
                            .SetParent<Header>()
                            .SetGroupName("FutureNet")
                            .AddConstructor<TaskPacketHeader>();
    return tid;
}

TypeId
TaskPacketHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
TaskPacketHeader::Serialize(Buffer::Iterator start) const
{
    start.WriteU8(m_packetType);
    start.WriteU8(0);
    start.WriteU8(0);
    start.WriteU8(0);
    start.WriteHtonU32(m_sourceNodeId);
    start.WriteHtonU32(m_targetNodeId);
    start.WriteHtonU32(m_chunkBytes);
    start.WriteHtonU64(m_taskId);
    start.WriteHtonU64(m_inputBytes);
    start.WriteHtonU64(m_outputBytes);
    start.WriteHtonU64(EncodeDouble(m_requiredCycles));
    start.WriteHtonU64(static_cast<uint64_t>(m_queueDurationNs));
    start.WriteHtonU64(static_cast<uint64_t>(m_computeDurationNs));
}

uint32_t
TaskPacketHeader::Deserialize(Buffer::Iterator start)
{
    m_packetType = start.ReadU8();
    m_reserved[0] = start.ReadU8();
    m_reserved[1] = start.ReadU8();
    m_reserved[2] = start.ReadU8();
    m_sourceNodeId = start.ReadNtohU32();
    m_targetNodeId = start.ReadNtohU32();
    m_chunkBytes = start.ReadNtohU32();
    m_taskId = start.ReadNtohU64();
    m_inputBytes = start.ReadNtohU64();
    m_outputBytes = start.ReadNtohU64();
    m_requiredCycles = DecodeDouble(start.ReadNtohU64());
    m_queueDurationNs = DecodeInt64(start.ReadNtohU64());
    m_computeDurationNs = DecodeInt64(start.ReadNtohU64());
    return SERIALIZED_SIZE;
}

uint32_t
TaskPacketHeader::GetSerializedSize() const
{
    return SERIALIZED_SIZE;
}

void
TaskPacketHeader::Print(std::ostream& os) const
{
    os << "type=" << static_cast<uint32_t>(m_packetType)
       << " source=" << m_sourceNodeId << " target=" << m_targetNodeId
       << " chunkBytes=" << m_chunkBytes << " taskId=" << m_taskId
       << " inputBytes=" << m_inputBytes << " outputBytes=" << m_outputBytes
       << " requiredCycles=" << m_requiredCycles
       << " queueDuration=" << NanoSeconds(m_queueDurationNs).GetSeconds() << "s"
       << " computeDuration=" << NanoSeconds(m_computeDurationNs).GetSeconds() << "s";
}

void
TaskPacketHeader::SetPacketType(uint8_t packetType)
{
    NS_ABORT_MSG_IF(packetType < TASK_REQUEST || packetType > TASK_ACK_ONLY,
                    "Invalid TaskPacketHeader packet type " << static_cast<uint32_t>(packetType));
    m_packetType = packetType;
}

uint8_t
TaskPacketHeader::GetPacketType() const
{
    return m_packetType;
}

void
TaskPacketHeader::SetSourceNodeId(uint32_t sourceNodeId)
{
    m_sourceNodeId = sourceNodeId;
}

uint32_t
TaskPacketHeader::GetSourceNodeId() const
{
    return m_sourceNodeId;
}

void
TaskPacketHeader::SetTargetNodeId(uint32_t targetNodeId)
{
    m_targetNodeId = targetNodeId;
}

uint32_t
TaskPacketHeader::GetTargetNodeId() const
{
    return m_targetNodeId;
}

void
TaskPacketHeader::SetChunkBytes(uint32_t chunkBytes)
{
    NS_ABORT_MSG_IF(chunkBytes > MAX_CHUNK_BYTES,
                    "TaskPacketHeader chunk exceeds the 1400-byte payload limit");
    m_chunkBytes = chunkBytes;
}

uint32_t
TaskPacketHeader::GetChunkBytes() const
{
    return m_chunkBytes;
}

void
TaskPacketHeader::SetTaskId(uint64_t taskId)
{
    m_taskId = taskId;
}

uint64_t
TaskPacketHeader::GetTaskId() const
{
    return m_taskId;
}

void
TaskPacketHeader::SetInputBytes(uint64_t inputBytes)
{
    m_inputBytes = inputBytes;
}

uint64_t
TaskPacketHeader::GetInputBytes() const
{
    return m_inputBytes;
}

void
TaskPacketHeader::SetOutputBytes(uint64_t outputBytes)
{
    m_outputBytes = outputBytes;
}

uint64_t
TaskPacketHeader::GetOutputBytes() const
{
    return m_outputBytes;
}

void
TaskPacketHeader::SetRequiredCycles(double requiredCycles)
{
    NS_ABORT_MSG_IF(!std::isfinite(requiredCycles) || requiredCycles < 0.0,
                    "Required cycles must be a finite, non-negative value");
    m_requiredCycles = requiredCycles;
}

double
TaskPacketHeader::GetRequiredCycles() const
{
    return m_requiredCycles;
}

void
TaskPacketHeader::SetQueueDuration(Time duration)
{
    NS_ABORT_MSG_IF(duration < Seconds(0), "Queue duration cannot be negative");
    m_queueDurationNs = duration.GetNanoSeconds();
}

Time
TaskPacketHeader::GetQueueDuration() const
{
    return NanoSeconds(m_queueDurationNs);
}

void
TaskPacketHeader::SetComputeDuration(Time duration)
{
    NS_ABORT_MSG_IF(duration < Seconds(0), "Compute duration cannot be negative");
    m_computeDurationNs = duration.GetNanoSeconds();
}

Time
TaskPacketHeader::GetComputeDuration() const
{
    return NanoSeconds(m_computeDurationNs);
}

} // namespace ns3

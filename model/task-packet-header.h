/**
 * @file task-packet-header.h
 * @brief 64-Byte Application Wire Header for M06 futurenet-edge module.
 * @author FutureNet Module Team
 *
 * Implements a fixed 64-byte binary header for framing task requests,
 * computation results, and completion acknowledgments across ns-3 TCP and UDP sockets.
 */

#ifndef FUTURENET_TASK_PACKET_HEADER_H
#define FUTURENET_TASK_PACKET_HEADER_H

#include "ns3/header.h"
#include "ns3/nstime.h"

#include <cstdint>
#include <iostream>

namespace ns3
{

/**
 * @ingroup futurenet-edge
 * @brief Fixed 64-byte application wire header for edge task transmission.
 *
 * Packet layout (exact 64 bytes):
 * - Byte 0: PacketType (1=REQUEST, 2=RESULT, 3=ACK_ONLY)
 * - Bytes 1-3: Reserved / alignment padding (set to 0)
 * - Bytes 4-7: SourceNodeId (uint32_t)
 * - Bytes 8-11: TargetNodeId (uint32_t)
 * - Bytes 12-15: ChunkBytes (uint32_t, payload size <= 1400 bytes)
 * - Bytes 16-23: TaskId (uint64_t)
 * - Bytes 24-31: InputBytes (uint64_t, total required upload size)
 * - Bytes 32-39: OutputBytes (uint64_t, total result download size)
 * - Bytes 40-47: RequiredCycles (double, 64-bit IEEE 754 float)
 * - Bytes 48-55: QueueDurationNs (int64_t, nanoseconds spent in server queue)
 * - Bytes 56-63: ComputeDurationNs (int64_t, nanoseconds spent in CPU execution)
 */
class TaskPacketHeader : public Header
{
  public:
    static constexpr uint8_t TASK_REQUEST = 1;  //!< Client upload request
    static constexpr uint8_t TASK_RESULT = 2;   //!< Server result download
    static constexpr uint8_t TASK_ACK_ONLY = 3; //!< Server completion acknowledgment header

    TaskPacketHeader();
    ~TaskPacketHeader() override = default;

    /**
     * @brief Register this type with the ns-3 TypeId system.
     * @return TypeId instance.
     */
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;

    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    uint32_t GetSerializedSize() const override;
    void Print(std::ostream& os) const override;

    // Field accessors
    void SetPacketType(uint8_t packetType);
    uint8_t GetPacketType() const;

    void SetSourceNodeId(uint32_t sourceNodeId);
    uint32_t GetSourceNodeId() const;

    void SetTargetNodeId(uint32_t targetNodeId);
    uint32_t GetTargetNodeId() const;

    void SetChunkBytes(uint32_t chunkBytes);
    uint32_t GetChunkBytes() const;

    void SetTaskId(uint64_t taskId);
    uint64_t GetTaskId() const;

    void SetInputBytes(uint64_t inputBytes);
    uint64_t GetInputBytes() const;

    void SetOutputBytes(uint64_t outputBytes);
    uint64_t GetOutputBytes() const;

    void SetRequiredCycles(double requiredCycles);
    double GetRequiredCycles() const;

    void SetQueueDuration(Time duration);
    Time GetQueueDuration() const;

    void SetComputeDuration(Time duration);
    Time GetComputeDuration() const;

  private:
    uint8_t m_packetType{TASK_REQUEST}; //!< Header type discriminator
    uint8_t m_reserved[3]{0, 0, 0};     //!< 3-byte struct padding
    uint32_t m_sourceNodeId{0};         //!< Client node ID
    uint32_t m_targetNodeId{0};         //!< Destination server node ID
    uint32_t m_chunkBytes{0};           //!< Segment payload bytes
    uint64_t m_taskId{0};               //!< Unique task identifier
    uint64_t m_inputBytes{0};           //!< Total input volume in bytes
    uint64_t m_outputBytes{0};          //!< Total result volume in bytes
    double m_requiredCycles{0.0};       //!< Required computational demand
    int64_t m_queueDurationNs{0};       //!< Remote server queue duration in ns
    int64_t m_computeDurationNs{0};     //!< Remote server compute service duration in ns
};

} // namespace ns3

#endif // FUTURENET_TASK_PACKET_HEADER_H

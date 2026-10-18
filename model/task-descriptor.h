/**
 * \file task-descriptor.h
 * \brief TaskDescriptor and TaskStatus definition for M06 futurenet-edge module.
 * \author FutureNet Module Team
 *
 * Implements M06-FR-01: Encapsulates task ID, source node ID, input bytes,
 * output bytes or output ratio, required compute cycles, creation time,
 * and optional deadline.
 */

#ifndef FUTURENET_TASK_DESCRIPTOR_H
#define FUTURENET_TASK_DESCRIPTOR_H

#include "futurenet-common-types.h"
#include "ns3/nstime.h"

#include <cstdint>
#include <iostream>

namespace ns3
{

/**
 * \ingroup futurenet-edge
 * \brief Lifecycle states of an edge computational task.
 */
enum class TaskStatus
{
    CREATED,           //!< Task instantiated by generator
    OFFLOADING_INPUT,  //!< Task input bytes traversing network
    QUEUED,            //!< Task payload received and waiting in execution queue
    EXECUTING,         //!< Task currently being processed by CPU
    RETURNING_RESULT,  //!< Task result or ACK traversing network back to client
    COMPLETED,         //!< Task completed end-to-end
    FAILED_QUEUE_FULL, //!< Dropped due to execution queue capacity exceeded
    FAILED_NETWORK,    //!< Network transfer failed or connection aborted
    CANCELLED          //!< Explicitly cancelled before completion
};

/**
 * \ingroup futurenet-edge
 * \brief Abstract computational task descriptor (M06-FR-01).
 *
 * Encapsulates the workload characteristics of a discrete compute job without
 * embedding a real OS or language runtime. Supports deterministic output payload
 * calculation via fixed bytes or a dynamic input-to-output ratio.
 */
class TaskDescriptor
{
  public:
    /**
     * \brief Default constructor initializing all fields to safe zero defaults.
     */
    TaskDescriptor();

    /**
     * \brief Parameterized constructor.
     * \param taskId Unique task identifier.
     * \param sourceNodeId Node ID generating this task.
     * \param inputBytes Payload size in bytes to be transferred to compute node.
     * \param outputBytes Result size in bytes to be returned to client.
     * \param requiredCycles Computational demand in abstract CPU cycles.
     * \param creationTime Simulation time when task was instantiated.
     */
    TaskDescriptor(uint64_t taskId,
                   uint32_t sourceNodeId,
                   uint64_t inputBytes,
                   uint64_t outputBytes,
                   double requiredCycles,
                   Time creationTime = Seconds(0));

    // Getters
    uint64_t GetTaskId() const;
    uint32_t GetSourceNodeId() const;
    uint64_t GetInputBytes() const;

    /**
     * \brief Derives the output result payload size in bytes (M06-FR-01).
     * If an output ratio was configured (m_outputRatio >= 0.0), evaluates
     * static_cast<uint64_t>(m_inputBytes * m_outputRatio). Otherwise returns m_outputBytes.
     * \return Size in bytes of output result payload.
     */
    uint64_t GetOutputBytes() const;

    double GetOutputRatio() const;
    bool HasOutputRatio() const;
    double GetRequiredCycles() const;
    Time GetCreationTime() const;
    Time GetDeadline() const;
    bool HasDeadline() const;

    // Setters
    void SetTaskId(uint64_t taskId);
    void SetSourceNodeId(uint32_t sourceNodeId);
    void SetInputBytes(uint64_t inputBytes);
    void SetOutputBytes(uint64_t outputBytes);
    void SetOutputRatio(double outputRatio);
    void ClearOutputRatio();
    void SetRequiredCycles(double requiredCycles);
    void SetCreationTime(Time creationTime);
    void SetDeadline(Time deadline);
    void ClearDeadline();

    /**
     * \brief Converts this task's metadata into a FutureNetTrafficClass struct (CG Section 4.4).
     * \param priority Traffic priority level (0-7, default: 0).
     * \param importance Normalized importance metric [0, 1].
     * \return Standard FutureNetTrafficClass metadata.
     */
    FutureNetTrafficClass ToTrafficClass(uint8_t priority = 0, double importance = 1.0) const;

  private:
    uint64_t m_taskId{0};            //!< Unique simulation-wide task identifier
    uint32_t m_sourceNodeId{0};      //!< Originating client node ID
    uint64_t m_inputBytes{0};        //!< Upload data volume in bytes
    uint64_t m_outputBytes{0};       //!< Download result data volume in bytes
    double m_outputRatio{-1.0};      //!< Dynamic ratio of input bytes (disabled if < 0.0)
    bool m_useOutputRatio{false};    //!< Flag enabling dynamic output ratio derivation
    double m_requiredCycles{0.0};    //!< Processing demand in abstract CPU cycles
    Time m_creationTime{Seconds(0)}; //!< Creation timestamp
    Time m_deadline{Seconds(0)};     //!< Completion deadline timestamp
    bool m_hasDeadline{false};       //!< Flag indicating if deadline was specified
};

} // namespace ns3

#endif // FUTURENET_TASK_DESCRIPTOR_H

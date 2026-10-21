/**
 * \file compute-node-model.h
 * \brief ComputeNodeModel definition for M06 futurenet-edge module.
 * \author FutureNet Module Team
 *
 * Implements M06-FR-02, M06-FR-03, M06-FR-04: Models node compute capacity
 * in abstract cycles/s, execution queue, non-preemptive task scheduling,
 * and completion estimation.
 */

#ifndef FUTURENET_COMPUTE_NODE_MODEL_H
#define FUTURENET_COMPUTE_NODE_MODEL_H

#include "compute-queue.h"
#include "futurenet-common-types.h"
#include "task-descriptor.h"

#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/traced-callback.h"

namespace ns3
{

/**
 * \ingroup futurenet-edge
 * \brief Represents the computational processing capability of an ns-3 node.
 *
 * Manages an execution queue and schedules non-preemptive CPU execution
 * events based on task cycles and node compute capacity (cycles/s).
 */
class ComputeNodeModel : public Object
{
  public:
    /**
     * \brief Register this type with the ns-3 TypeId system.
     * \return TypeId instance.
     */
    static TypeId GetTypeId();

    ComputeNodeModel();
    ~ComputeNodeModel() override;

    /**
     * \brief Sets the compute processing rate.
     * \param cyclesPerSecond Rate in abstract cycles per second.
     */
    void SetComputeCapacity(double cyclesPerSecond);

    /**
     * \brief Gets the compute processing rate.
     * \return Capacity in cycles/s.
     */
    double GetComputeCapacity() const;

    /**
     * \brief Gets the current number of tasks waiting in queue.
     * \return Current queue length.
     */
    uint32_t GetQueueLength() const;

    /**
     * \brief Returns the underlying ComputeQueue object.
     * \return Pointer to ComputeQueue.
     */
    Ptr<ComputeQueue> GetQueue() const;

    /**
     * \brief Estimates total duration from submission until completion of task.
     * Evaluates: RemainingActiveTime + (QueuedCycles / Capacity) + (TaskCycles / Capacity).
     * \param task Task descriptor to estimate.
     * \return Estimated completion delay.
     */
    Time EstimateCompletion(const TaskDescriptor& task) const;

    /**
     * \brief Submits a task for execution on this compute node.
     * \param task Task metadata.
     * \param onComplete Callback invoked when CPU execution completes (task, waitTime, computeTime).
     * \return true if task was admitted; false if queue is full.
     */
    bool SubmitTask(const TaskDescriptor& task,
                    Callback<void, const TaskDescriptor&, Time, Time> onComplete);

    /**
     * \brief Cancels a task by ID (purges from queue or cancels active execution).
     * \param taskId Unique task ID.
     */
    void CancelTask(uint64_t taskId);

    /**
     * \brief Exports the node's current utilization and queue state as NetworkState (CG Section 4.1).
     * \return NetworkState telemetry snapshot.
     */
    NetworkState GetNetworkState() const;

  protected:
    void DoDispose() override;

  private:
    /**
     * \brief Pops next task from queue and schedules CPU execution event.
     */
    void ProcessNextTask();

    /**
     * \brief Called when current task's CPU cycles finish.
     */
    void OnTaskExecutionFinished();

    double m_computeCapacity{1e9};               //!< Processing capacity in cycles/s
    Ptr<ComputeQueue> m_queue;                   //!< Managed execution queue
    bool m_isExecuting{false};                   //!< True if CPU is actively running a task
    ComputeQueueEntry m_currentRunningTask;      //!< Currently executing task entry
    EventId m_currentExecutionEvent;             //!< Scheduled CPU completion event
    Time m_activeTaskStartTime{Seconds(0)};      //!< Simulation time when active task started
    Time m_activeTaskExpectedDuration{Seconds(0)};//!< Expected service duration of active task

    // Trace sources (M06-FR-09)
    TracedCallback<uint64_t, uint32_t, uint32_t> m_taskQueuedTrace;
    TracedCallback<uint64_t, uint32_t> m_taskExecutionStartTrace;
    TracedCallback<uint64_t, uint32_t> m_taskExecutionCompleteTrace;
};

} // namespace ns3

#endif // FUTURENET_COMPUTE_NODE_MODEL_H

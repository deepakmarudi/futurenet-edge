/**
 * @file compute-queue.h
 * @brief ComputeQueue and ComputeQueueEntry definitions for M06 futurenet-edge module.
 * @author FutureNet Module Team
 *
 * Implements M06-FR-03: Execution queue maintenance with configurable admission limit
 * (MaxQueuedTasks) and O(1) running cycle tracking (CG Section 11).
 */

#ifndef FUTURENET_COMPUTE_QUEUE_H
#define FUTURENET_COMPUTE_QUEUE_H

#include "task-descriptor.h"

#include "ns3/callback.h"
#include "ns3/nstime.h"
#include "ns3/object.h"

#include <deque>
#include <string>

namespace ns3
{

/**
 * @ingroup futurenet-edge
 * @brief Single entry held inside the ComputeQueue.
 */
struct ComputeQueueEntry
{
    TaskDescriptor task;            //!< The computational task descriptor
    Time timeQueued{Seconds(0)};    //!< Simulation time when entry was admitted to queue
    Time timeExecStart{Seconds(0)}; //!< Simulation time when CPU execution started
    Callback<void, const TaskDescriptor&, Time, Time>
        onCompleteCallback; //!< Completion callback (task, queueWaitTime, computeTime)
};

/**
 * @ingroup futurenet-edge
 * @brief Execution queue for compute nodes (M06-FR-03).
 *
 * Manages waiting tasks using an underlying std::deque to ensure strictly O(1)
 * enqueue and dequeue operations. Tracks cumulative queued cycles in O(1) time
 * to support instant wait-time estimation without traversing elements.
 */
class ComputeQueue : public Object
{
  public:
    /**
     * @brief Register this type with the ns-3 TypeId system.
     * @return TypeId instance.
     */
    static TypeId GetTypeId();

    ComputeQueue();
    ~ComputeQueue() override = default;

    /**
     * @brief Enqueue a task entry at the tail of the queue.
     * @param entry The entry to be queued.
     * @return true if admitted; false if queue is full (m_queue.size() >= m_maxQueuedTasks).
     */
    bool Enqueue(const ComputeQueueEntry& entry);

    /**
     * @brief Dequeue the head entry from the queue.
     * @param outEntry Reference populated with the popped entry.
     * @return true if an entry was popped; false if queue was empty.
     */
    bool Dequeue(ComputeQueueEntry& outEntry);

    /**
     * @brief Inspect the head entry without removing it.
     * @param outEntry Reference populated with head entry.
     * @return true if head exists; false if empty.
     */
    bool Peek(ComputeQueueEntry& outEntry) const;

    /**
     * @brief Current number of waiting tasks in the queue.
     * @return Queue length.
     */
    uint32_t GetQueueLength() const;

    /**
     * @brief Maximum permitted queue length before task admission drops.
     * @return Configured MaxQueuedTasks capacity.
     */
    uint32_t GetMaxQueueLength() const;

    /**
     * @brief Sets the maximum queued task limit.
     * @param maxQueue New maximum limit.
     */
    void SetMaxQueueLength(uint32_t maxQueue);

    /**
     * @brief Running total of computational cycles across all currently waiting tasks.
     * @return Cumulative cycles in queue.
     */
    double GetTotalQueuedCycles() const;

    /**
     * @brief Checks if the queue has reached capacity.
     * @return true if queue length >= maxQueueLength.
     */
    bool IsFull() const;

    /**
     * @brief Checks if the queue is empty.
     * @return true if queue length == 0.
     */
    bool IsEmpty() const;

    /**
     * @brief Removes a task from the queue by task ID (M06 CancelTask support).
     * @param taskId Unique task ID to remove.
     * @return true if found and removed; false otherwise.
     */
    bool RemoveTask(uint64_t taskId);

  private:
    std::deque<ComputeQueueEntry> m_queue; //!< Underlying FIFO double-ended buffer
    uint32_t m_maxQueuedTasks{100};        //!< Maximum queue length (MaxQueuedTasks attribute)
    std::string m_scheduling{"FCFS"};      //!< Scheduling policy (ComputeScheduling attribute)
    double m_totalQueuedCycles{0.0};       //!< Running sum of requiredCycles in queue
};

} // namespace ns3

#endif // FUTURENET_COMPUTE_QUEUE_H

/**
 * @file offload-policy.h
 * @brief OffloadPolicy interface and concrete selection policies for M06 futurenet-edge module.
 * @author FutureNet Module Team
 *
 * Implements M06-FR-08 and M06-FR-10: Mechanism/policy separation (CG Section 1.3),
 * LocalOnlyPolicy baseline, NearestEdgePolicy, and LowestEstimatedCompletionTimePolicy.
 */

#ifndef FUTURENET_OFFLOAD_POLICY_H
#define FUTURENET_OFFLOAD_POLICY_H

#include "task-descriptor.h"

#include "ns3/address.h"
#include "ns3/nstime.h"
#include "ns3/object.h"

#include <vector>

namespace ns3
{

/**
 * @ingroup futurenet-edge
 * @brief Telemetry snapshot of a candidate compute node for policy evaluation.
 */
struct ComputeNodeState
{
    uint32_t nodeId{0};                    //!< Candidate node identifier
    bool isLocal{false};                   //!< True if this represents the local client node
    Address serverAddress;                 //!< IP address and port of the remote EdgeComputeApplication
    double computeCapacityCyclesPerS{0.0}; //!< Processing rate in cycles/s
    uint32_t queueLength{0};               //!< Current number of tasks in execution queue
    uint32_t maxQueueLength{100};          //!< Queue admission limit
    Time estimatedWaitTime{Seconds(0)};    //!< Estimated delay until queue empties
    double estimatedDataRateMbps{100.0};   //!< Estimated transmission bandwidth in Mbit/s
    double estimatedOneWayDelayMs{5.0};    //!< Estimated physical propagation latency in ms
    double distanceMeters{0.0};            //!< Euclidean distance to client in meters
};

/**
 * @ingroup futurenet-edge
 * @brief Decision output returned by an OffloadPolicy.
 */
struct OffloadDecision
{
    bool executeLocally{true}; //!< True = run locally; False = offload to remote server
    uint32_t targetNodeId{0};  //!< Selected target node ID (equals sourceNodeId if local)

    OffloadDecision() = default;
    OffloadDecision(bool local, uint32_t target)
        : executeLocally(local),
          targetNodeId(target)
    {
    }
};

/**
 * @ingroup futurenet-edge
 * @brief Abstract base interface for task offloading policies (CG Section 4.6).
 */
class OffloadPolicy : public Object
{
  public:
    /**
     * @brief Register this type with the ns-3 TypeId system.
     * @return TypeId instance.
     */
    static TypeId GetTypeId();

    OffloadPolicy() = default;
    ~OffloadPolicy() override = default;

    /**
     * @brief Selects an execution target for the given task across candidate nodes.
     * @param task Workload descriptor.
     * @param candidates Candidate compute nodes (including local node).
     * @return OffloadDecision specifying local vs remote and target node ID.
     */
    virtual OffloadDecision Select(const TaskDescriptor& task,
                                   const std::vector<ComputeNodeState>& candidates) const = 0;
};

/**
 * @ingroup futurenet-edge
 * @brief Deterministic baseline policy that always executes locally (M06-FR-08).
 */
class LocalOnlyPolicy : public OffloadPolicy
{
  public:
    static TypeId GetTypeId();

    LocalOnlyPolicy() = default;
    ~LocalOnlyPolicy() override = default;

    OffloadDecision Select(const TaskDescriptor& task,
                           const std::vector<ComputeNodeState>& candidates) const override;
};

/**
 * @ingroup futurenet-edge
 * @brief Proximity-aware policy that offloads to the nearest available edge node (M06-FR-08).
 * Selects candidate with min(distanceMeters) or min(propagationDelay) subject to queue space.
 */
class NearestEdgePolicy : public OffloadPolicy
{
  public:
    static TypeId GetTypeId();

    NearestEdgePolicy() = default;
    ~NearestEdgePolicy() override = default;

    OffloadDecision Select(const TaskDescriptor& task,
                           const std::vector<ComputeNodeState>& candidates) const override;
};

/**
 * @ingroup futurenet-edge
 * @brief Latency-optimal policy minimizing estimated end-to-end completion time (M06-FR-10).
 * Evaluates: argmin(T_tx_in + T_queue + T_compute + T_tx_out) across all candidates.
 */
class LowestEstimatedCompletionTimePolicy : public OffloadPolicy
{
  public:
    static TypeId GetTypeId();

    LowestEstimatedCompletionTimePolicy() = default;
    ~LowestEstimatedCompletionTimePolicy() override = default;

    OffloadDecision Select(const TaskDescriptor& task,
                           const std::vector<ComputeNodeState>& candidates) const override;
};

} // namespace ns3

#endif // FUTURENET_OFFLOAD_POLICY_H

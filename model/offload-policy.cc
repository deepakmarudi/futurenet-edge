/**
 * \file offload-policy.cc
 * \brief Implementation of offloading policies for M06 futurenet-edge module.
 * \author FutureNet Module Team
 *
 * Implements M06-FR-08 and M06-FR-10: LocalOnlyPolicy baseline, NearestEdgePolicy,
 * and LowestEstimatedCompletionTimePolicy.
 */

#include "offload-policy.h"

#include "ns3/log.h"

#include <limits>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OffloadPolicy");

// ============================================================================
// OffloadPolicy Base Class
// ============================================================================

TypeId
OffloadPolicy::GetTypeId()
{
    static TypeId tid = TypeId("ns3::OffloadPolicy")
                            .SetParent<Object>()
                            .SetGroupName("FuturenetEdge");
    return tid;
}

// ============================================================================
// Rule 1: LocalOnlyPolicy (Always execute on local client node)
// ============================================================================

TypeId
LocalOnlyPolicy::GetTypeId()
{
    static TypeId tid = TypeId("ns3::LocalOnlyPolicy")
                            .SetParent<OffloadPolicy>()
                            .SetGroupName("FuturenetEdge")
                            .AddConstructor<LocalOnlyPolicy>();
    return tid;
}

OffloadDecision
LocalOnlyPolicy::Select(const TaskDescriptor& task,
                        const std::vector<ComputeNodeState>& candidates) const
{
    NS_LOG_FUNCTION(this << task.GetTaskId() << candidates.size());

    // Locate the local node ID from candidates or default to task source
    uint32_t localNodeId = task.GetSourceNodeId();
    for (const auto& candidate : candidates)
    {
        if (candidate.isLocal)
        {
            localNodeId = candidate.nodeId;
            break;
        }
    }

    NS_LOG_INFO("LocalOnlyPolicy: Task " << task.GetTaskId()
                                         << " assigned to local node " << localNodeId);
    return OffloadDecision(true, localNodeId);
}

// ============================================================================
// Rule 2: NearestEdgePolicy (Offload to closest server with queue space)
// ============================================================================

TypeId
NearestEdgePolicy::GetTypeId()
{
    static TypeId tid = TypeId("ns3::NearestEdgePolicy")
                            .SetParent<OffloadPolicy>()
                            .SetGroupName("FuturenetEdge")
                            .AddConstructor<NearestEdgePolicy>();
    return tid;
}

OffloadDecision
NearestEdgePolicy::Select(const TaskDescriptor& task,
                          const std::vector<ComputeNodeState>& candidates) const
{
    NS_LOG_FUNCTION(this << task.GetTaskId() << candidates.size());

    const ComputeNodeState* bestCandidate = nullptr;
    double minMetric = std::numeric_limits<double>::max();
    const ComputeNodeState* localCandidate = nullptr;

    for (const auto& candidate : candidates)
    {
        if (candidate.isLocal)
        {
            localCandidate = &candidate;
            continue;
        }

        // Admission check: skip candidate if queue is full
        if (candidate.queueLength >= candidate.maxQueueLength)
        {
            continue;
        }

        // Proximity metric: prioritize physical distance in meters; fallback to delay
        double metric = candidate.distanceMeters > 0.0 ? candidate.distanceMeters
                                                       : candidate.estimatedOneWayDelayMs;

        if (metric < minMetric)
        {
            minMetric = metric;
            bestCandidate = &candidate;
        }
    }

    // Offload if an available remote edge server exists
    if (bestCandidate != nullptr)
    {
        NS_LOG_INFO("NearestEdgePolicy: Task " << task.GetTaskId()
                                               << " offloaded to edge node "
                                               << bestCandidate->nodeId);
        return OffloadDecision(false, bestCandidate->nodeId);
    }

    // Fallback to local execution if all remote servers are congested
    uint32_t localId = localCandidate ? localCandidate->nodeId : task.GetSourceNodeId();
    NS_LOG_WARN("NearestEdgePolicy: No remote edge server available. Falling back to local node "
                << localId);
    return OffloadDecision(true, localId);
}

// ============================================================================
// Rule 3: LowestEstimatedCompletionTimePolicy (Latency-Optimal Policy)
// ============================================================================

TypeId
LowestEstimatedCompletionTimePolicy::GetTypeId()
{
    static TypeId tid = TypeId("ns3::LowestEstimatedCompletionTimePolicy")
                            .SetParent<OffloadPolicy>()
                            .SetGroupName("FuturenetEdge")
                            .AddConstructor<LowestEstimatedCompletionTimePolicy>();
    return tid;
}

OffloadDecision
LowestEstimatedCompletionTimePolicy::Select(const TaskDescriptor& task,
                                           const std::vector<ComputeNodeState>& candidates) const
{
    NS_LOG_FUNCTION(this << task.GetTaskId() << candidates.size());

    const ComputeNodeState* bestCandidate = nullptr;
    double minTotalTimeSec = std::numeric_limits<double>::max();
    const ComputeNodeState* localCandidate = nullptr;

    double requiredCycles = task.GetRequiredCycles();
    uint64_t inputBytes = task.GetInputBytes();
    uint64_t outputBytes = task.GetOutputBytes();

    for (const auto& candidate : candidates)
    {
        if (candidate.isLocal)
        {
            localCandidate = &candidate;
        }

        // Skip candidate if queue capacity exceeded
        if (candidate.queueLength >= candidate.maxQueueLength)
        {
            continue;
        }

        // Prevent invalid compute rates
        if (candidate.computeCapacityCyclesPerS <= 0.0)
        {
            continue;
        }

        // Execution time on CPU: Cycles / ComputeCapacity
        double computeTimeSec = requiredCycles / candidate.computeCapacityCyclesPerS;

        // Waiting time in queue
        double queueWaitSec = candidate.estimatedWaitTime.GetSeconds();

        // Network communication delay (Input upload + Output download)
        double txInSec = 0.0;
        double txOutSec = 0.0;

        if (!candidate.isLocal)
        {
            double dataRateBps = candidate.estimatedDataRateMbps * 1e6;
            if (dataRateBps > 0.0)
            {
                txInSec = (static_cast<double>(inputBytes) * 8.0) / dataRateBps;
                txOutSec = (static_cast<double>(outputBytes) * 8.0) / dataRateBps;
            }
            double oneWayDelaySec = candidate.estimatedOneWayDelayMs / 1000.0;
            txInSec += oneWayDelaySec;
            txOutSec += oneWayDelaySec;
        }

        double totalEstimatedSec = txInSec + queueWaitSec + computeTimeSec + txOutSec;

        if (totalEstimatedSec < minTotalTimeSec)
        {
            minTotalTimeSec = totalEstimatedSec;
            bestCandidate = &candidate;
        }
    }

    if (bestCandidate != nullptr)
    {
        NS_LOG_INFO("LowestEstimatedCompletionTimePolicy: Task " << task.GetTaskId()
                    << " assigned to node " << bestCandidate->nodeId
                    << " (estimated total: " << minTotalTimeSec << "s)");
        return OffloadDecision(bestCandidate->isLocal, bestCandidate->nodeId);
    }

    // Fallback to local execution if all queues full
    uint32_t fallbackId = localCandidate ? localCandidate->nodeId : task.GetSourceNodeId();
    NS_LOG_WARN("LowestEstimatedCompletionTimePolicy: All candidates full. Falling back to local node "
                << fallbackId);
    return OffloadDecision(true, fallbackId);
}

} // namespace ns3
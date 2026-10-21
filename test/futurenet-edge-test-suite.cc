/**
 * \file futurenet-edge-test-suite.cc
 * \brief Automated test suite for M06 futurenet-edge module.
 * \author FutureNet Module Team
 *
 * Implements Test T04: LocalOnlyPolicy verification (M06 SRS Section 9).
 */

#include "offload-policy.h"
#include "task-descriptor.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/test.h"

#include <vector>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FuturenetEdgeTestSuite");

/**
 * \ingroup futurenet-edge-test
 * \brief Test Case T04: Verification of LocalOnlyPolicy (M06-FR-08).
 *
 * Verifies that LocalOnlyPolicy strictly selects local execution (executeLocally == true)
 * and targets the client node ID, even when remote edge servers with vastly superior
 * compute capacities are available.
 */
class LocalOnlyPolicyTestCase : public TestCase
{
  public:
    LocalOnlyPolicyTestCase();
    ~LocalOnlyPolicyTestCase() override = default;

  private:
    void DoRun() override;
};

LocalOnlyPolicyTestCase::LocalOnlyPolicyTestCase()
    : TestCase("T04: Verify LocalOnlyPolicy strictly selects local execution")
{
}

void
LocalOnlyPolicyTestCase::DoRun()
{
    // 1. Create a task originating from client node 10
    TaskDescriptor task(1, 10, 1000000, 100000, 1e9, Seconds(1.0));

    // 2. Set up candidate compute nodes:
    // Candidate 1: Local node (slow phone: 1 Billion cycles/s)
    ComputeNodeState localNode;
    localNode.nodeId = 10;
    localNode.isLocal = true;
    localNode.computeCapacityCyclesPerS = 1e9;
    localNode.queueLength = 0;
    localNode.maxQueueLength = 100;
    localNode.estimatedWaitTime = Seconds(0);
    localNode.distanceMeters = 0.0;

    // Candidate 2: Edge server (10x faster: 10 Billion cycles/s)
    ComputeNodeState edgeNode;
    edgeNode.nodeId = 20;
    edgeNode.isLocal = false;
    edgeNode.computeCapacityCyclesPerS = 1e10;
    edgeNode.queueLength = 0;
    edgeNode.maxQueueLength = 100;
    edgeNode.estimatedWaitTime = Seconds(0);
    edgeNode.distanceMeters = 15.0;
    edgeNode.estimatedDataRateMbps = 100.0;
    edgeNode.estimatedOneWayDelayMs = 2.0;

    // Candidate 3: Cloud server (100x faster: 100 Billion cycles/s)
    ComputeNodeState cloudNode;
    cloudNode.nodeId = 30;
    cloudNode.isLocal = false;
    cloudNode.computeCapacityCyclesPerS = 1e11;
    cloudNode.queueLength = 0;
    cloudNode.maxQueueLength = 100;
    cloudNode.estimatedWaitTime = Seconds(0);
    cloudNode.distanceMeters = 500.0;
    cloudNode.estimatedDataRateMbps = 50.0;
    cloudNode.estimatedOneWayDelayMs = 20.0;

    std::vector<ComputeNodeState> candidates = {localNode, edgeNode, cloudNode};

    // 3. Run the policy
    Ptr<LocalOnlyPolicy> policy = CreateObject<LocalOnlyPolicy>();
    OffloadDecision decision = policy->Select(task, candidates);

    // 4. Check if Rule 1 worked
    NS_TEST_ASSERT_MSG_EQ(decision.executeLocally,
                          true,
                          "T04 Failed: LocalOnlyPolicy must select local execution!");
    NS_TEST_ASSERT_MSG_EQ(decision.targetNodeId,
                          10,
                          "T04 Failed: LocalOnlyPolicy must target local node ID 10!");
}

/**
 * \ingroup futurenet-edge-test
 * \brief Main Test Suite registration for futurenet-edge.
 */
class FuturenetEdgeTestSuite : public TestSuite
{
  public:
    FuturenetEdgeTestSuite();
};

FuturenetEdgeTestSuite::FuturenetEdgeTestSuite()
    : TestSuite("futurenet-edge", Type::UNIT)
{
    // Add TM3's Test T04
    AddTestCase(new LocalOnlyPolicyTestCase, TestCase::Duration::QUICK);
}

static FuturenetEdgeTestSuite g_futurenetEdgeTestSuite;

} // namespace ns3
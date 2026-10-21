/**
 * \file futurenet-edge-test-suite.cc
 * \brief Automated Unit Test Suite for futurenet-edge (Module M06).
 * \author FutureNet Module Team
 */

#include "ns3/compute-node-model.h"
#include "ns3/compute-queue.h"
#include "ns3/double.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/task-descriptor.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"

#include <vector>

namespace ns3
{

// ===========================================================================
// Test T01: Verify T = C / F execution duration in empty queue (M06-FR-04)
// ===========================================================================
class FutureNetEdgeT01TestCase : public TestCase
{
  public:
    FutureNetEdgeT01TestCase();
    void DoRun() override;

  private:
    void OnTaskDone(const TaskDescriptor& task, Time waitTime, Time computeTime);

    bool m_completed{false};
    Time m_measuredWait{Seconds(0)};
    Time m_measuredCompute{Seconds(0)};
};

FutureNetEdgeT01TestCase::FutureNetEdgeT01TestCase()
    : TestCase("T01: Compute capacity execution duration (T = C / F)")
{
}

void
FutureNetEdgeT01TestCase::OnTaskDone(const TaskDescriptor& task, Time waitTime, Time computeTime)
{
    m_completed = true;
    m_measuredWait = waitTime;
    m_measuredCompute = computeTime;
}

void
FutureNetEdgeT01TestCase::DoRun()
{
    // Scenario 1: Capacity = 1e9 cycles/s, Demand = 1e9 cycles -> Expected: 1.0 s
    {
        m_completed = false;
        Ptr<ComputeNodeModel> node = CreateObject<ComputeNodeModel>();
        node->SetComputeCapacity(1e9);

        TaskDescriptor task(1, 0, 1000, 100, 1e9, Simulator::Now());
        bool admitted = node->SubmitTask(
            task,
            MakeCallback(&FutureNetEdgeT01TestCase::OnTaskDone, this));

        NS_TEST_ASSERT_MSG_EQ(admitted, true, "Task 1 should be admitted to empty node");

        Simulator::Run();
        Simulator::Destroy();

        NS_TEST_ASSERT_MSG_EQ(m_completed, true, "Task 1 must complete");
        NS_TEST_ASSERT_MSG_EQ(m_measuredWait, Seconds(0), "Empty queue wait time must be 0");
        NS_TEST_ASSERT_MSG_EQ_TOL(m_measuredCompute.GetSeconds(), 1.0, 1e-6,
                                  "Compute time for 1e9 cycles at 1e9 cycles/s must be 1.0 s");
    }

    // Scenario 2: Capacity = 1e10 cycles/s, Demand = 1e9 cycles -> Expected: 0.1 s
    {
        m_completed = false;
        Ptr<ComputeNodeModel> node = CreateObject<ComputeNodeModel>();
        node->SetComputeCapacity(1e10);

        TaskDescriptor task(2, 0, 1000, 100, 1e9, Simulator::Now());
        bool admitted = node->SubmitTask(
            task,
            MakeCallback(&FutureNetEdgeT01TestCase::OnTaskDone, this));

        NS_TEST_ASSERT_MSG_EQ(admitted, true, "Task 2 should be admitted to empty node");

        Simulator::Run();
        Simulator::Destroy();

        NS_TEST_ASSERT_MSG_EQ(m_completed, true, "Task 2 must complete");
        NS_TEST_ASSERT_MSG_EQ(m_measuredWait, Seconds(0), "Empty queue wait time must be 0");
        NS_TEST_ASSERT_MSG_EQ_TOL(m_measuredCompute.GetSeconds(), 0.1, 1e-6,
                                  "Compute time for 1e9 cycles at 1e10 cycles/s must be 0.1 s");
    }
}

// ===========================================================================
// Test T02: Verify FCFS ordering and queue wait time tracking (M06-FR-03/04)
// ===========================================================================
class FutureNetEdgeT02TestCase : public TestCase
{
  public:
    FutureNetEdgeT02TestCase();
    void DoRun() override;

  private:
    void OnTaskDone(const TaskDescriptor& task, Time waitTime, Time computeTime);

    struct TaskResult
    {
        uint64_t id;
        Time wait;
        Time compute;
    };
    std::vector<TaskResult> m_results;
};

FutureNetEdgeT02TestCase::FutureNetEdgeT02TestCase()
    : TestCase("T02: FCFS execution ordering and queue wait times")
{
}

void
FutureNetEdgeT02TestCase::OnTaskDone(const TaskDescriptor& task, Time waitTime, Time computeTime)
{
    m_results.push_back({task.GetTaskId(), waitTime, computeTime});
}

void
FutureNetEdgeT02TestCase::DoRun()
{
    m_results.clear();
    Ptr<ComputeNodeModel> node = CreateObject<ComputeNodeModel>();
    node->SetComputeCapacity(1e9); // 1 Giga-cycle per second

    // Submit 3 tasks back-to-back at t=0
    TaskDescriptor t1(101, 0, 1000, 100, 1e9, Simulator::Now());
    TaskDescriptor t2(102, 0, 1000, 100, 1e9, Simulator::Now());
    TaskDescriptor t3(103, 0, 1000, 100, 1e9, Simulator::Now());

    node->SubmitTask(t1, MakeCallback(&FutureNetEdgeT02TestCase::OnTaskDone, this));
    node->SubmitTask(t2, MakeCallback(&FutureNetEdgeT02TestCase::OnTaskDone, this));
    node->SubmitTask(t3, MakeCallback(&FutureNetEdgeT02TestCase::OnTaskDone, this));

    Simulator::Run();
    Simulator::Destroy();

    NS_TEST_ASSERT_MSG_EQ(m_results.size(), 3, "All 3 tasks must complete");

    // Verify strict FCFS sequence
    NS_TEST_ASSERT_MSG_EQ(m_results[0].id, 101, "First completed must be Task 1");
    NS_TEST_ASSERT_MSG_EQ(m_results[1].id, 102, "Second completed must be Task 2");
    NS_TEST_ASSERT_MSG_EQ(m_results[2].id, 103, "Third completed must be Task 3");

    // Verify exact queue delays
    NS_TEST_ASSERT_MSG_EQ_TOL(m_results[0].wait.GetSeconds(), 0.0, 1e-6, "Task 1 wait must be 0 s");
    NS_TEST_ASSERT_MSG_EQ_TOL(m_results[1].wait.GetSeconds(), 1.0, 1e-6, "Task 2 wait must be 1.0 s");
    NS_TEST_ASSERT_MSG_EQ_TOL(m_results[2].wait.GetSeconds(), 2.0, 1e-6, "Task 3 wait must be 2.0 s");

    // All compute durations must equal 1.0 s
    for (size_t i = 0; i < 3; ++i)
    {
        NS_TEST_ASSERT_MSG_EQ_TOL(m_results[i].compute.GetSeconds(), 1.0, 1e-6,
                                  "Compute duration must equal 1.0 s");
    }
}

// ===========================================================================
// Test T05: Verify queue admission limit (MaxQueuedTasks) (M06-FR-03)
// ===========================================================================
class FutureNetEdgeT05TestCase : public TestCase
{
  public:
    FutureNetEdgeT05TestCase();
    void DoRun() override;
};

FutureNetEdgeT05TestCase::FutureNetEdgeT05TestCase()
    : TestCase("T05: Queue admission limit handling (MaxQueuedTasks)")
{
}

void
FutureNetEdgeT05TestCase::DoRun()
{
    Ptr<ComputeNodeModel> node = CreateObject<ComputeNodeModel>();
    node->SetComputeCapacity(1e9);
    node->GetQueue()->SetMaxQueueLength(2); // Queue holds max 2 waiting tasks

    auto nullCb = MakeNullCallback<void, const TaskDescriptor&, Time, Time>();

    TaskDescriptor t1(1, 0, 1000, 100, 1e9, Simulator::Now());
    TaskDescriptor t2(2, 0, 1000, 100, 1e9, Simulator::Now());
    TaskDescriptor t3(3, 0, 1000, 100, 1e9, Simulator::Now());
    TaskDescriptor t4(4, 0, 1000, 100, 1e9, Simulator::Now());

    // Task 1: admitted and starts CPU execution immediately
    bool adm1 = node->SubmitTask(t1, nullCb);
    NS_TEST_ASSERT_MSG_EQ(adm1, true, "Task 1 must be admitted (CPU executing)");
    NS_TEST_ASSERT_MSG_EQ(node->GetQueueLength(), 0, "Queue length should be 0 (task is on CPU)");

    // Task 2: admitted into queue (1/2)
    bool adm2 = node->SubmitTask(t2, nullCb);
    NS_TEST_ASSERT_MSG_EQ(adm2, true, "Task 2 must be admitted into queue");
    NS_TEST_ASSERT_MSG_EQ(node->GetQueueLength(), 1, "Queue length should be 1");

    // Task 3: admitted into queue (2/2 - Queue is now FULL)
    bool adm3 = node->SubmitTask(t3, nullCb);
    NS_TEST_ASSERT_MSG_EQ(adm3, true, "Task 3 must be admitted into queue");
    NS_TEST_ASSERT_MSG_EQ(node->GetQueueLength(), 2, "Queue length should be 2 (Full)");

    // Task 4: MUST BE REJECTED because queue capacity is 2
    bool adm4 = node->SubmitTask(t4, nullCb);
    NS_TEST_ASSERT_MSG_EQ(adm4, false, "Task 4 must be REJECTED when queue is full");
    NS_TEST_ASSERT_MSG_EQ(node->GetQueueLength(), 2, "Queue length must not exceed 2");

    Simulator::Destroy();
}

// ===========================================================================
// Test CG-T1: Attribute registration and default values (CG-01, CG-11)
// ===========================================================================
class FutureNetEdgeCgT1TestCase : public TestCase
{
  public:
    FutureNetEdgeCgT1TestCase();
    void DoRun() override;
};

FutureNetEdgeCgT1TestCase::FutureNetEdgeCgT1TestCase()
    : TestCase("CG-T1: Attribute registration and defaults verification")
{
}

void
FutureNetEdgeCgT1TestCase::DoRun()
{
    // 1. Verify ComputeNodeModel defaults
    Ptr<ComputeNodeModel> node = CreateObject<ComputeNodeModel>();
    DoubleValue capacityVal;
    node->GetAttribute("ComputeCapacity", capacityVal);
    NS_TEST_ASSERT_MSG_EQ_TOL(capacityVal.Get(), 1e9, 1e-6,
                              "Default ComputeCapacity must be 1e9 cycles/s");

    // 2. Verify ComputeQueue defaults
    Ptr<ComputeQueue> queue = CreateObject<ComputeQueue>();
    UintegerValue maxQVal;
    queue->GetAttribute("MaxQueuedTasks", maxQVal);
    NS_TEST_ASSERT_MSG_EQ(maxQVal.Get(), 100, "Default MaxQueuedTasks must be 100");

    StringValue schedVal;
    queue->GetAttribute("ComputeScheduling", schedVal);
    NS_TEST_ASSERT_MSG_EQ(schedVal.Get(), "FCFS", "Default scheduling must be FCFS");

    // 3. Verify TaskDescriptor dynamic output ratio
    TaskDescriptor task;
    task.SetInputBytes(1000);
    task.SetOutputRatio(0.25);
    NS_TEST_ASSERT_MSG_EQ(task.GetOutputBytes(), 250,
                          "Output bytes must derive to 250 with 0.25 ratio on 1000 input bytes");
}

// ===========================================================================
// Test CG-T2: Attribute boundary and range enforcement (CG-11)
// ===========================================================================
class FutureNetEdgeCgT2TestCase : public TestCase
{
  public:
    FutureNetEdgeCgT2TestCase();
    void DoRun() override;
};

FutureNetEdgeCgT2TestCase::FutureNetEdgeCgT2TestCase()
    : TestCase("CG-T2: Attribute boundary and range enforcement")
{
}

void
FutureNetEdgeCgT2TestCase::DoRun()
{
    Ptr<ComputeNodeModel> node = CreateObject<ComputeNodeModel>();

    // ComputeCapacity range: [1e7, 1e14]
    bool belowRange = node->SetAttributeFailSafe("ComputeCapacity", DoubleValue(1e6));
    NS_TEST_ASSERT_MSG_EQ(belowRange, false, "ComputeCapacity < 1e7 must fail");

    bool aboveRange = node->SetAttributeFailSafe("ComputeCapacity", DoubleValue(1e15));
    NS_TEST_ASSERT_MSG_EQ(aboveRange, false, "ComputeCapacity > 1e14 must fail");

    bool validRange = node->SetAttributeFailSafe("ComputeCapacity", DoubleValue(1e10));
    NS_TEST_ASSERT_MSG_EQ(validRange, true, "Valid ComputeCapacity 1e10 must succeed");

    // MaxQueuedTasks range: [1, 100000]
    Ptr<ComputeQueue> queue = CreateObject<ComputeQueue>();
    bool zeroLimit = queue->SetAttributeFailSafe("MaxQueuedTasks", UintegerValue(0));
    NS_TEST_ASSERT_MSG_EQ(zeroLimit, false, "MaxQueuedTasks == 0 must fail");

    bool excessiveLimit = queue->SetAttributeFailSafe("MaxQueuedTasks", UintegerValue(200000));
    NS_TEST_ASSERT_MSG_EQ(excessiveLimit, false, "MaxQueuedTasks > 100000 must fail");

    bool validLimit = queue->SetAttributeFailSafe("MaxQueuedTasks", UintegerValue(500));
    NS_TEST_ASSERT_MSG_EQ(validLimit, true, "Valid MaxQueuedTasks 500 must succeed");
}

// ===========================================================================
// TestSuite definition: Bundles all test cases under "futurenet-edge"
// ===========================================================================
class FutureNetEdgeTestSuite : public TestSuite
{
  public:
    FutureNetEdgeTestSuite();
};

FutureNetEdgeTestSuite::FutureNetEdgeTestSuite()
    : TestSuite("futurenet-edge", Type::UNIT)
{
    AddTestCase(new FutureNetEdgeT01TestCase);
    AddTestCase(new FutureNetEdgeT02TestCase);
    AddTestCase(new FutureNetEdgeT05TestCase);
    AddTestCase(new FutureNetEdgeCgT1TestCase);
    AddTestCase(new FutureNetEdgeCgT2TestCase);
}

static FutureNetEdgeTestSuite g_futurenetEdgeTestSuite;

} // namespace ns3

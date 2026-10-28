/**
 * @file futurenet-edge-test-suite.cc
 * @brief Unified automated unit and integration test suite for futurenet-edge (Module M06).
 * @author FutureNet Module Team
 *
 * Implements all 10 project tests required by M06 SRS and Common Guidelines:
 * - T01: Compute capacity execution duration (T = C / F)
 * - T02: FCFS execution ordering and queue wait delays
 * - T03: Remote compute starts strictly after full payload arrival
 * - T04: LocalOnlyPolicy never offloads to remote candidate
 * - T05: Queue admission drop when MaxQueuedTasks is reached
 * - T06: TaskCompleted TraceSource emits correct taskId and valid duration
 * - CG-T1: Attribute registration and default values
 * - CG-T2: Attribute boundary and range enforcement
 * - CG-T5: Deterministic repeatability across identical RNG seeds
 * - CG-T6: Integration with standard ns-3 FlowMonitor
 */

#include "ns3/compute-node-model.h"
#include "ns3/compute-queue.h"
#include "ns3/double.h"
#include "ns3/edge-compute-application.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/log.h"
#include "ns3/net-device-container.h"
#include "ns3/nstime.h"
#include "ns3/offload-policy.h"
#include "ns3/packet.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/task-client-application.h"
#include "ns3/task-descriptor.h"
#include "ns3/task-packet-header.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"

#include <vector>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FuturenetEdgeTestSuite");

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
    : TestCase("T01: Compute capacity execution duration T equals C over F")
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
    node->SetComputeCapacity(1e9);

    TaskDescriptor t1(101, 0, 1000, 100, 1e9, Simulator::Now());
    TaskDescriptor t2(102, 0, 1000, 100, 1e9, Simulator::Now());
    TaskDescriptor t3(103, 0, 1000, 100, 1e9, Simulator::Now());

    node->SubmitTask(t1, MakeCallback(&FutureNetEdgeT02TestCase::OnTaskDone, this));
    node->SubmitTask(t2, MakeCallback(&FutureNetEdgeT02TestCase::OnTaskDone, this));
    node->SubmitTask(t3, MakeCallback(&FutureNetEdgeT02TestCase::OnTaskDone, this));

    Simulator::Run();
    Simulator::Destroy();

    NS_TEST_ASSERT_MSG_EQ(m_results.size(), 3, "All 3 tasks must complete");
    NS_TEST_ASSERT_MSG_EQ(m_results[0].id, 101, "First completed must be Task 1");
    NS_TEST_ASSERT_MSG_EQ(m_results[1].id, 102, "Second completed must be Task 2");
    NS_TEST_ASSERT_MSG_EQ(m_results[2].id, 103, "Third completed must be Task 3");

    NS_TEST_ASSERT_MSG_EQ_TOL(m_results[0].wait.GetSeconds(), 0.0, 1e-6, "Task 1 wait must be 0 s");
    NS_TEST_ASSERT_MSG_EQ_TOL(m_results[1].wait.GetSeconds(), 1.0, 1e-6, "Task 2 wait must be 1.0 s");
    NS_TEST_ASSERT_MSG_EQ_TOL(m_results[2].wait.GetSeconds(), 2.0, 1e-6, "Task 3 wait must be 2.0 s");

    for (size_t i = 0; i < 3; ++i)
    {
        NS_TEST_ASSERT_MSG_EQ_TOL(m_results[i].compute.GetSeconds(), 1.0, 1e-6,
                                  "Compute duration must equal 1.0 s");
    }
}

// ===========================================================================
// Test T03: Remote compute starts strictly after all task input arrives (M06-FR-06)
// ===========================================================================
class EdgeTaskInputGateTestCase : public TestCase
{
  public:
    EdgeTaskInputGateTestCase();
    ~EdgeTaskInputGateTestCase() override = default;

  private:
    void DoRun() override;
    void StartClientConnection();
    void OnConnect(Ptr<Socket> socket);
    void OnConnectFailure(Ptr<Socket> socket);
    void SendFinalChunk();
    void CheckBeforeFinalChunk();
    void CheckAfterFinalChunk();
    void OnExecutionStart(uint64_t taskId, uint32_t nodeId);
    void HandleClientRead(Ptr<Socket> socket);
    Ptr<Packet> MakeRequestChunk(uint32_t chunkBytes) const;

    Ptr<Socket> m_clientSocket;
    Address m_serverAddress;
    uint32_t m_clientNodeId{0};
    uint32_t m_serverNodeId{0};
    bool m_connected{false};
    bool m_executionStarted{false};
    bool m_startedBeforeFinalChunk{false};
    bool m_startedAfterFinalChunk{false};
    bool m_resultReceived{false};
};

EdgeTaskInputGateTestCase::EdgeTaskInputGateTestCase()
    : TestCase("T03: Remote compute starts strictly after full payload arrival")
{
}

Ptr<Packet>
EdgeTaskInputGateTestCase::MakeRequestChunk(uint32_t chunkBytes) const
{
    TaskPacketHeader header;
    header.SetPacketType(TaskPacketHeader::TASK_REQUEST);
    header.SetSourceNodeId(m_clientNodeId);
    header.SetTargetNodeId(m_serverNodeId);
    header.SetChunkBytes(chunkBytes);
    header.SetTaskId(1001);
    header.SetInputBytes(2800);
    header.SetOutputBytes(1000);
    header.SetRequiredCycles(1e6);

    Ptr<Packet> packet = Create<Packet>(chunkBytes);
    packet->AddHeader(header);
    return packet;
}

void
EdgeTaskInputGateTestCase::OnConnect(Ptr<Socket> socket)
{
    m_connected = true;
    const int sent = socket->Send(MakeRequestChunk(1400));
    NS_TEST_ASSERT_MSG_EQ(sent, 1464, "First chunk sent");
    Simulator::Schedule(MilliSeconds(50),
                        &EdgeTaskInputGateTestCase::CheckBeforeFinalChunk,
                        this);
    Simulator::Schedule(MilliSeconds(100), &EdgeTaskInputGateTestCase::SendFinalChunk, this);
}

void
EdgeTaskInputGateTestCase::StartClientConnection()
{
    NS_TEST_ASSERT_MSG_EQ(m_clientSocket->Connect(m_serverAddress), 0,
                          "TCP connection initiation failed");
}

void
EdgeTaskInputGateTestCase::OnConnectFailure(Ptr<Socket> socket)
{
    (void)socket;
}

void
EdgeTaskInputGateTestCase::CheckBeforeFinalChunk()
{
    m_startedBeforeFinalChunk = m_executionStarted;
}

void
EdgeTaskInputGateTestCase::SendFinalChunk()
{
    const int sent = m_clientSocket->Send(MakeRequestChunk(1400));
    NS_TEST_ASSERT_MSG_EQ(sent, 1464, "Final chunk sent");
    Simulator::Schedule(MilliSeconds(50),
                        &EdgeTaskInputGateTestCase::CheckAfterFinalChunk,
                        this);
}

void
EdgeTaskInputGateTestCase::CheckAfterFinalChunk()
{
    m_startedAfterFinalChunk = m_executionStarted;
}

void
EdgeTaskInputGateTestCase::OnExecutionStart(uint64_t taskId, uint32_t nodeId)
{
    if (taskId == 1001 && nodeId == m_serverNodeId)
    {
        m_executionStarted = true;
    }
}

void
EdgeTaskInputGateTestCase::HandleClientRead(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    while ((packet = socket->Recv()) != nullptr)
    {
        if (packet->GetSize() > 0)
        {
            m_resultReceived = true;
        }
    }
}

void
EdgeTaskInputGateTestCase::DoRun()
{
    NodeContainer nodes;
    nodes.Create(2);
    m_clientNodeId = nodes.Get(0)->GetId();
    m_serverNodeId = nodes.Get(1)->GetId();

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("1ms"));
    NetDeviceContainer devices = p2p.Install(nodes);

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper addresses;
    addresses.SetBase("10.77.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = addresses.Assign(devices);

    Ptr<ComputeNodeModel> compute = CreateObject<ComputeNodeModel>();
    compute->SetComputeCapacity(1e6);
    nodes.Get(1)->AggregateObject(compute);

    Ptr<EdgeComputeApplication> server = CreateObject<EdgeComputeApplication>();
    server->SetComputeNodeModel(compute);
    server->SetAttribute("Port", UintegerValue(9091));
    nodes.Get(1)->AddApplication(server);
    server->SetStartTime(Seconds(0));

    NS_TEST_ASSERT_MSG_EQ(compute->TraceConnectWithoutContext(
                              "TaskExecutionStart",
                              MakeCallback(&EdgeTaskInputGateTestCase::OnExecutionStart, this)),
                          true,
                          "Could not connect to TaskExecutionStart trace");

    m_clientSocket = Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());
    m_clientSocket->SetConnectCallback(
        MakeCallback(&EdgeTaskInputGateTestCase::OnConnect, this),
        MakeCallback(&EdgeTaskInputGateTestCase::OnConnectFailure, this));
    m_clientSocket->SetRecvCallback(
        MakeCallback(&EdgeTaskInputGateTestCase::HandleClientRead, this));
    m_serverAddress = InetSocketAddress(interfaces.GetAddress(1), 9091);
    Simulator::Schedule(MilliSeconds(1),
                        &EdgeTaskInputGateTestCase::StartClientConnection,
                        this);

    Simulator::Stop(Seconds(1.5));
    Simulator::Run();

    NS_TEST_ASSERT_MSG_EQ(m_connected, true, "Client did not connect to server");
    NS_TEST_ASSERT_MSG_EQ(m_startedBeforeFinalChunk, false,
                          "Compute started BEFORE all input bytes arrived (Violates M06-FR-06)");
    NS_TEST_ASSERT_MSG_EQ(m_startedAfterFinalChunk, true,
                          "Compute did not start after all input bytes arrived");
    NS_TEST_ASSERT_MSG_EQ(m_resultReceived, true,
                          "Result not returned after remote computation");

    m_clientSocket->Close();
    Simulator::Destroy();
}

// ===========================================================================
// Test T04: Verification of LocalOnlyPolicy (M06-FR-08)
// ===========================================================================
class LocalOnlyPolicyTestCase : public TestCase
{
  public:
    LocalOnlyPolicyTestCase();
    void DoRun() override;
};

LocalOnlyPolicyTestCase::LocalOnlyPolicyTestCase()
    : TestCase("T04: LocalOnlyPolicy strictly selects local execution")
{
}

void
LocalOnlyPolicyTestCase::DoRun()
{
    TaskDescriptor task(1, 10, 1000000, 100000, 1e9, Seconds(0));

    ComputeNodeState localNode;
    localNode.nodeId = 10;
    localNode.isLocal = true;
    localNode.computeCapacityCyclesPerS = 1e9;
    localNode.queueLength = 0;
    localNode.maxQueueLength = 100;

    ComputeNodeState edgeNode;
    edgeNode.nodeId = 20;
    edgeNode.isLocal = false;
    edgeNode.computeCapacityCyclesPerS = 1e10;
    edgeNode.queueLength = 0;
    edgeNode.maxQueueLength = 100;

    ComputeNodeState cloudNode;
    cloudNode.nodeId = 30;
    cloudNode.isLocal = false;
    cloudNode.computeCapacityCyclesPerS = 1e11;
    cloudNode.queueLength = 0;
    cloudNode.maxQueueLength = 100;

    std::vector<ComputeNodeState> candidates = {localNode, edgeNode, cloudNode};

    Ptr<LocalOnlyPolicy> policy = CreateObject<LocalOnlyPolicy>();
    OffloadDecision decision = policy->Select(task, candidates);

    NS_TEST_ASSERT_MSG_EQ(decision.executeLocally, true,
                          "LocalOnlyPolicy must select local execution");
    NS_TEST_ASSERT_MSG_EQ(decision.targetNodeId, 10,
                          "LocalOnlyPolicy must target local node ID 10");
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
    node->GetQueue()->SetMaxQueueLength(2);

    auto nullCb = MakeNullCallback<void, const TaskDescriptor&, Time, Time>();

    TaskDescriptor t1(1, 0, 1000, 100, 1e9, Simulator::Now());
    TaskDescriptor t2(2, 0, 1000, 100, 1e9, Simulator::Now());
    TaskDescriptor t3(3, 0, 1000, 100, 1e9, Simulator::Now());
    TaskDescriptor t4(4, 0, 1000, 100, 1e9, Simulator::Now());

    bool adm1 = node->SubmitTask(t1, nullCb);
    NS_TEST_ASSERT_MSG_EQ(adm1, true, "Task 1 admitted to CPU");
    NS_TEST_ASSERT_MSG_EQ(node->GetQueueLength(), 0, "Queue length should be 0");

    bool adm2 = node->SubmitTask(t2, nullCb);
    NS_TEST_ASSERT_MSG_EQ(adm2, true, "Task 2 admitted into queue");
    NS_TEST_ASSERT_MSG_EQ(node->GetQueueLength(), 1, "Queue length should be 1");

    bool adm3 = node->SubmitTask(t3, nullCb);
    NS_TEST_ASSERT_MSG_EQ(adm3, true, "Task 3 admitted into queue (Full)");
    NS_TEST_ASSERT_MSG_EQ(node->GetQueueLength(), 2, "Queue length should be 2");

    bool adm4 = node->SubmitTask(t4, nullCb);
    NS_TEST_ASSERT_MSG_EQ(adm4, false, "Task 4 must be dropped when queue is full");
    NS_TEST_ASSERT_MSG_EQ(node->GetQueueLength(), 2, "Queue length must not exceed 2");

    Simulator::Destroy();
}

// ===========================================================================
// Test T06: TaskCompleted TraceSource emits correct taskId (M06-FR-09)
// ===========================================================================
class FutureNetEdgeT06TestCase : public TestCase
{
  public:
    FutureNetEdgeT06TestCase();
    void DoRun() override;

  private:
    void OnTaskCompleted(uint64_t taskId, Time e2eTime);
    uint64_t m_completedTaskId{0};
    Time m_completedE2eTime{Seconds(0)};
};

FutureNetEdgeT06TestCase::FutureNetEdgeT06TestCase()
    : TestCase("T06: TaskCompleted TraceSource emits correct taskId and duration")
{
}

void
FutureNetEdgeT06TestCase::OnTaskCompleted(uint64_t taskId, Time e2eTime)
{
    m_completedTaskId = taskId;
    m_completedE2eTime = e2eTime;
}

void
FutureNetEdgeT06TestCase::DoRun()
{
    NodeContainer nodes;
    nodes.Create(1);

    Ptr<ComputeNodeModel> compute = CreateObject<ComputeNodeModel>();
    compute->SetComputeCapacity(1e9);
    nodes.Get(0)->AggregateObject(compute);

    Ptr<TaskClientApplication> client = CreateObject<TaskClientApplication>();
    client->SetAttribute("RequiredCycles", DoubleValue(5e8)); // 0.5 seconds
    client->SetAttribute("TaskArrivalInterval", TimeValue(Seconds(0))); // Manual generation
    nodes.Get(0)->AddApplication(client);

    client->TraceConnectWithoutContext(
        "TaskCompleted",
        MakeCallback(&FutureNetEdgeT06TestCase::OnTaskCompleted, this));

    client->SetStartTime(Seconds(0.0));
    client->SetStopTime(Seconds(2.0));

    // Generate single local task at t=0.1s
    Simulator::Schedule(Seconds(0.1), &TaskClientApplication::GenerateTask, client);

    Simulator::Stop(Seconds(2.0));
    Simulator::Run();

    NS_TEST_ASSERT_MSG_NE(m_completedTaskId, 0, "TaskCompleted trace source must fire");
    NS_TEST_ASSERT_MSG_EQ_TOL(m_completedE2eTime.GetSeconds(), 0.5, 1e-4,
                              "Task completion time must equal 0.5 s");

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
    Ptr<ComputeNodeModel> node = CreateObject<ComputeNodeModel>();
    DoubleValue capacityVal;
    node->GetAttribute("ComputeCapacity", capacityVal);
    NS_TEST_ASSERT_MSG_EQ_TOL(capacityVal.Get(), 1e9, 1e-6,
                              "Default ComputeCapacity must be 1e9 cycles/s");

    Ptr<ComputeQueue> queue = CreateObject<ComputeQueue>();
    UintegerValue maxQVal;
    queue->GetAttribute("MaxQueuedTasks", maxQVal);
    NS_TEST_ASSERT_MSG_EQ(maxQVal.Get(), 100, "Default MaxQueuedTasks must be 100");

    StringValue schedVal;
    queue->GetAttribute("ComputeScheduling", schedVal);
    NS_TEST_ASSERT_MSG_EQ(schedVal.Get(), "FCFS", "Default scheduling must be FCFS");

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

    bool belowRange = node->SetAttributeFailSafe("ComputeCapacity", DoubleValue(1e6));
    NS_TEST_ASSERT_MSG_EQ(belowRange, false, "ComputeCapacity < 1e7 must fail");

    bool aboveRange = node->SetAttributeFailSafe("ComputeCapacity", DoubleValue(1e15));
    NS_TEST_ASSERT_MSG_EQ(aboveRange, false, "ComputeCapacity > 1e14 must fail");

    bool validRange = node->SetAttributeFailSafe("ComputeCapacity", DoubleValue(1e10));
    NS_TEST_ASSERT_MSG_EQ(validRange, true, "Valid ComputeCapacity 1e10 must succeed");

    Ptr<ComputeQueue> queue = CreateObject<ComputeQueue>();
    bool zeroLimit = queue->SetAttributeFailSafe("MaxQueuedTasks", UintegerValue(0));
    NS_TEST_ASSERT_MSG_EQ(zeroLimit, false, "MaxQueuedTasks == 0 must fail");

    bool excessiveLimit = queue->SetAttributeFailSafe("MaxQueuedTasks", UintegerValue(200000));
    NS_TEST_ASSERT_MSG_EQ(excessiveLimit, false, "MaxQueuedTasks > 100000 must fail");

    bool validLimit = queue->SetAttributeFailSafe("MaxQueuedTasks", UintegerValue(500));
    NS_TEST_ASSERT_MSG_EQ(validLimit, true, "Valid MaxQueuedTasks 500 must succeed");
}

// ===========================================================================
// Test CG-T5: Deterministic repeatability across identical RNG seeds (CG-10)
// ===========================================================================
class FutureNetEdgeCgT5TestCase : public TestCase
{
  public:
    FutureNetEdgeCgT5TestCase();
    void DoRun() override;

  private:
    Time RunSimulationPass(uint32_t seed, uint32_t run);
    void OnTaskCompleted(uint64_t taskId, Time e2eTime);
    Time m_lastE2eTime{Seconds(0)};
};

FutureNetEdgeCgT5TestCase::FutureNetEdgeCgT5TestCase()
    : TestCase("CG-T5: Deterministic repeatability across identical RNG seeds")
{
}

void
FutureNetEdgeCgT5TestCase::OnTaskCompleted(uint64_t taskId, Time e2eTime)
{
    (void)taskId;
    m_lastE2eTime = e2eTime;
}

Time
FutureNetEdgeCgT5TestCase::RunSimulationPass(uint32_t seed, uint32_t run)
{
    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(run);

    NodeContainer nodes;
    nodes.Create(1);

    Ptr<ComputeNodeModel> compute = CreateObject<ComputeNodeModel>();
    compute->SetComputeCapacity(1e9);
    nodes.Get(0)->AggregateObject(compute);

    Ptr<TaskClientApplication> client = CreateObject<TaskClientApplication>();
    client->SetAttribute("RequiredCycles", DoubleValue(1e9));
    client->SetAttribute("TaskArrivalInterval", TimeValue(Seconds(0)));
    nodes.Get(0)->AddApplication(client);

    client->TraceConnectWithoutContext(
        "TaskCompleted",
        MakeCallback(&FutureNetEdgeCgT5TestCase::OnTaskCompleted, this));

    client->SetStartTime(Seconds(0.0));
    client->SetStopTime(Seconds(2.0));

    Simulator::Schedule(Seconds(0.1), &TaskClientApplication::GenerateTask, client);

    Simulator::Stop(Seconds(2.0));
    Simulator::Run();
    Simulator::Destroy();

    return m_lastE2eTime;
}

void
FutureNetEdgeCgT5TestCase::DoRun()
{
    Time pass1 = RunSimulationPass(42, 1);
    Time pass2 = RunSimulationPass(42, 1);

    NS_TEST_ASSERT_MSG_EQ(pass1, pass2,
                          "Simulation with identical RNG seed and run must be bit-for-bit identical (CG-10)");
}

// ===========================================================================
// Test CG-T6: Integration test with FlowMonitor and ns-3 network stack (CG-15)
// ===========================================================================
class FutureNetEdgeCgT6TestCase : public TestCase
{
  public:
    FutureNetEdgeCgT6TestCase();
    void DoRun() override;
};

FutureNetEdgeCgT6TestCase::FutureNetEdgeCgT6TestCase()
    : TestCase("CG-T6: Integration with standard ns-3 FlowMonitor")
{
}

void
FutureNetEdgeCgT6TestCase::DoRun()
{
    NodeContainer nodes;
    nodes.Create(2);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    NetDeviceContainer devices = p2p.Install(nodes);

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper addresses;
    addresses.SetBase("10.88.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = addresses.Assign(devices);

    Ptr<ComputeNodeModel> serverCompute = CreateObject<ComputeNodeModel>();
    serverCompute->SetComputeCapacity(1e10);
    nodes.Get(1)->AggregateObject(serverCompute);

    Ptr<EdgeComputeApplication> serverApp = CreateObject<EdgeComputeApplication>();
    serverApp->SetComputeNodeModel(serverCompute);
    serverApp->SetAttribute("Port", UintegerValue(9092));
    nodes.Get(1)->AddApplication(serverApp);
    serverApp->SetStartTime(Seconds(0));

    Ptr<TaskClientApplication> clientApp = CreateObject<TaskClientApplication>();
    clientApp->SetOffloadPolicy(CreateObject<NearestEdgePolicy>());
    clientApp->SetAttribute("TaskInputBytes", UintegerValue(50000));
    clientApp->SetAttribute("TaskOutputBytes", UintegerValue(10000));
    clientApp->SetAttribute("RequiredCycles", DoubleValue(1e8));
    clientApp->SetAttribute("TaskArrivalInterval", TimeValue(Seconds(0)));

    InetSocketAddress serverAddr(interfaces.GetAddress(1), 9092);
    clientApp->AddComputeTarget(nodes.Get(1), serverAddr, 100.0, 2.0);
    nodes.Get(0)->AddApplication(clientApp);

    clientApp->SetStartTime(Seconds(0.5));
    clientApp->SetStopTime(Seconds(3.0));


    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    Simulator::Schedule(Seconds(1.0), &TaskClientApplication::GenerateTask, clientApp);

    Simulator::Stop(Seconds(3.0));
    Simulator::Run();

    monitor->CheckForLostPackets();
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    NS_TEST_ASSERT_MSG_GT(stats.size(), 0, "FlowMonitor must detect network flows");

    uint32_t totalTx = 0;
    uint32_t totalRx = 0;
    for (const auto& flow : stats)
    {
        totalTx += flow.second.txPackets;
        totalRx += flow.second.rxPackets;
    }

    NS_TEST_ASSERT_MSG_GT(totalTx, 0, "Network transmission packets must be > 0");
    NS_TEST_ASSERT_MSG_EQ(totalTx, totalRx, "No packets should be lost on clean point-to-point link");

    Simulator::Destroy();
}

// ===========================================================================
// Test T07: ComputeQueue priority scheduling (SJF and EDF)
// ===========================================================================
class FutureNetEdgeT07TestCase : public TestCase
{
  public:
    FutureNetEdgeT07TestCase();
    void DoRun() override;

  private:
    void OnTaskDone(const TaskDescriptor& task, Time waitTime, Time computeTime);
    std::vector<uint64_t> m_completedOrder;
};

FutureNetEdgeT07TestCase::FutureNetEdgeT07TestCase()
    : TestCase("T07: ComputeQueue priority scheduling (SJF and EDF)")
{
}

void
FutureNetEdgeT07TestCase::OnTaskDone(const TaskDescriptor& task, Time waitTime, Time computeTime)
{
    (void)waitTime;
    (void)computeTime;
    m_completedOrder.push_back(task.GetTaskId());
}

void
FutureNetEdgeT07TestCase::DoRun()
{
    // Part A: Direct ComputeQueue SJF (Shortest Job First) verification
    {
        Ptr<ComputeQueue> queue = CreateObject<ComputeQueue>();
        queue->SetScheduling("SJF");

        TaskDescriptor t1(1, 0, 1000, 100, 3e9, Simulator::Now()); // 3 Gcycles
        TaskDescriptor t2(2, 0, 1000, 100, 1e9, Simulator::Now()); // 1 Gcycles
        TaskDescriptor t3(3, 0, 1000, 100, 2e9, Simulator::Now()); // 2 Gcycles

        auto nullCb = MakeNullCallback<void, const TaskDescriptor&, Time, Time>();
        ComputeQueueEntry e1;
        e1.task = t1;
        e1.timeQueued = Simulator::Now();
        e1.onCompleteCallback = nullCb;

        ComputeQueueEntry e2;
        e2.task = t2;
        e2.timeQueued = Simulator::Now();
        e2.onCompleteCallback = nullCb;

        ComputeQueueEntry e3;
        e3.task = t3;
        e3.timeQueued = Simulator::Now();
        e3.onCompleteCallback = nullCb;

        NS_TEST_ASSERT_MSG_EQ(queue->Enqueue(e1), true, "Enqueue e1");
        NS_TEST_ASSERT_MSG_EQ(queue->Enqueue(e2), true, "Enqueue e2");
        NS_TEST_ASSERT_MSG_EQ(queue->Enqueue(e3), true, "Enqueue e3");

        ComputeQueueEntry out;
        NS_TEST_ASSERT_MSG_EQ(queue->Dequeue(out), true, "Dequeue 1");
        NS_TEST_ASSERT_MSG_EQ(out.task.GetTaskId(), 2, "SJF must prioritize shortest job (Task 2: 1e9 cycles)");

        NS_TEST_ASSERT_MSG_EQ(queue->Dequeue(out), true, "Dequeue 2");
        NS_TEST_ASSERT_MSG_EQ(out.task.GetTaskId(), 3, "SJF must dequeue next shortest job (Task 3: 2e9 cycles)");

        NS_TEST_ASSERT_MSG_EQ(queue->Dequeue(out), true, "Dequeue 3");
        NS_TEST_ASSERT_MSG_EQ(out.task.GetTaskId(), 1, "SJF must dequeue longest job last (Task 1: 3e9 cycles)");

        NS_TEST_ASSERT_MSG_EQ(queue->IsEmpty(), true, "Queue must be empty");
    }

    // Part B: Direct ComputeQueue EDF (Earliest Deadline First) verification
    {
        Ptr<ComputeQueue> queue = CreateObject<ComputeQueue>();
        queue->SetScheduling("EDF");

        TaskDescriptor t1(10, 0, 1000, 100, 1e9, Simulator::Now());
        t1.SetDeadline(Seconds(30)); // Deadline 30s
        TaskDescriptor t2(20, 0, 1000, 100, 1e9, Simulator::Now());
        t2.SetDeadline(Seconds(10)); // Deadline 10s
        TaskDescriptor t3(30, 0, 1000, 100, 1e9, Simulator::Now());
        t3.SetDeadline(Seconds(20)); // Deadline 20s

        auto nullCb = MakeNullCallback<void, const TaskDescriptor&, Time, Time>();
        ComputeQueueEntry e1;
        e1.task = t1;
        e1.timeQueued = Simulator::Now();
        e1.onCompleteCallback = nullCb;

        ComputeQueueEntry e2;
        e2.task = t2;
        e2.timeQueued = Simulator::Now();
        e2.onCompleteCallback = nullCb;

        ComputeQueueEntry e3;
        e3.task = t3;
        e3.timeQueued = Simulator::Now();
        e3.onCompleteCallback = nullCb;

        NS_TEST_ASSERT_MSG_EQ(queue->Enqueue(e1), true, "Enqueue e1");
        NS_TEST_ASSERT_MSG_EQ(queue->Enqueue(e2), true, "Enqueue e2");
        NS_TEST_ASSERT_MSG_EQ(queue->Enqueue(e3), true, "Enqueue e3");

        ComputeQueueEntry out;
        NS_TEST_ASSERT_MSG_EQ(queue->Dequeue(out), true, "Dequeue 1");
        NS_TEST_ASSERT_MSG_EQ(out.task.GetTaskId(), 20, "EDF must prioritize earliest deadline (Task 20: 10s)");

        NS_TEST_ASSERT_MSG_EQ(queue->Dequeue(out), true, "Dequeue 2");
        NS_TEST_ASSERT_MSG_EQ(out.task.GetTaskId(), 30, "EDF must dequeue next earliest deadline (Task 30: 20s)");

        NS_TEST_ASSERT_MSG_EQ(queue->Dequeue(out), true, "Dequeue 3");
        NS_TEST_ASSERT_MSG_EQ(out.task.GetTaskId(), 10, "EDF must dequeue latest deadline last (Task 10: 30s)");

        NS_TEST_ASSERT_MSG_EQ(queue->IsEmpty(), true, "Queue must be empty");
    }

    // Part C: Integrated ComputeNodeModel execution with SJF
    {
        m_completedOrder.clear();
        Ptr<ComputeNodeModel> node = CreateObject<ComputeNodeModel>();
        node->SetComputeCapacity(1e9); // 1 Gcycles/s
        node->GetQueue()->SetScheduling("SJF");

        // t100 runs immediately: 2e9 cycles = 2.0s compute duration
        TaskDescriptor t100(100, 0, 1000, 100, 2e9, Simulator::Now());
        // While t100 is executing, submit t101 (3e9 cycles) and t102 (1e9 cycles)
        TaskDescriptor t101(101, 0, 1000, 100, 3e9, Simulator::Now());
        TaskDescriptor t102(102, 0, 1000, 100, 1e9, Simulator::Now());

        node->SubmitTask(t100, MakeCallback(&FutureNetEdgeT07TestCase::OnTaskDone, this));
        node->SubmitTask(t101, MakeCallback(&FutureNetEdgeT07TestCase::OnTaskDone, this));
        node->SubmitTask(t102, MakeCallback(&FutureNetEdgeT07TestCase::OnTaskDone, this));

        Simulator::Run();
        Simulator::Destroy();

        NS_TEST_ASSERT_MSG_EQ(m_completedOrder.size(), 3, "All 3 tasks must complete");
        NS_TEST_ASSERT_MSG_EQ(m_completedOrder[0], 100, "First running task must complete first");
        NS_TEST_ASSERT_MSG_EQ(m_completedOrder[1], 102, "SJF must dispatch shorter queued job (Task 102) before longer job");
        NS_TEST_ASSERT_MSG_EQ(m_completedOrder[2], 101, "SJF must dispatch longer queued job (Task 101) last");
    }
}

// ===========================================================================
// Test T08: UDP transport offloading and packet chunking (M06-FR-02)
// ===========================================================================
class FutureNetEdgeT08TestCase : public TestCase
{
  public:
    FutureNetEdgeT08TestCase();
    void DoRun() override;

  private:
    void OnTaskCompleted(uint64_t taskId, Time e2eTime);
    uint32_t m_completedCount{0};
    uint64_t m_completedTaskId{0};
    Time m_lastE2eTime{Seconds(0)};
};

FutureNetEdgeT08TestCase::FutureNetEdgeT08TestCase()
    : TestCase("T08: UDP transport offloading and packet chunking")
{
}

void
FutureNetEdgeT08TestCase::OnTaskCompleted(uint64_t taskId, Time e2eTime)
{
    m_completedCount++;
    m_completedTaskId = taskId;
    m_lastE2eTime = e2eTime;
}

void
FutureNetEdgeT08TestCase::DoRun()
{
    m_completedCount = 0;
    m_completedTaskId = 0;
    m_lastE2eTime = Seconds(0);

    NodeContainer nodes;
    nodes.Create(2);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    NetDeviceContainer devices = p2p.Install(nodes);

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper addresses;
    addresses.SetBase("10.77.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = addresses.Assign(devices);

    Ptr<ComputeNodeModel> serverCompute = CreateObject<ComputeNodeModel>();
    serverCompute->SetComputeCapacity(1e10); // 10 Gcycles/s
    nodes.Get(1)->AggregateObject(serverCompute);

    Ptr<EdgeComputeApplication> serverApp = CreateObject<EdgeComputeApplication>();
    serverApp->SetComputeNodeModel(serverCompute);
    serverApp->SetAttribute("OffloadProtocol", StringValue("UDP"));
    serverApp->SetAttribute("Port", UintegerValue(9098));
    nodes.Get(1)->AddApplication(serverApp);
    serverApp->SetStartTime(Seconds(0.0));
    serverApp->SetStopTime(Seconds(5.0));

    Ptr<TaskClientApplication> clientApp = CreateObject<TaskClientApplication>();
    clientApp->SetOffloadPolicy(CreateObject<NearestEdgePolicy>());
    clientApp->SetAttribute("OffloadProtocol", StringValue("UDP"));
    clientApp->SetAttribute("TaskInputBytes", UintegerValue(5000));   // Multi-packet (>1400 bytes)
    clientApp->SetAttribute("TaskOutputBytes", UintegerValue(1500));  // Multi-packet return
    clientApp->SetAttribute("RequiredCycles", DoubleValue(1e8));      // 0.01s compute
    clientApp->SetAttribute("TaskArrivalInterval", TimeValue(Seconds(0)));

    InetSocketAddress serverAddr(interfaces.GetAddress(1), 9098);
    clientApp->AddComputeTarget(nodes.Get(1), serverAddr, 100.0, 2.0);
    nodes.Get(0)->AddApplication(clientApp);

    clientApp->TraceConnectWithoutContext(
        "TaskCompleted",
        MakeCallback(&FutureNetEdgeT08TestCase::OnTaskCompleted, this));

    clientApp->SetStartTime(Seconds(0.5));
    clientApp->SetStopTime(Seconds(5.0));

    Simulator::Schedule(Seconds(1.0), &TaskClientApplication::GenerateTask, clientApp);

    Simulator::Stop(Seconds(5.0));
    Simulator::Run();

    NS_TEST_ASSERT_MSG_EQ(m_completedCount, 1, "UDP offload task must complete");
    NS_TEST_ASSERT_MSG_EQ(m_completedTaskId, 1, "Completed taskId must match generated task");
    NS_TEST_ASSERT_MSG_GT(m_lastE2eTime, Seconds(0), "End-to-end time must be positive");

    Simulator::Destroy();
}

// ===========================================================================
// TestSuite definition: Bundles all 12 test cases under "futurenet-edge"
// ===========================================================================
class FuturenetEdgeTestSuite : public TestSuite
{
  public:
    FuturenetEdgeTestSuite();
};

FuturenetEdgeTestSuite::FuturenetEdgeTestSuite()
    : TestSuite("futurenet-edge", Type::UNIT)
{
    AddTestCase(new FutureNetEdgeT01TestCase);
    AddTestCase(new FutureNetEdgeT02TestCase);
    AddTestCase(new EdgeTaskInputGateTestCase);
    AddTestCase(new LocalOnlyPolicyTestCase);
    AddTestCase(new FutureNetEdgeT05TestCase);
    AddTestCase(new FutureNetEdgeT06TestCase);
    AddTestCase(new FutureNetEdgeT07TestCase);
    AddTestCase(new FutureNetEdgeT08TestCase);
    AddTestCase(new FutureNetEdgeCgT1TestCase);
    AddTestCase(new FutureNetEdgeCgT2TestCase);
    AddTestCase(new FutureNetEdgeCgT5TestCase);
    AddTestCase(new FutureNetEdgeCgT6TestCase);
}

static FuturenetEdgeTestSuite g_futurenetEdgeTestSuite;

} // namespace ns3

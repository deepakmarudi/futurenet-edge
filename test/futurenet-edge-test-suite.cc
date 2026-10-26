/**
 * \file futurenet-edge-test-suite.cc
 * \brief Automated test suite for M06 futurenet-edge module.
 * \author FutureNet Module Team
 *
 * Implements Test T04: LocalOnlyPolicy verification (M06 SRS Section 9).
 */

#include "ns3/compute-node-model.h"
#include "ns3/edge-compute-application.h"
#include "ns3/offload-policy.h"
#include "ns3/task-descriptor.h"
#include "ns3/task-packet-header.h"

#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/log.h"
#include "ns3/net-device-container.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"

#include <cstdint>
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

/**
 * \ingroup futurenet-edge-test
 * \brief T03: the server must wait for all input chunks before queue admission.
 */
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
    : TestCase("T03: remote compute starts only after all task input arrives")
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
    NS_TEST_ASSERT_MSG_EQ(sent, 1464, "The first framed input chunk should be sent");
    Simulator::Schedule(MilliSeconds(50),
                        &EdgeTaskInputGateTestCase::CheckBeforeFinalChunk,
                        this);
    Simulator::Schedule(MilliSeconds(100), &EdgeTaskInputGateTestCase::SendFinalChunk, this);
}

void
EdgeTaskInputGateTestCase::StartClientConnection()
{
    NS_TEST_ASSERT_MSG_EQ(m_clientSocket->Connect(m_serverAddress),
                          0,
                          "Could not initiate the TCP connection to the edge server");
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
    NS_TEST_ASSERT_MSG_EQ(sent, 1464, "The final framed input chunk should be sent");
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
                          "Could not connect to the compute start trace");

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

    NS_TEST_ASSERT_MSG_EQ(m_connected, true, "Client did not connect to the edge server");
    NS_TEST_ASSERT_MSG_EQ(m_startedBeforeFinalChunk,
                          false,
                          "Server started computation before the final input chunk arrived");
    NS_TEST_ASSERT_MSG_EQ(m_startedAfterFinalChunk,
                          true,
                          "Server did not start after receiving the complete input");
    NS_TEST_ASSERT_MSG_EQ(m_resultReceived,
                          true,
                          "Server did not return the result after computation");

    m_clientSocket->Close();
    Simulator::Destroy();
}

FuturenetEdgeTestSuite::FuturenetEdgeTestSuite()
    : TestSuite("futurenet-edge", Type::UNIT)
{
    AddTestCase(new LocalOnlyPolicyTestCase, TestCase::Duration::QUICK);
    AddTestCase(new EdgeTaskInputGateTestCase, TestCase::Duration::QUICK);
}

static FuturenetEdgeTestSuite g_futurenetEdgeTestSuite;

} // namespace ns3

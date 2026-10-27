/**
 * @file example-multiedge.cc
 * @brief Scale demonstration with 25 clients and 3 edge servers.
 *
 * Generates a configurable number of tasks on multiple clients and compares
 * the nearest-edge and estimated-completion-time policies over a routed
 * point-to-point topology.
 */

#include "ns3/core-module.h"
#include "ns3/edge-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/network-module.h"
#include "ns3/offload-policy.h"
#include "ns3/point-to-point-module.h"
#include "ns3/task-client-application.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ExampleMultiEdge");

namespace
{
constexpr uint32_t MIN_CLIENTS = 25;
constexpr uint32_t MIN_EDGE_SERVERS = 3;
constexpr uint64_t MIN_TOTAL_TASKS = 100;
constexpr double CLIENT_ACCESS_DELAY_MS = 2.0;
constexpr double FIRST_EDGE_DELAY_MS = 2.0;
constexpr double EDGE_DELAY_STEP_MS = 5.0;
constexpr double TASK_SPACING_SECONDS = 0.4;
constexpr double CLIENT_STAGGER_SECONDS = 0.01;
constexpr double FIRST_TASK_TIME_SECONDS = 1.0;
constexpr uint32_t CLIENT_ACCESS_RATE_MBPS = 100;
constexpr uint32_t EDGE_ACCESS_RATE_MBPS = 100;
constexpr uint64_t DEFAULT_INPUT_BYTES = 100000;
constexpr uint64_t DEFAULT_OUTPUT_BYTES = 20000;
constexpr double DEFAULT_REQUIRED_CYCLES = 5e9;

struct MultiEdgeStats
{
    std::map<uint32_t, uint32_t> offloadsByTarget;

    void
    OnTaskOffloaded(uint64_t taskId, uint32_t sourceNodeId, uint32_t targetNodeId)
    {
        (void)taskId;
        (void)sourceNodeId;
        ++offloadsByTarget[targetNodeId];
    }
};

Ipv4Address
InstallLink(PointToPointHelper &link, Ptr<Node> endpoint, Ptr<Node> router,
            Ipv4AddressHelper &address)
{
    NetDeviceContainer devices = link.Install(endpoint, router);
    Ipv4InterfaceContainer interfaces = address.Assign(devices);
    address.NewNetwork();
    return interfaces.GetAddress(0);
}

void
GenerateOneTask(Ptr<TaskClientApplication> client)
{
    client->GenerateTask();
}

bool
IsValidConfiguration(uint32_t clientCount, uint32_t edgeCount, uint32_t tasksPerClient,
                     uint64_t inputBytes, uint64_t outputBytes, double requiredCycles,
                     double simTime, const std::string &policy, const std::string &protocol,
                     const std::string &ackMode, uint16_t port)
{
    const uint64_t totalTasks = static_cast<uint64_t>(clientCount) * tasksPerClient;
    if (clientCount < MIN_CLIENTS || edgeCount < MIN_EDGE_SERVERS || totalTasks < MIN_TOTAL_TASKS)
    {
        std::cerr << "The scale scenario requires at least " << MIN_CLIENTS << " clients, "
                  << MIN_EDGE_SERVERS << " edge servers, and " << MIN_TOTAL_TASKS
                  << " total tasks.\n";
        return false;
    }
    const double lastTaskTime = FIRST_TASK_TIME_SECONDS +
                                static_cast<double>(tasksPerClient - 1) * TASK_SPACING_SECONDS +
                                static_cast<double>(clientCount - 1) * CLIENT_STAGGER_SECONDS;
    if (inputBytes < 1000 || inputBytes > 1000000000ULL || outputBytes > 1000000000ULL ||
        !std::isfinite(requiredCycles) || requiredCycles < 1e6 || requiredCycles > 1e13)
    {
        std::cerr << "Task sizes or required cycles are outside the M06 SRS limits.\n";
        return false;
    }
    if (!std::isfinite(simTime) || simTime <= lastTaskTime || port == 0)
    {
        std::cerr << "Simulation time must exceed the final task generation time and "
                     "the server port must be nonzero.\n";
        return false;
    }
    if (policy != "nearest" && policy != "lowest")
    {
        std::cerr << "Policy must be 'nearest' or 'lowest'.\n";
        return false;
    }
    if (protocol != "TCP" && protocol != "UDP")
    {
        std::cerr << "Offload protocol must be TCP or UDP.\n";
        return false;
    }
    if (ackMode != "ResultData" && ackMode != "AckOnly")
    {
        std::cerr << "Result acknowledgement mode must be ResultData or AckOnly.\n";
        return false;
    }
    return true;
}
} // namespace

int
main(int argc, char *argv[])
{
    uint32_t clientCount = MIN_CLIENTS;
    uint32_t edgeCount = MIN_EDGE_SERVERS;
    uint32_t tasksPerClient = 4;
    uint64_t inputBytes = DEFAULT_INPUT_BYTES;
    uint64_t outputBytes = DEFAULT_OUTPUT_BYTES;
    double requiredCycles = DEFAULT_REQUIRED_CYCLES;
    double simTime = 60.0;
    std::string policyName = "lowest";
    std::string protocol = "TCP";
    std::string ackMode = "ResultData";
    std::string csvPath = "multiedge-results.csv";
    uint16_t serverPort = 9090;
    uint32_t seed = 1;
    uint64_t run = 1;

    CommandLine cmd(__FILE__);
    cmd.AddValue("clientCount", "Number of task-generating client nodes (minimum 25)", clientCount);
    cmd.AddValue("edgeCount", "Number of edge servers (minimum 3)", edgeCount);
    cmd.AddValue("tasksPerClient", "Number of tasks generated by each client", tasksPerClient);
    cmd.AddValue("inputBytes", "Input payload bytes per task", inputBytes);
    cmd.AddValue("outputBytes", "Result payload bytes per task", outputBytes);
    cmd.AddValue("requiredCycles", "Required abstract CPU cycles per task", requiredCycles);
    cmd.AddValue("simTime", "Simulation duration in seconds", simTime);
    cmd.AddValue("policy", "Offload policy: nearest or lowest", policyName);
    cmd.AddValue("protocol", "Task transport protocol: TCP or UDP", protocol);
    cmd.AddValue("ackMode", "Result mode: ResultData or AckOnly", ackMode);
    cmd.AddValue("csvPath", "Path to output CSV file (default: multiedge-results.csv)", csvPath);
    cmd.AddValue("port", "Edge server listening port", serverPort);
    cmd.AddValue("seed", "RNG seed", seed);
    cmd.AddValue("run", "RNG run number", run);
    cmd.Parse(argc, argv);

    if (tasksPerClient == 0 ||
        !IsValidConfiguration(clientCount, edgeCount, tasksPerClient, inputBytes, outputBytes,
                              requiredCycles, simTime, policyName, protocol, ackMode, serverPort))
    {
        return 1;
    }

    Time::SetResolution(Time::NS);
    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(run);
    LogComponentEnable("ExampleMultiEdge", LOG_LEVEL_INFO);

    const uint64_t totalTaskCount = static_cast<uint64_t>(clientCount) * tasksPerClient;
    NS_LOG_INFO("Starting multi-edge scenario: "
                << clientCount << " clients, " << edgeCount << " edge servers, " << totalTaskCount
                << " tasks, policy=" << policyName << ", protocol=" << protocol);

    NodeContainer router;
    router.Create(1);
    NodeContainer clients;
    clients.Create(clientCount);
    NodeContainer edgeServers;
    edgeServers.Create(edgeCount);

    NodeContainer allNodes;
    allNodes.Add(router);
    allNodes.Add(clients);
    allNodes.Add(edgeServers);

    InternetStackHelper internet;
    internet.Install(allNodes);
    Ipv4AddressHelper address;
    address.SetBase("10.1.0.0", "255.255.255.252");

    PointToPointHelper clientLink;
    clientLink.SetDeviceAttribute("DataRate",
                                  StringValue(std::to_string(CLIENT_ACCESS_RATE_MBPS) + "Mbps"));
    clientLink.SetChannelAttribute("Delay", StringValue("2ms"));
    for (uint32_t i = 0; i < clientCount; ++i)
    {
        InstallLink(clientLink, clients.Get(i), router.Get(0), address);
    }

    std::vector<Ipv4Address> edgeAddresses;
    edgeAddresses.reserve(edgeCount);
    std::vector<double> edgeDelaysMs;
    edgeDelaysMs.reserve(edgeCount);
    for (uint32_t i = 0; i < edgeCount; ++i)
    {
        const double edgeDelayMs = FIRST_EDGE_DELAY_MS + i * EDGE_DELAY_STEP_MS;
        PointToPointHelper edgeLink;
        edgeLink.SetDeviceAttribute("DataRate",
                                    StringValue(std::to_string(EDGE_ACCESS_RATE_MBPS) + "Mbps"));
        edgeLink.SetChannelAttribute("Delay", StringValue(std::to_string(edgeDelayMs) + "ms"));
        edgeAddresses.push_back(InstallLink(edgeLink, edgeServers.Get(i), router.Get(0), address));
        edgeDelaysMs.push_back(edgeDelayMs);
    }
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    EdgeHelper edgeHelper;
    edgeHelper.SetComputeAttribute("ComputeCapacity", DoubleValue(1e9));
    edgeHelper.InstallComputeNode(clients);
    for (uint32_t i = 0; i < edgeCount; ++i)
    {
        const double capacityCyclesPerSecond = 1e10 * (1.0 + 0.5 * i);
        edgeHelper.SetComputeAttribute("ComputeCapacity", DoubleValue(capacityCyclesPerSecond));
        edgeHelper.InstallComputeNode(edgeServers.Get(i));
    }

    edgeHelper.SetServerAttribute("Port", UintegerValue(serverPort));
    edgeHelper.SetServerAttribute("OffloadProtocol", StringValue(protocol));
    edgeHelper.SetServerAttribute("ResultAckMode", StringValue(ackMode));
    ApplicationContainer serverApps = edgeHelper.InstallServer(edgeServers);
    serverApps.Start(Seconds(0.1));
    serverApps.Stop(Seconds(simTime));

    Ptr<OffloadPolicy> policy;
    if (policyName == "nearest")
    {
        policy = CreateObject<NearestEdgePolicy>();
    }
    else
    {
        policy = CreateObject<LowestEstimatedCompletionTimePolicy>();
    }
    edgeHelper.SetPolicy(policy);
    edgeHelper.SetClientAttribute("TaskArrivalInterval", TimeValue(Seconds(0)));
    edgeHelper.SetClientAttribute("TaskInputBytes", UintegerValue(inputBytes));
    edgeHelper.SetClientAttribute("TaskOutputBytes", UintegerValue(outputBytes));
    edgeHelper.SetClientAttribute("RequiredCycles", DoubleValue(requiredCycles));
    edgeHelper.SetClientAttribute("OffloadProtocol", StringValue(protocol));
    edgeHelper.SetClientAttribute("ResultAckMode", StringValue(ackMode));

    ApplicationContainer clientApps = edgeHelper.InstallClient(clients);
    MultiEdgeStats stats;
    std::vector<Ptr<TaskClientApplication>> clientApplications;
    clientApplications.reserve(clientCount);
    int64_t stream = 1;
    for (uint32_t i = 0; i < clientCount; ++i)
    {
        Ptr<TaskClientApplication> client = DynamicCast<TaskClientApplication>(clientApps.Get(i));
        client->AssignStreams(stream++);
        client->TraceConnectWithoutContext("TaskOffloaded",
                                           MakeCallback(&MultiEdgeStats::OnTaskOffloaded, &stats));
        for (uint32_t edge = 0; edge < edgeCount; ++edge)
        {
            const double estimatedOneWayDelayMs = CLIENT_ACCESS_DELAY_MS + edgeDelaysMs[edge];
            client->AddComputeTarget(edgeServers.Get(edge),
                                     InetSocketAddress(edgeAddresses[edge], serverPort),
                                     EDGE_ACCESS_RATE_MBPS, estimatedOneWayDelayMs);
        }
        clientApplications.push_back(client);
    }
    clientApps.Start(Seconds(0.5));
    clientApps.Stop(Seconds(simTime));

    // Enable 12-column CSV tracing for all client tasks
    edgeHelper.EnableCsvTracing(csvPath);

    // Disable periodic generation and schedule a fixed task count for a repeatable
    // 25-client / 100-task baseline while still using TaskClientApplication.
    for (uint32_t clientIndex = 0; clientIndex < clientCount; ++clientIndex)
    {
        const Time clientOffset = Seconds(clientIndex * CLIENT_STAGGER_SECONDS);
        for (uint32_t taskIndex = 0; taskIndex < tasksPerClient; ++taskIndex)
        {
            const Time taskTime = Seconds(FIRST_TASK_TIME_SECONDS) + clientOffset +
                                  Seconds(taskIndex * TASK_SPACING_SECONDS);
            Simulator::Schedule(taskTime, &GenerateOneTask, clientApplications[clientIndex]);
        }
    }

    FlowMonitorHelper flowMonitorHelper;
    Ptr<FlowMonitor> flowMonitor = flowMonitorHelper.InstallAll();
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    uint32_t completedTasks = 0;
    uint32_t failedTasks = 0;
    uint32_t unfinishedTasks = 0;
    for (uint32_t clientIndex = 0; clientIndex < clientCount; ++clientIndex)
    {
        Ptr<TaskClientApplication> client = clientApplications[clientIndex];
        const uint32_t sourceNodeId = clients.Get(clientIndex)->GetId();
        for (uint32_t taskIndex = 1; taskIndex <= tasksPerClient; ++taskIndex)
        {
            const uint64_t taskId = (static_cast<uint64_t>(sourceNodeId) << 32) | taskIndex;
            switch (client->GetTaskStatus(taskId))
            {
            case TaskStatus::COMPLETED:
                ++completedTasks;
                break;
            case TaskStatus::FAILED_QUEUE_FULL:
            case TaskStatus::FAILED_NETWORK:
                ++failedTasks;
                break;
            default:
                ++unfinishedTasks;
                break;
            }
        }
    }

    flowMonitor->CheckForLostPackets();
    const auto flowStats = flowMonitor->GetFlowStats();
    uint64_t transmittedBytes = 0;
    uint64_t receivedBytes = 0;
    uint64_t transmittedPackets = 0;
    uint64_t receivedPackets = 0;
    for (const auto &flow : flowStats)
    {
        transmittedBytes += flow.second.txBytes;
        receivedBytes += flow.second.rxBytes;
        transmittedPackets += flow.second.txPackets;
        receivedPackets += flow.second.rxPackets;
    }

    NS_LOG_INFO("Task summary: generated=" << totalTaskCount << ", completed=" << completedTasks
                                           << ", failed=" << failedTasks
                                           << ", unfinished=" << unfinishedTasks);
    NS_LOG_INFO("Remote task distribution:");
    for (uint32_t i = 0; i < edgeCount; ++i)
    {
        const uint32_t nodeId = edgeServers.Get(i)->GetId();
        NS_LOG_INFO("  Edge node " << nodeId << " (" << edgeAddresses[i]
                                   << "): " << stats.offloadsByTarget[nodeId]
                                   << " tasks, capacity=" << 1e10 * (1.0 + 0.5 * i) << " cycles/s");
    }
    NS_LOG_INFO("FlowMonitor totals: Tx=" << transmittedPackets << " packets / " << transmittedBytes
                                          << " bytes, Rx=" << receivedPackets << " packets / "
                                          << receivedBytes << " bytes");

    Simulator::Destroy();
    return 0;
}

/**
 * @file example-basic.cc
 * @brief Basic demonstration of M06 futurenet-edge task offloading.
 * @author FutureNet Module Team
 *
 * Demonstrates 1 Client Node + 1 Edge Server Node comparing LocalOnlyPolicy
 * vs. NearestEdgePolicy, integrated with FlowMonitor and CSV export.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/flow-monitor-module.h"

#include "ns3/edge-helper.h"
#include "ns3/offload-policy.h"
#include "ns3/task-client-application.h"
#include "ns3/edge-compute-application.h"
#include "ns3/compute-node-model.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ExampleBasic");

int
main(int argc, char* argv[])
{
    // 1. Command Line Parameters
    bool useOffload = false;
    uint64_t inputBytes = 1000000;      // 1 MB
    double requiredCycles = 1e9;        // 1 Billion cycles
    double simTime = 10.0;              // 10 seconds
    std::string csvPath = "edge-offload-results.csv";
    std::string eventLogPath = "";

    CommandLine cmd(__FILE__);
    cmd.AddValue("useOffload", "True = Offload to Edge, False = Run Local Only", useOffload);
    cmd.AddValue("inputBytes", "Task input data size in bytes (default: 1 MB)", inputBytes);
    cmd.AddValue("requiredCycles", "Required computation cycles (default: 1e9)", requiredCycles);
    cmd.AddValue("simTime", "Simulation duration in seconds", simTime);
    cmd.AddValue("csvPath", "Path to output CSV file", csvPath);
    cmd.AddValue("eventLogPath", "Path to optional 10-column Common Guidelines event log CSV", eventLogPath);
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);
    LogComponentEnable("ExampleBasic", LOG_LEVEL_INFO);

    NS_LOG_INFO("--- Starting M06 FutureNet-Edge Basic Example ---");
    NS_LOG_INFO("Mode: " << (useOffload ? "REMOTE OFFLOADING (NearestEdgePolicy)" : "LOCAL ONLY (LocalOnlyPolicy)"));

    // 2. Create 2 Nodes: Node 0 = Phone Client, Node 1 = Edge Server
    NodeContainer nodes;
    nodes.Create(2);

    // 3. Set up Point-to-Point Network Link (10 Mbps, 5 ms delay)
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("5ms"));

    NetDeviceContainer devices = p2p.Install(nodes);

    // 4. Install Internet Stack & Assign IP Addresses
    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // 5. Configure Compute Engines via EdgeHelper
    EdgeHelper edgeHelper;

    // Node 0 (Phone): Weak CPU (1e9 cycles/s)
    edgeHelper.SetComputeAttribute("ComputeCapacity", DoubleValue(1e9));
    edgeHelper.InstallComputeNode(nodes.Get(0));

    // Node 1 (Edge Server): Fast CPU (1e10 cycles/s = 10x faster)
    edgeHelper.SetComputeAttribute("ComputeCapacity", DoubleValue(1e10));
    edgeHelper.InstallComputeNode(nodes.Get(1));

    // 6. Install Edge Server Application on Node 1 (Listening on port 9090)
    ApplicationContainer serverApp = edgeHelper.InstallServer(nodes.Get(1));
    serverApp.Start(Seconds(0.0));
    serverApp.Stop(Seconds(simTime));

    // 7. Choose Offloading Policy
    Ptr<OffloadPolicy> policy;
    if (useOffload)
    {
        policy = CreateObject<NearestEdgePolicy>();
    }
    else
    {
        policy = CreateObject<LocalOnlyPolicy>();
    }
    edgeHelper.SetPolicy(policy);

    // Configure task generation parameters
    edgeHelper.SetClientAttribute("TaskInputBytes", UintegerValue(inputBytes));
    edgeHelper.SetClientAttribute("RequiredCycles", DoubleValue(requiredCycles));
    edgeHelper.SetClientAttribute("TaskArrivalInterval", TimeValue(Seconds(1.0)));

    // Install Client Application on Node 0
    ApplicationContainer clientApp = edgeHelper.InstallClient(nodes.Get(0));
    Ptr<TaskClientApplication> client = DynamicCast<TaskClientApplication>(clientApp.Get(0));

    // Register Edge Server as a remote compute target (Port 9090)
    InetSocketAddress serverAddr(interfaces.GetAddress(1), 9090);
    client->AddComputeTarget(nodes.Get(1), serverAddr, 10.0, 5.0);

    clientApp.Start(Seconds(1.0));
    clientApp.Stop(Seconds(simTime));

    // 8. Enable CSV Tracing Output
    edgeHelper.EnableCsvTracing(csvPath);
    if (!eventLogPath.empty())
    {
        edgeHelper.EnableCommonEventLog(eventLogPath);
    }

    // 9. Install FlowMonitor to measure real IP network traffic
    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    // 10. Run the Simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // 11. FlowMonitor Summary Report
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    NS_LOG_INFO("--- Network Flow Summary ---");
    for (auto const& flow : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        NS_LOG_INFO("Flow " << flow.first << " (" << t.sourceAddress << " -> " << t.destinationAddress << "):");
        NS_LOG_INFO("  Tx Packets: " << flow.second.txPackets << ", Rx Packets: " << flow.second.rxPackets);
        NS_LOG_INFO("  Tx Bytes:   " << flow.second.txBytes << ", Rx Bytes:   " << flow.second.rxBytes);
    }

    Simulator::Destroy();
    NS_LOG_INFO("--- Simulation Finished Successfully. CSV exported to " << csvPath << " ---");

    return 0;
}

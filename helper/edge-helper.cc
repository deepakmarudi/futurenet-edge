/**
 * @file edge-helper.cc
 * @brief Implementation of EdgeHelper for M06 futurenet-edge module.
 * @author FutureNet Module Team
 *
 * Implements M06 SRS Section 6 & CG-04: Helper class to install compute nodes,
 * client applications, and server applications on NodeContainers, forward attributes,
 * and configure machine-readable CSV/event logging.
 */

#include "edge-helper.h"

#include "../model/compute-node-model.h"
#include "../model/edge-compute-application.h"
#include "../model/offload-policy.h"
#include "../model/task-client-application.h"

#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include "ns3/trace-helper.h"

#include <iomanip>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("EdgeHelper");

EdgeHelper::~EdgeHelper() = default;

EdgeHelper::EdgeHelper()
{
    m_computeFactory.SetTypeId("ns3::ComputeNodeModel");
    m_clientFactory.SetTypeId("ns3::TaskClientApplication");
    m_serverFactory.SetTypeId("ns3::EdgeComputeApplication");
    m_defaultPolicy = CreateObject<LocalOnlyPolicy>();
}

void
EdgeHelper::InstallComputeNode(NodeContainer nodes)
{
    for (auto it = nodes.Begin(); it != nodes.End(); ++it)
    {
        InstallComputeNode(*it);
    }
}

void
EdgeHelper::InstallComputeNode(Ptr<Node> node)
{
    NS_ASSERT_MSG(node != nullptr, "Cannot install ComputeNodeModel on null Node");
    Ptr<ComputeNodeModel> model = m_computeFactory.Create<ComputeNodeModel>();
    node->AggregateObject(model);
    m_installedComputeNodes.push_back(model);
    if (m_eventStream != nullptr)
    {
        HookEventCompute(model);
    }
    NS_LOG_INFO("Installed ComputeNodeModel on Node " << node->GetId());
}

ApplicationContainer
EdgeHelper::InstallClient(NodeContainer nodes)
{
    ApplicationContainer apps;
    for (auto it = nodes.Begin(); it != nodes.End(); ++it)
    {
        apps.Add(InstallClient(*it));
    }
    return apps;
}

ApplicationContainer
EdgeHelper::InstallClient(Ptr<Node> node)
{
    NS_ASSERT_MSG(node != nullptr, "Cannot install TaskClientApplication on null Node");
    Ptr<TaskClientApplication> app = m_clientFactory.Create<TaskClientApplication>();
    if (m_defaultPolicy != nullptr)
    {
        app->SetOffloadPolicy(m_defaultPolicy);
    }
    node->AddApplication(app);
    m_installedClients.push_back(app);
    if (m_csvStream != nullptr)
    {
        HookCsvClient(app);
    }
    if (m_eventStream != nullptr)
    {
        HookEventClient(app);
    }
    NS_LOG_INFO("Installed TaskClientApplication on Node " << node->GetId());
    return ApplicationContainer(app);
}

ApplicationContainer
EdgeHelper::InstallServer(NodeContainer nodes)
{
    ApplicationContainer apps;
    for (auto it = nodes.Begin(); it != nodes.End(); ++it)
    {
        apps.Add(InstallServer(*it));
    }
    return apps;
}

ApplicationContainer
EdgeHelper::InstallServer(Ptr<Node> node)
{
    NS_ASSERT_MSG(node != nullptr, "Cannot install EdgeComputeApplication on null Node");
    Ptr<EdgeComputeApplication> app = m_serverFactory.Create<EdgeComputeApplication>();

    // Connect node's ComputeNodeModel to the server application
    Ptr<ComputeNodeModel> computeModel = node->GetObject<ComputeNodeModel>();
    if (computeModel != nullptr)
    {
        app->SetComputeNodeModel(computeModel);
    }
    else
    {
        NS_LOG_WARN("Node " << node->GetId() << " missing ComputeNodeModel. Installing default.");
        InstallComputeNode(node);
        app->SetComputeNodeModel(node->GetObject<ComputeNodeModel>());
    }

    node->AddApplication(app);
    NS_LOG_INFO("Installed EdgeComputeApplication on Node " << node->GetId());
    return ApplicationContainer(app);
}

void
EdgeHelper::SetComputeAttribute(std::string name, const AttributeValue& val)
{
    m_computeFactory.Set(name, val);
}

void
EdgeHelper::SetClientAttribute(std::string name, const AttributeValue& val)
{
    m_clientFactory.Set(name, val);
}

void
EdgeHelper::SetServerAttribute(std::string name, const AttributeValue& val)
{
    m_serverFactory.Set(name, val);
}

void
EdgeHelper::SetPolicy(Ptr<OffloadPolicy> policy)
{
    m_defaultPolicy = policy;
}

void
EdgeHelper::HookCsvClient(Ptr<TaskClientApplication> client)
{
    if (client != nullptr && m_csvStream != nullptr)
    {
        client->TraceConnectWithoutContext(
            "TaskRecordCompleted",
            MakeBoundCallback(&EdgeHelper::TaskRecordCsvWriter, m_csvStream));
    }
}

void
EdgeHelper::TaskRecordCsvWriter(Ptr<OutputStreamWrapper> stream, const TaskClientRecord& record)
{
    if (stream == nullptr || stream->GetStream() == nullptr)
    {
        return;
    }

    std::ostream* os = stream->GetStream();

    double e2eMs = (record.timeDone - record.timeCreated).GetSeconds() * 1000.0;
    double computeMs = record.remoteComputeDuration.GetSeconds() * 1000.0;
    double queueMs = record.remoteQueueDuration.GetSeconds() * 1000.0;

    double netMs = 0.0;
    if (record.targetNodeId != record.task.GetSourceNodeId())
    {
        netMs = std::max(0.0, e2eMs - computeMs - queueMs);
    }

    double deadlineMs = record.task.HasDeadline()
                            ? record.task.GetDeadline().GetSeconds() * 1000.0
                            : 0.0;
    int completed = (record.status == TaskStatus::COMPLETED) ? 1 : 0;

    std::ios state(nullptr);
    state.copyfmt(*os);

    *os << record.task.GetTaskId() << ","
        << record.task.GetSourceNodeId() << ","
        << record.targetNodeId << ","
        << std::fixed << std::setprecision(3) << record.timeCreated.GetSeconds() << ","
        << record.task.GetInputBytes() << ","
        << static_cast<uint64_t>(record.task.GetRequiredCycles()) << ","
        << std::fixed << std::setprecision(3) << queueMs << ","
        << std::fixed << std::setprecision(3) << netMs << ","
        << std::fixed << std::setprecision(3) << computeMs << ","
        << std::fixed << std::setprecision(3) << e2eMs << ","
        << std::fixed << std::setprecision(3) << deadlineMs << ","
        << completed << "\n";

    os->copyfmt(state);
    os->flush();
}

void
EdgeHelper::EnableCsvTracing(std::string filePath)
{
    AsciiTraceHelper ascii;
    m_csvStream = ascii.CreateFileStream(filePath);
    *m_csvStream->GetStream()
        << "task_id,source,target,created_s,input_bytes,cycles,queue_ms,network_ms,compute_ms,e2e_ms,deadline_ms,completed\n";
    m_csvStream->GetStream()->flush();

    for (auto& client : m_installedClients)
    {
        HookCsvClient(client);
    }

    NS_LOG_INFO("Enabled M06 CSV tracing to: " << filePath);
}

void
EdgeHelper::HookEventClient(Ptr<TaskClientApplication> client)
{
    if (client != nullptr && m_eventStream != nullptr)
    {
        client->TraceConnectWithoutContext("TaskCreated",
            MakeBoundCallback(&EdgeHelper::EventTaskCreated, m_eventStream));
        client->TraceConnectWithoutContext("TaskOffloaded",
            MakeBoundCallback(&EdgeHelper::EventTaskOffloaded, m_eventStream));
        client->TraceConnectWithoutContext("TaskCompleted",
            MakeBoundCallback(&EdgeHelper::EventTaskCompleted, m_eventStream));
        client->TraceConnectWithoutContext("TaskDeadlineMiss",
            MakeBoundCallback(&EdgeHelper::EventTaskDeadlineMiss, m_eventStream));
    }
}

void
EdgeHelper::HookEventCompute(Ptr<ComputeNodeModel> compute)
{
    if (compute != nullptr && m_eventStream != nullptr)
    {
        compute->TraceConnectWithoutContext("TaskQueued",
            MakeBoundCallback(&EdgeHelper::EventTaskQueued, m_eventStream));
        compute->TraceConnectWithoutContext("TaskExecutionStart",
            MakeBoundCallback(&EdgeHelper::EventTaskExecutionStart, m_eventStream));
        compute->TraceConnectWithoutContext("TaskExecutionComplete",
            MakeBoundCallback(&EdgeHelper::EventTaskExecutionComplete, m_eventStream));
    }
}

void
EdgeHelper::EventTaskCreated(Ptr<OutputStreamWrapper> stream, uint64_t taskId, uint32_t sourceId)
{
    if (stream != nullptr && stream->GetStream() != nullptr)
    {
        std::ostream* os = stream->GetStream();
        std::ios state(nullptr);
        state.copyfmt(*os);
        *os << std::fixed << std::setprecision(3) << Simulator::Now().GetSeconds()
            << ",futurenet-edge,TaskCreated," << sourceId << "," << sourceId << ","
            << taskId << ",0,0,none,task_id=" << taskId << "\n";
        os->copyfmt(state);
        os->flush();
    }
}

void
EdgeHelper::EventTaskOffloaded(Ptr<OutputStreamWrapper> stream, uint64_t taskId, uint32_t sourceId, uint32_t targetId)
{
    if (stream != nullptr && stream->GetStream() != nullptr)
    {
        std::ostream* os = stream->GetStream();
        std::ios state(nullptr);
        state.copyfmt(*os);
        *os << std::fixed << std::setprecision(3) << Simulator::Now().GetSeconds()
            << ",futurenet-edge,TaskOffloaded," << sourceId << "," << targetId << ","
            << taskId << ",0,0,none,task_id=" << taskId << "\n";
        os->copyfmt(state);
        os->flush();
    }
}

void
EdgeHelper::EventTaskCompleted(Ptr<OutputStreamWrapper> stream, uint64_t taskId, Time e2eTime)
{
    if (stream != nullptr && stream->GetStream() != nullptr)
    {
        std::ostream* os = stream->GetStream();
        std::ios state(nullptr);
        state.copyfmt(*os);
        *os << std::fixed << std::setprecision(3) << Simulator::Now().GetSeconds()
            << ",futurenet-edge,TaskCompleted,0,0," << taskId << ","
            << std::fixed << std::setprecision(3) << e2eTime.GetMilliSeconds()
            << ",0,ms,task_id=" << taskId << "\n";
        os->copyfmt(state);
        os->flush();
    }
}

void
EdgeHelper::EventTaskDeadlineMiss(Ptr<OutputStreamWrapper> stream, uint64_t taskId, Time lateness)
{
    if (stream != nullptr && stream->GetStream() != nullptr)
    {
        std::ostream* os = stream->GetStream();
        std::ios state(nullptr);
        state.copyfmt(*os);
        *os << std::fixed << std::setprecision(3) << Simulator::Now().GetSeconds()
            << ",futurenet-edge,TaskDeadlineMiss,0,0," << taskId << ","
            << std::fixed << std::setprecision(3) << lateness.GetMilliSeconds()
            << ",0,ms,task_id=" << taskId << "\n";
        os->copyfmt(state);
        os->flush();
    }
}

void
EdgeHelper::EventTaskQueued(Ptr<OutputStreamWrapper> stream, uint64_t taskId, uint32_t targetId, uint32_t queueLen)
{
    if (stream != nullptr && stream->GetStream() != nullptr)
    {
        std::ostream* os = stream->GetStream();
        std::ios state(nullptr);
        state.copyfmt(*os);
        *os << std::fixed << std::setprecision(3) << Simulator::Now().GetSeconds()
            << ",futurenet-edge,TaskQueued," << targetId << ",0," << taskId << ","
            << queueLen << ",0,tasks,task_id=" << taskId << "\n";
        os->copyfmt(state);
        os->flush();
    }
}

void
EdgeHelper::EventTaskExecutionStart(Ptr<OutputStreamWrapper> stream, uint64_t taskId, uint32_t targetId)
{
    if (stream != nullptr && stream->GetStream() != nullptr)
    {
        std::ostream* os = stream->GetStream();
        std::ios state(nullptr);
        state.copyfmt(*os);
        *os << std::fixed << std::setprecision(3) << Simulator::Now().GetSeconds()
            << ",futurenet-edge,TaskExecutionStart," << targetId << ",0," << taskId << ",0,0,none,task_id=" << taskId << "\n";
        os->copyfmt(state);
        os->flush();
    }
}

void
EdgeHelper::EventTaskExecutionComplete(Ptr<OutputStreamWrapper> stream, uint64_t taskId, uint32_t targetId)
{
    if (stream != nullptr && stream->GetStream() != nullptr)
    {
        std::ostream* os = stream->GetStream();
        std::ios state(nullptr);
        state.copyfmt(*os);
        *os << std::fixed << std::setprecision(3) << Simulator::Now().GetSeconds()
            << ",futurenet-edge,TaskExecutionComplete," << targetId << ",0," << taskId << ",0,0,none,task_id=" << taskId << "\n";
        os->copyfmt(state);
        os->flush();
    }
}

void
EdgeHelper::EnableCommonEventLog(std::string filePath)
{
    AsciiTraceHelper ascii;
    m_eventStream = ascii.CreateFileStream(filePath);
    *m_eventStream->GetStream()
        << "time_s,module,event,node_id,peer_id,flow_id,value1,value2,unit,detail\n";
    m_eventStream->GetStream()->flush();

    for (auto& client : m_installedClients)
    {
        HookEventClient(client);
    }
    for (auto& compute : m_installedComputeNodes)
    {
        HookEventCompute(compute);
    }

    NS_LOG_INFO("Enabled Common Guidelines Event Log to: " << filePath);
}

} // namespace ns3

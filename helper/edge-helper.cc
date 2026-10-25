/**
 * \file edge-helper.cc
 * \brief Implementation of EdgeHelper for M06 futurenet-edge module.
 * \author FutureNet Module Team
 *
 * Implements M06 SRS Section 6 & CG-04: Helper class to install compute nodes,
 * client applications, and server applications on NodeContainers, forward attributes,
 * and configure machine-readable CSV/event logging.
 */

#include "edge-helper.h"

#include "compute-node-model.h"
#include "edge-compute-application.h"
#include "offload-policy.h"
#include "task-client-application.h"

#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("EdgeHelper");

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
EdgeHelper::EnableCsvTracing(std::string filePath)
{
    AsciiTraceHelper ascii;
    m_csvStream = ascii.CreateFileStream(filePath);
    *m_csvStream->GetStream()
        << "task_id,source,target,created_s,input_bytes,cycles,queue_ms,network_ms,compute_ms,e2e_ms,deadline_ms,completed\n";
    m_csvStream->GetStream()->flush();
    NS_LOG_INFO("Enabled M06 CSV tracing to: " << filePath);
}

void
EdgeHelper::EnableCommonEventLog(std::string filePath)
{
    AsciiTraceHelper ascii;
    m_eventStream = ascii.CreateFileStream(filePath);
    *m_eventStream->GetStream()
        << "time_s,module,event,node_id,peer_id,flow_id,value1,value2,unit,detail\n";
    m_eventStream->GetStream()->flush();
    NS_LOG_INFO("Enabled Common Guidelines Event Log to: " << filePath);
}

} // namespace ns3
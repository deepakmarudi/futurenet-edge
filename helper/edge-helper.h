/**
 * @file edge-helper.h
 * @brief EdgeHelper definition for M06 futurenet-edge module.
 * @author FutureNet Module Team
 *
 * Implements M06 SRS Section 6 & CG-04: Helper class to install compute nodes,
 * client applications, and server applications on NodeContainers, forward attributes,
 * and configure machine-readable CSV/event logging.
 */

#ifndef FUTURENET_EDGE_HELPER_H
#define FUTURENET_EDGE_HELPER_H

#include "ns3/application-container.h"
#include "ns3/attribute.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"
#include "ns3/output-stream-wrapper.h"

#include <string>

namespace ns3
{

class OffloadPolicy;
class TaskClientApplication;
struct TaskClientRecord;

/**
 * @ingroup futurenet-edge
 * @brief Helper to install and configure futurenet-edge models and applications.
 */
class EdgeHelper
{
  public:
    EdgeHelper();
    ~EdgeHelper();

    /**
     * @brief Installs ComputeNodeModel onto each node in the container.
     * @param nodes Container of nodes.
     */
    void InstallComputeNode(NodeContainer nodes);

    /**
     * @brief Installs ComputeNodeModel onto a single node.
     * @param node Target node.
     */
    void InstallComputeNode(Ptr<Node> node);

    /**
     * @brief Installs TaskClientApplication onto each node in the container.
     * @param nodes Container of client nodes.
     * @return ApplicationContainer containing the installed client applications.
     */
    ApplicationContainer InstallClient(NodeContainer nodes);

    /**
     * @brief Installs TaskClientApplication onto a single node.
     * @param node Target client node.
     * @return ApplicationContainer containing the installed client application.
     */
    ApplicationContainer InstallClient(Ptr<Node> node);

    /**
     * @brief Installs EdgeComputeApplication onto each node in the container.
     * @param nodes Container of server nodes.
     * @return ApplicationContainer containing the installed server applications.
     */
    ApplicationContainer InstallServer(NodeContainer nodes);

    /**
     * @brief Installs EdgeComputeApplication onto a single node.
     * @param node Target server node.
     * @return ApplicationContainer containing the installed server application.
     */
    ApplicationContainer InstallServer(Ptr<Node> node);

    /**
     * @brief Sets an attribute on the ComputeNodeModel factory.
     * @param name Attribute name.
     * @param val Attribute value.
     */
    void SetComputeAttribute(std::string name, const AttributeValue& val);

    /**
     * @brief Sets an attribute on the TaskClientApplication factory.
     * @param name Attribute name.
     * @param val Attribute value.
     */
    void SetClientAttribute(std::string name, const AttributeValue& val);

    /**
     * @brief Sets an attribute on the EdgeComputeApplication factory.
     * @param name Attribute name.
     * @param val Attribute value.
     */
    void SetServerAttribute(std::string name, const AttributeValue& val);

    /**
     * @brief Sets the default offloading policy for newly installed clients.
     * @param policy Pointer to OffloadPolicy.
     */
    void SetPolicy(Ptr<OffloadPolicy> policy);

    /**
     * @brief Enables the mandatory 12-column M06 Task CSV log stream (M06 SRS Section 7).
     * Output format:
     * task_id,source,target,created_s,input_bytes,cycles,queue_ms,network_ms,compute_ms,e2e_ms,deadline_ms,completed
     * @param filePath Destination path for CSV log file.
     */
    void EnableCsvTracing(std::string filePath);

    /**
     * @brief Enables the 10-column FutureNet Common Guidelines event log stream (CG Section 4.5).
     * Output format:
     * time_s,module,event,node_id,peer_id,flow_id,value1,value2,unit,detail
     * @param filePath Destination path for event log file.
     */
    void EnableCommonEventLog(std::string filePath);

  private:
    ObjectFactory m_computeFactory;          //!< Factory for ComputeNodeModel
    ObjectFactory m_clientFactory;           //!< Factory for TaskClientApplication
    ObjectFactory m_serverFactory;           //!< Factory for EdgeComputeApplication
    Ptr<OffloadPolicy> m_defaultPolicy;      //!< Default policy assigned to clients
    Ptr<OutputStreamWrapper> m_csvStream;    //!< 12-column task log stream
    Ptr<OutputStreamWrapper> m_eventStream;  //!< 10-column event log stream

    std::vector<Ptr<TaskClientApplication>> m_installedClients;
    void HookCsvClient(Ptr<TaskClientApplication> client);
    static void TaskRecordCsvWriter(Ptr<OutputStreamWrapper> stream, const TaskClientRecord& record);
};

} // namespace ns3

#endif // FUTURENET_EDGE_HELPER_H

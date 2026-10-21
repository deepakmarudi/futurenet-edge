/**
 * \file edge-compute-application.h
 * \brief EdgeComputeApplication definition for M06 futurenet-edge module.
 * \author FutureNet Module Team
 *
 * Implements M06-FR-06 and M06-FR-07: Server application that receives
 * offloaded task chunks over TCP/UDP, gates queue entry until payload reception
 * completes (enforcing M06-FR-06 / Test T03), submits to local ComputeNodeModel,
 * and returns results or completion acknowledgments.
 */

#ifndef FUTURENET_EDGE_COMPUTE_APPLICATION_H
#define FUTURENET_EDGE_COMPUTE_APPLICATION_H

#include "compute-node-model.h"
#include "task-descriptor.h"
#include "task-packet-header.h"

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/socket.h"

#include <map>
#include <string>

namespace ns3
{

/**
 * \ingroup futurenet-edge
 * \brief Context maintained by the server while reassembling multi-chunk task payloads.
 */
struct RxTaskContext
{
    TaskDescriptor task;            //!< Workload metadata
    uint64_t bytesReceived{0};      //!< Running counter of received input bytes
    uint64_t totalExpectedBytes{0}; //!< Required upload size (header.GetInputBytes())
    Time timeFirstChunkRx{Seconds(0)};//!< Arrival timestamp of first segment
    Address clientAddress;          //!< Return network address of requesting client
};

/**
 * \ingroup futurenet-edge
 * \brief Server-side application running on edge/cloud nodes to process offloaded tasks.
 */
class EdgeComputeApplication : public Application
{
  public:
    /**
     * \brief Register this type with the ns-3 TypeId system.
     * \return TypeId instance.
     */
    static TypeId GetTypeId();

    EdgeComputeApplication();
    ~EdgeComputeApplication() override;

    /**
     * \brief Attaches the underlying compute engine (M06-FR-02, M06-FR-03).
     * \param computeModel Pointer to node's ComputeNodeModel.
     */
    void SetComputeNodeModel(Ptr<ComputeNodeModel> computeModel);

    /**
     * \brief Gets the attached compute engine.
     * \return Pointer to ComputeNodeModel.
     */
    Ptr<ComputeNodeModel> GetComputeNodeModel() const;

  protected:
    void StartApplication() override;
    void StopApplication() override;
    void DoDispose() override;

  private:
    void HandleAccept(Ptr<Socket> socket, const Address& from);
    void HandleRead(Ptr<Socket> socket);
    void ProcessIncomingChunk(const TaskPacketHeader& header,
                              Ptr<Packet> packet,
                              const Address& from);
    void OnTaskComputeFinished(const TaskDescriptor& task, Time waitTime, Time computeTime);
    void SendResult(const Address& clientAddr,
                    const TaskDescriptor& task,
                    Time waitTime,
                    Time computeTime);

    Ptr<ComputeNodeModel> m_computeModel;                 //!< Processing hardware model
    Ptr<Socket> m_listeningSocket;                        //!< Inbound listening socket
    std::map<uint64_t, RxTaskContext> m_assemblyMap;      //!< In-flight reassembly buffers
    std::map<uint64_t, Address> m_taskReturnAddressMap;  //!< Return endpoint per taskId

    // Attributes (M06 SRS Section 5)
    uint16_t m_port{9090};                                //!< Inbound listening port
    std::string m_protocol{"TCP"};                        //!< OffloadProtocol ("TCP" or "UDP")
    std::string m_resultAckMode{"ResultData"};            //!< ResultAckMode ("ResultData" or "AckOnly")
};

} // namespace ns3

#endif // FUTURENET_EDGE_COMPUTE_APPLICATION_H

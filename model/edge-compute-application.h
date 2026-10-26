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
#include "ns3/event-id.h"
#include "ns3/socket.h"

#include <map>
#include <set>
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
    Ptr<Socket> clientSocket;        //!< Accepted TCP connection; null for UDP
};

/**
 * \ingroup futurenet-edge
 * \brief Result data waiting to be sent in bounded wire-protocol chunks.
 */
struct TxTaskContext
{
    TaskDescriptor task;
    Address clientAddress;
    Ptr<Socket> socket;
    bool isUdp{false};
    bool ackOnly{false};
    bool hasPendingFrame{false};
    bool ackFrameSent{false};
    bool emptyResultFrameSent{false};
    uint32_t pendingChunkBytes{0};
    uint64_t bytesSent{0};
    Time queueDuration{Seconds(0)};
    Time computeDuration{Seconds(0)};
    Ptr<Packet> pendingFrame;
    EventId nextSendEvent;
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
    bool AcceptConnection(Ptr<Socket> socket, const Address& from);
    void HandleRead(Ptr<Socket> socket);
    void ProcessIncomingChunk(const TaskPacketHeader& header,
                              Ptr<Packet> packet,
                              const Address& from,
                              Ptr<Socket> clientSocket);
    void OnTaskComputeFinished(const TaskDescriptor& task, Time waitTime, Time computeTime);
    void SendResult(const Address& clientAddr,
                    const TaskDescriptor& task,
                    Time waitTime,
                    Time computeTime,
                    Ptr<Socket> clientSocket);
    void HandleSendReady(Ptr<Socket> socket, uint32_t txSpace);
    void HandlePeerClose(Ptr<Socket> socket);
    void HandlePeerError(Ptr<Socket> socket);
    void PumpResult(uint64_t taskId);
    void FinishResult(uint64_t taskId);
    void CleanupTcpSocket(Ptr<Socket> socket, bool closeSocket);
    void RejectIncomingTask(uint64_t taskId, Ptr<Socket> clientSocket, const std::string& reason);

    Ptr<ComputeNodeModel> m_computeModel;                 //!< Processing hardware model
    Ptr<Socket> m_listeningSocket;                        //!< Inbound listening socket
    std::map<uint64_t, RxTaskContext> m_assemblyMap;      //!< In-flight reassembly buffers
    std::map<uint64_t, Address> m_taskReturnAddressMap;  //!< Return endpoint per taskId
    std::map<uint64_t, Ptr<Socket>> m_taskTcpSocketMap;   //!< Accepted connection per TCP task
    std::map<uint64_t, TxTaskContext> m_resultTransmissions; //!< Per-task result send state
    std::map<const Socket*, Ptr<Packet>> m_tcpRxBuffers; //!< Framing buffer for each TCP connection
    std::map<const Socket*, Address> m_tcpPeerAddresses; //!< Peer address for accepted sockets
    std::map<const Socket*, uint64_t> m_tcpTaskIds;       //!< Task currently associated with a connection
    std::map<const Socket*, uint64_t> m_txTaskIds;        //!< Reverse lookup for send callbacks
    std::set<uint64_t> m_seenTaskIds;                    //!< Prevents duplicate task submission
    std::set<uint64_t> m_activeTaskIds;                  //!< Tasks admitted to the compute model

    // Attributes (M06 SRS Section 5)
    uint16_t m_port{9090};                                //!< Inbound listening port
    std::string m_protocol{"TCP"};                        //!< OffloadProtocol ("TCP" or "UDP")
    std::string m_resultAckMode{"ResultData"};            //!< ResultAckMode ("ResultData" or "AckOnly")
};

} // namespace ns3

#endif // FUTURENET_EDGE_COMPUTE_APPLICATION_H

/**
 * \file edge-compute-application.cc
 * \brief Server-side task reception, compute submission, and result transfer.
 */

#include "edge-compute-application.h"

#include "compute-queue.h"

#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-address.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FutureNetEdgeComputeApplication");
NS_OBJECT_ENSURE_REGISTERED(EdgeComputeApplication);

namespace
{
constexpr uint32_t TASK_HEADER_BYTES = 64;
constexpr uint32_t MAX_CHUNK_BYTES = 1400;
constexpr uint64_t MIN_TASK_INPUT_BYTES = 1000;
constexpr uint64_t MAX_TASK_BYTES = 1000000000ULL;
constexpr double MIN_REQUIRED_CYCLES = 1e6;
constexpr double MAX_REQUIRED_CYCLES = 1e13;
} // namespace

TypeId
EdgeComputeApplication::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::EdgeComputeApplication")
            .SetParent<Application>()
            .SetGroupName("FuturenetEdge")
            .AddConstructor<EdgeComputeApplication>()
            .AddAttribute("Port",
                          "Local TCP or UDP port used for incoming task requests.",
                          UintegerValue(9090),
                          MakeUintegerAccessor(&EdgeComputeApplication::m_port),
                          MakeUintegerChecker<uint16_t>(1, 65535))
            .AddAttribute("OffloadProtocol",
                          "Transport used for remote tasks: TCP or UDP.",
                          StringValue("TCP"),
                          MakeStringAccessor(&EdgeComputeApplication::m_protocol),
                          MakeStringChecker())
            .AddAttribute("ResultAckMode",
                          "ResultData returns output bytes; AckOnly returns a completion header.",
                          StringValue("ResultData"),
                          MakeStringAccessor(&EdgeComputeApplication::m_resultAckMode),
                          MakeStringChecker());
    return tid;
}

EdgeComputeApplication::EdgeComputeApplication()
{
    NS_LOG_FUNCTION(this);
}

EdgeComputeApplication::~EdgeComputeApplication()
{
    NS_LOG_FUNCTION(this);
}

void
EdgeComputeApplication::SetComputeNodeModel(Ptr<ComputeNodeModel> computeModel)
{
    m_computeModel = computeModel;
}

Ptr<ComputeNodeModel>
EdgeComputeApplication::GetComputeNodeModel() const
{
    return m_computeModel;
}

void
EdgeComputeApplication::StartApplication()
{
    NS_LOG_FUNCTION(this);

    if (m_computeModel == nullptr)
    {
        m_computeModel = GetNode()->GetObject<ComputeNodeModel>();
    }
    NS_ABORT_MSG_IF(m_computeModel == nullptr,
                    "EdgeComputeApplication requires a ComputeNodeModel on its node");
    NS_ABORT_MSG_IF(m_protocol != "TCP" && m_protocol != "UDP",
                    "OffloadProtocol must be TCP or UDP; got " << m_protocol);
    NS_ABORT_MSG_IF(m_resultAckMode != "ResultData" && m_resultAckMode != "AckOnly",
                    "ResultAckMode must be ResultData or AckOnly; got " << m_resultAckMode);

    const TypeId socketFactory = m_protocol == "TCP" ? TcpSocketFactory::GetTypeId()
                                                       : UdpSocketFactory::GetTypeId();
    m_listeningSocket = Socket::CreateSocket(GetNode(), socketFactory);
    NS_ABORT_MSG_IF(m_listeningSocket == nullptr, "Could not create server socket");

    const InetSocketAddress localAddress(Ipv4Address::GetAny(), m_port);
    NS_ABORT_MSG_IF(m_listeningSocket->Bind(localAddress) < 0,
                    "Could not bind EdgeComputeApplication to port " << m_port);

    if (m_protocol == "TCP")
    {
        NS_ABORT_MSG_IF(m_listeningSocket->Listen() < 0,
                        "Could not listen on TCP port " << m_port);
        m_listeningSocket->SetAcceptCallback(
            MakeCallback(&EdgeComputeApplication::AcceptConnection, this),
            MakeCallback(&EdgeComputeApplication::HandleAccept, this));
    }
    else
    {
        m_listeningSocket->SetRecvCallback(
            MakeCallback(&EdgeComputeApplication::HandleRead, this));
    }

    NS_LOG_INFO("EdgeComputeApplication on node " << GetNode()->GetId() << " listening on "
                                                   << m_protocol << " port " << m_port);
}

void
EdgeComputeApplication::StopApplication()
{
    NS_LOG_FUNCTION(this);

    if (m_listeningSocket != nullptr)
    {
        m_listeningSocket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        m_listeningSocket->SetAcceptCallback(
            MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
            MakeNullCallback<void, Ptr<Socket>, const Address&>());
    }

    std::vector<uint64_t> activeTasks(m_activeTaskIds.begin(), m_activeTaskIds.end());
    if (m_computeModel != nullptr)
    {
        for (uint64_t taskId : activeTasks)
        {
            m_computeModel->CancelTask(taskId);
        }
    }
    m_activeTaskIds.clear();

    std::vector<Ptr<Socket>> tcpSockets;
    for (const auto& task : m_taskTcpSocketMap)
    {
        tcpSockets.push_back(task.second);
    }
    for (const auto& task : m_assemblyMap)
    {
        if (task.second.clientSocket != nullptr)
        {
            tcpSockets.push_back(task.second.clientSocket);
        }
    }
    for (auto& result : m_resultTransmissions)
    {
        if (result.second.nextSendEvent.IsPending())
        {
            Simulator::Cancel(result.second.nextSendEvent);
        }
        if (!result.second.isUdp && result.second.socket != nullptr)
        {
            tcpSockets.push_back(result.second.socket);
        }
    }

    for (const auto& socket : tcpSockets)
    {
        CleanupTcpSocket(socket, true);
    }
    m_resultTransmissions.clear();
    m_assemblyMap.clear();
    m_taskReturnAddressMap.clear();
    m_taskTcpSocketMap.clear();
    m_tcpRxBuffers.clear();
    m_tcpPeerAddresses.clear();
    m_tcpTaskIds.clear();
    m_txTaskIds.clear();
    m_seenTaskIds.clear();

    if (m_listeningSocket != nullptr)
    {
        m_listeningSocket->Close();
        m_listeningSocket = nullptr;
    }
}

void
EdgeComputeApplication::DoDispose()
{
    NS_LOG_FUNCTION(this);
    StopApplication();
    m_computeModel = nullptr;
    Application::DoDispose();
}

bool
EdgeComputeApplication::AcceptConnection(Ptr<Socket> socket, const Address& from)
{
    (void)from;
    return socket != nullptr && m_protocol == "TCP";
}

void
EdgeComputeApplication::HandleAccept(Ptr<Socket> socket, const Address& from)
{
    NS_LOG_FUNCTION(this << socket << from);
    if (socket == nullptr)
    {
        return;
    }

    socket->SetRecvCallback(MakeCallback(&EdgeComputeApplication::HandleRead, this));
    socket->SetSendCallback(MakeCallback(&EdgeComputeApplication::HandleSendReady, this));
    socket->SetCloseCallbacks(MakeCallback(&EdgeComputeApplication::HandlePeerClose, this),
                              MakeCallback(&EdgeComputeApplication::HandlePeerError, this));
    m_tcpRxBuffers[PeekPointer(socket)] = Create<Packet>();
    m_tcpPeerAddresses[PeekPointer(socket)] = from;
}

void
EdgeComputeApplication::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    if (m_protocol == "UDP" && socket == m_listeningSocket)
    {
        Address from;
        Ptr<Packet> datagram;
        while ((datagram = socket->RecvFrom(from)) != nullptr)
        {
            if (datagram->GetSize() < TASK_HEADER_BYTES)
            {
                NS_LOG_WARN("Discarding short UDP task datagram");
                continue;
            }

            TaskPacketHeader header;
            if (datagram->RemoveHeader(header) != TASK_HEADER_BYTES ||
                header.GetChunkBytes() > MAX_CHUNK_BYTES ||
                datagram->GetSize() != header.GetChunkBytes())
            {
                NS_LOG_WARN("Discarding malformed UDP task datagram");
                continue;
            }
            ProcessIncomingChunk(header, datagram, from, nullptr);
        }
        return;
    }

    if (m_protocol != "TCP" || socket == m_listeningSocket)
    {
        return;
    }

    const Socket* socketKey = PeekPointer(socket);
    auto bufferIt = m_tcpRxBuffers.find(socketKey);
    if (bufferIt == m_tcpRxBuffers.end())
    {
        m_tcpRxBuffers[socketKey] = Create<Packet>();
        bufferIt = m_tcpRxBuffers.find(socketKey);
    }

    Ptr<Packet> incoming;
    while ((incoming = socket->Recv()) != nullptr)
    {
        if (incoming->GetSize() == 0)
        {
            HandlePeerClose(socket);
            return;
        }
        bufferIt = m_tcpRxBuffers.find(socketKey);
        if (bufferIt == m_tcpRxBuffers.end())
        {
            return;
        }
        bufferIt->second->AddAtEnd(incoming);
    }

    while (true)
    {
        bufferIt = m_tcpRxBuffers.find(socketKey);
        if (bufferIt == m_tcpRxBuffers.end() || bufferIt->second->GetSize() < TASK_HEADER_BYTES)
        {
            return;
        }

        Ptr<Packet> buffer = bufferIt->second;
        Ptr<Packet> headerPacket = buffer->Copy();
        TaskPacketHeader peekHeader;
        if (headerPacket->RemoveHeader(peekHeader) != TASK_HEADER_BYTES ||
            peekHeader.GetChunkBytes() > MAX_CHUNK_BYTES)
        {
            NS_LOG_WARN("Closing TCP task connection with an invalid frame header");
            CleanupTcpSocket(socket, true);
            return;
        }

        const uint32_t frameBytes = TASK_HEADER_BYTES + peekHeader.GetChunkBytes();
        if (buffer->GetSize() < frameBytes)
        {
            return;
        }

        TaskPacketHeader header;
        if (buffer->RemoveHeader(header) != TASK_HEADER_BYTES)
        {
            CleanupTcpSocket(socket, true);
            return;
        }
        Ptr<Packet> payload = buffer->CreateFragment(0, header.GetChunkBytes());
        buffer->RemoveAtStart(header.GetChunkBytes());

        Address from;
        auto peerIt = m_tcpPeerAddresses.find(socketKey);
        if (peerIt != m_tcpPeerAddresses.end())
        {
            from = peerIt->second;
        }
        else if (socket->GetPeerName(from) < 0)
        {
            CleanupTcpSocket(socket, true);
            return;
        }

        ProcessIncomingChunk(header, payload, from, socket);
        if (m_tcpRxBuffers.find(socketKey) == m_tcpRxBuffers.end())
        {
            return;
        }
    }
}

void
EdgeComputeApplication::ProcessIncomingChunk(const TaskPacketHeader& header,
                                             Ptr<Packet> packet,
                                             const Address& from,
                                             Ptr<Socket> clientSocket)
{
    const uint64_t taskId = header.GetTaskId();
    const bool isTcp = clientSocket != nullptr;
    const uint32_t serverNodeId = GetNode()->GetId();

    if (header.GetPacketType() != TaskPacketHeader::TASK_REQUEST ||
        header.GetTargetNodeId() != serverNodeId || packet == nullptr ||
        packet->GetSize() != header.GetChunkBytes() ||
        header.GetChunkBytes() > MAX_CHUNK_BYTES)
    {
        RejectIncomingTask(taskId, clientSocket, "invalid request header or payload");
        return;
    }
    if (header.GetInputBytes() < MIN_TASK_INPUT_BYTES ||
        header.GetInputBytes() > MAX_TASK_BYTES ||
        header.GetOutputBytes() > MAX_TASK_BYTES ||
        !std::isfinite(header.GetRequiredCycles()) ||
        header.GetRequiredCycles() < MIN_REQUIRED_CYCLES ||
        header.GetRequiredCycles() > MAX_REQUIRED_CYCLES)
    {
        RejectIncomingTask(taskId, clientSocket, "task metadata is outside the M06 SRS range");
        return;
    }

    auto contextIt = m_assemblyMap.find(taskId);
    if (contextIt == m_assemblyMap.end())
    {
        if (m_seenTaskIds.find(taskId) != m_seenTaskIds.end())
        {
            NS_LOG_WARN("Ignoring duplicate request for task " << taskId);
            return;
        }
        if (isTcp)
        {
            auto connectionTask = m_tcpTaskIds.find(PeekPointer(clientSocket));
            if (connectionTask != m_tcpTaskIds.end() && connectionTask->second != taskId)
            {
                RejectIncomingTask(taskId, clientSocket,
                                   "one TCP connection may carry only one task");
                return;
            }
            m_tcpTaskIds[PeekPointer(clientSocket)] = taskId;
        }

        RxTaskContext context;
        context.task = TaskDescriptor(taskId,
                                      header.GetSourceNodeId(),
                                      header.GetInputBytes(),
                                      header.GetOutputBytes(),
                                      header.GetRequiredCycles(),
                                      Simulator::Now());
        context.totalExpectedBytes = header.GetInputBytes();
        context.timeFirstChunkRx = Simulator::Now();
        context.clientAddress = from;
        context.clientSocket = clientSocket;
        contextIt = m_assemblyMap.emplace(taskId, context).first;
        m_seenTaskIds.insert(taskId);
    }
    else
    {
        const TaskDescriptor& existing = contextIt->second.task;
        if (existing.GetSourceNodeId() != header.GetSourceNodeId() ||
            existing.GetInputBytes() != header.GetInputBytes() ||
            existing.GetOutputBytes() != header.GetOutputBytes() ||
            existing.GetRequiredCycles() != header.GetRequiredCycles() ||
            contextIt->second.clientAddress != from)
        {
            RejectIncomingTask(taskId, clientSocket, "inconsistent metadata across task chunks");
            return;
        }
    }

    RxTaskContext& context = contextIt->second;
    if (isTcp && context.clientSocket != clientSocket)
    {
        RejectIncomingTask(taskId, clientSocket, "task chunks arrived on different TCP connections");
        return;
    }
    if ((context.totalExpectedBytes > 0 && header.GetChunkBytes() == 0) ||
        context.bytesReceived > context.totalExpectedBytes ||
        header.GetChunkBytes() > context.totalExpectedBytes - context.bytesReceived)
    {
        RejectIncomingTask(taskId, clientSocket, "task payload exceeds declared input size");
        return;
    }

    context.bytesReceived += header.GetChunkBytes();
    if (context.bytesReceived < context.totalExpectedBytes)
    {
        return; // Strict M06-FR-06 gate: do not enqueue until all input bytes arrive.
    }
    if (context.bytesReceived != context.totalExpectedBytes)
    {
        RejectIncomingTask(taskId, clientSocket, "received byte count does not match input size");
        return;
    }

    RxTaskContext completed = context;
    m_assemblyMap.erase(contextIt);
    m_taskReturnAddressMap[taskId] = from;
    if (isTcp)
    {
        m_taskTcpSocketMap[taskId] = clientSocket;
    }

    const bool admitted = m_computeModel->SubmitTask(
        completed.task,
        MakeCallback(&EdgeComputeApplication::OnTaskComputeFinished, this));
    if (!admitted)
    {
        RejectIncomingTask(taskId, clientSocket, "compute queue is full");
        return;
    }
    m_activeTaskIds.insert(taskId);

    NS_LOG_INFO("Received all " << completed.totalExpectedBytes << " input bytes for task "
                                << taskId << " on node " << serverNodeId
                                << "; task admitted to compute queue");
}

void
EdgeComputeApplication::OnTaskComputeFinished(const TaskDescriptor& task,
                                               Time waitTime,
                                               Time computeTime)
{
    const uint64_t taskId = task.GetTaskId();
    NS_LOG_INFO("Task " << taskId << " finished on edge: queue="
                         << waitTime.GetNanoSeconds() << " ns, compute="
                         << computeTime.GetNanoSeconds() << " ns");
    if (m_activeTaskIds.erase(taskId) == 0)
    {
        return; // The application stopped or the task was cancelled.
    }

    auto addressIt = m_taskReturnAddressMap.find(taskId);
    if (addressIt == m_taskReturnAddressMap.end())
    {
        NS_LOG_WARN("No return address remains for completed task " << taskId);
        return;
    }

    Ptr<Socket> clientSocket;
    if (m_protocol == "TCP")
    {
        auto socketIt = m_taskTcpSocketMap.find(taskId);
        if (socketIt == m_taskTcpSocketMap.end())
        {
            m_taskReturnAddressMap.erase(addressIt);
            NS_LOG_WARN("TCP client disconnected before task " << taskId << " completed");
            return;
        }
        clientSocket = socketIt->second;
    }

    SendResult(addressIt->second, task, waitTime, computeTime, clientSocket);
}

void
EdgeComputeApplication::SendResult(const Address& clientAddr,
                                   const TaskDescriptor& task,
                                   Time waitTime,
                                   Time computeTime,
                                   Ptr<Socket> clientSocket)
{
    Ptr<Socket> txSocket = m_protocol == "UDP" ? m_listeningSocket : clientSocket;
    if (txSocket == nullptr)
    {
        NS_LOG_WARN("Cannot return result for task " << task.GetTaskId()
                                                     << ": client socket is unavailable");
        m_taskReturnAddressMap.erase(task.GetTaskId());
        m_taskTcpSocketMap.erase(task.GetTaskId());
        return;
    }

    TxTaskContext context;
    context.task = task;
    context.clientAddress = clientAddr;
    context.socket = txSocket;
    context.isUdp = m_protocol == "UDP";
    context.ackOnly = m_resultAckMode == "AckOnly";
    context.queueDuration = waitTime;
    context.computeDuration = computeTime;
    m_resultTransmissions[task.GetTaskId()] = context;
    if (!context.isUdp)
    {
        m_txTaskIds[PeekPointer(txSocket)] = task.GetTaskId();
    }
    PumpResult(task.GetTaskId());
}

void
EdgeComputeApplication::HandleSendReady(Ptr<Socket> socket, uint32_t txSpace)
{
    (void)txSpace;
    auto taskIt = m_txTaskIds.find(PeekPointer(socket));
    if (taskIt != m_txTaskIds.end())
    {
        PumpResult(taskIt->second);
    }
}

void
EdgeComputeApplication::PumpResult(uint64_t taskId)
{
    auto contextIt = m_resultTransmissions.find(taskId);
    if (contextIt == m_resultTransmissions.end())
    {
        return;
    }
    TxTaskContext& context = contextIt->second;

    while (true)
    {
        if (!context.hasPendingFrame)
        {
            if (context.ackOnly)
            {
                if (context.ackFrameSent)
                {
                    FinishResult(taskId);
                    return;
                }
                context.pendingChunkBytes = 0;
            }
            else
            {
                const uint64_t outputBytes = context.task.GetOutputBytes();
                if (outputBytes == 0)
                {
                    if (context.emptyResultFrameSent)
                    {
                        FinishResult(taskId);
                        return;
                    }
                    context.pendingChunkBytes = 0;
                }
                else if (context.bytesSent >= outputBytes)
                {
                    FinishResult(taskId);
                    return;
                }
                context.pendingChunkBytes = static_cast<uint32_t>(
                    std::min<uint64_t>(outputBytes - context.bytesSent, MAX_CHUNK_BYTES));
            }

            TaskPacketHeader header;
            header.SetPacketType(context.ackOnly ? TaskPacketHeader::TASK_ACK_ONLY
                                                 : TaskPacketHeader::TASK_RESULT);
            header.SetSourceNodeId(context.task.GetSourceNodeId());
            header.SetTargetNodeId(GetNode()->GetId());
            header.SetChunkBytes(context.pendingChunkBytes);
            header.SetTaskId(taskId);
            header.SetInputBytes(context.task.GetInputBytes());
            header.SetOutputBytes(context.task.GetOutputBytes());
            header.SetRequiredCycles(context.task.GetRequiredCycles());
            NS_LOG_DEBUG("Returning task " << taskId << " with queue duration "
                                            << context.queueDuration.GetNanoSeconds() << " ns");
            header.SetQueueDuration(context.queueDuration);
            header.SetComputeDuration(context.computeDuration);

            context.pendingFrame = Create<Packet>(context.pendingChunkBytes);
            context.pendingFrame->AddHeader(header);
            context.hasPendingFrame = true;
        }

        const uint32_t frameBytes = context.pendingFrame->GetSize();
        const int sent = context.isUdp
                             ? context.socket->SendTo(context.pendingFrame,
                                                      0,
                                                      context.clientAddress)
                             : context.socket->Send(context.pendingFrame);
        if (sent < 0 || sent == 0)
        {
            if (context.isUdp)
            {
                NS_LOG_WARN("UDP result send failed for task " << taskId);
                FinishResult(taskId);
            }
            return; // TCP retries from HandleSendReady when buffer space becomes available.
        }
        if (context.isUdp && static_cast<uint32_t>(sent) != frameBytes)
        {
            NS_LOG_WARN("UDP accepted a partial result datagram for task " << taskId);
            FinishResult(taskId);
            return;
        }
        if (static_cast<uint32_t>(sent) < frameBytes)
        {
            context.pendingFrame = context.pendingFrame->CreateFragment(
                static_cast<uint32_t>(sent), frameBytes - static_cast<uint32_t>(sent));
            return;
        }

        context.hasPendingFrame = false;
        context.pendingFrame = nullptr;
        if (context.ackOnly)
        {
            context.ackFrameSent = true;
            FinishResult(taskId);
            return;
        }
        if (context.task.GetOutputBytes() == 0)
        {
            context.emptyResultFrameSent = true;
            FinishResult(taskId);
            return;
        }
        context.bytesSent += context.pendingChunkBytes;
        context.pendingChunkBytes = 0;

        if (context.isUdp)
        {
            context.nextSendEvent = Simulator::ScheduleNow(
                &EdgeComputeApplication::PumpResult,
                this,
                taskId);
            return;
        }
    }
}

void
EdgeComputeApplication::FinishResult(uint64_t taskId)
{
    auto contextIt = m_resultTransmissions.find(taskId);
    if (contextIt != m_resultTransmissions.end() &&
        contextIt->second.nextSendEvent.IsPending())
    {
        Simulator::Cancel(contextIt->second.nextSendEvent);
    }

    Ptr<Socket> tcpSocket;
    auto socketIt = m_taskTcpSocketMap.find(taskId);
    if (socketIt != m_taskTcpSocketMap.end())
    {
        tcpSocket = socketIt->second;
    }
    m_resultTransmissions.erase(taskId);
    m_taskReturnAddressMap.erase(taskId);
    m_taskTcpSocketMap.erase(taskId);

    if (tcpSocket != nullptr)
    {
        CleanupTcpSocket(tcpSocket, true);
    }
}

void
EdgeComputeApplication::HandlePeerClose(Ptr<Socket> socket)
{
    CleanupTcpSocket(socket, false);
}

void
EdgeComputeApplication::HandlePeerError(Ptr<Socket> socket)
{
    CleanupTcpSocket(socket, true);
}

void
EdgeComputeApplication::CleanupTcpSocket(Ptr<Socket> socket, bool closeSocket)
{
    if (socket == nullptr)
    {
        return;
    }
    const Socket* socketKey = PeekPointer(socket);

    if (closeSocket)
    {
        socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        socket->SetSendCallback(MakeNullCallback<void, Ptr<Socket>, uint32_t>());
        socket->SetCloseCallbacks(MakeNullCallback<void, Ptr<Socket>>(),
                                  MakeNullCallback<void, Ptr<Socket>>());
        socket->Close();
    }

    std::vector<uint64_t> taskIds;
    auto tcpTask = m_tcpTaskIds.find(socketKey);
    if (tcpTask != m_tcpTaskIds.end())
    {
        taskIds.push_back(tcpTask->second);
        m_tcpTaskIds.erase(tcpTask);
    }
    auto txTask = m_txTaskIds.find(socketKey);
    if (txTask != m_txTaskIds.end())
    {
        taskIds.push_back(txTask->second);
        m_txTaskIds.erase(txTask);
    }

    for (auto it = m_taskTcpSocketMap.begin(); it != m_taskTcpSocketMap.end();)
    {
        if (PeekPointer(it->second) == socketKey)
        {
            taskIds.push_back(it->first);
            it = m_taskTcpSocketMap.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (auto it = m_assemblyMap.begin(); it != m_assemblyMap.end();)
    {
        if (it->second.clientSocket == socket)
        {
            taskIds.push_back(it->first);
            it = m_assemblyMap.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (uint64_t taskId : taskIds)
    {
        m_taskReturnAddressMap.erase(taskId);
        auto result = m_resultTransmissions.find(taskId);
        if (result != m_resultTransmissions.end())
        {
            if (result->second.nextSendEvent.IsPending())
            {
                Simulator::Cancel(result->second.nextSendEvent);
            }
            m_resultTransmissions.erase(result);
        }
    }

    m_tcpRxBuffers.erase(socketKey);
    m_tcpPeerAddresses.erase(socketKey);
}

void
EdgeComputeApplication::RejectIncomingTask(uint64_t taskId,
                                           Ptr<Socket> clientSocket,
                                           const std::string& reason)
{
    NS_LOG_WARN("Rejecting task " << taskId << ": " << reason);
    m_assemblyMap.erase(taskId);
    m_seenTaskIds.insert(taskId);
    m_taskReturnAddressMap.erase(taskId);
    m_taskTcpSocketMap.erase(taskId);
    if (clientSocket != nullptr)
    {
        CleanupTcpSocket(clientSocket, true);
    }
}

} // namespace ns3

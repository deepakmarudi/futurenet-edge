/**
 * @file task-client-application.cc
 * @brief Implementation of TaskClientApplication for M06 futurenet-edge.
 */

#include "task-client-application.h"

#include "compute-queue.h"

#include "ns3/double.h"
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
#include <limits>
#include <utility>
#include <vector>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FutureNetTaskClientApplication");
NS_OBJECT_ENSURE_REGISTERED(TaskClientApplication);

namespace
{
constexpr uint32_t MAX_CHUNK_BYTES = 1400;
constexpr uint32_t TASK_HEADER_BYTES = 64;
constexpr double DEFAULT_COMPUTE_CAPACITY = 1e9;
constexpr double DEFAULT_DATA_RATE_MBPS = 100.0;
constexpr double DEFAULT_DELAY_MS = 5.0;
} // namespace

TypeId
TaskClientApplication::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::TaskClientApplication")
            .SetParent<Application>()
            .SetGroupName("FuturenetEdge")
            .AddConstructor<TaskClientApplication>()
                        .AddAttribute("TaskInputBytes",
                          "Input payload size generated for each task, in bytes.",
                          UintegerValue(1000000),
                          MakeUintegerAccessor(&TaskClientApplication::m_inputBytes),
                          MakeUintegerChecker<uint64_t>(1000, 1000000000))
            .AddAttribute("TaskOutputBytes",
                          "Fixed output payload size generated for each task, in bytes.",
                          UintegerValue(100000),
                          MakeUintegerAccessor(&TaskClientApplication::m_outputBytes),
                          MakeUintegerChecker<uint64_t>(0, 1000000000))
            .AddAttribute("TaskOutputRatio",
                          "When non-negative, derives output bytes as input bytes times this ratio.",
                          DoubleValue(-1.0),
                          MakeDoubleAccessor(&TaskClientApplication::m_outputRatio),
                          MakeDoubleChecker<double>(-1.0, 10.0))
            .AddAttribute("RequiredCycles",
                          "Abstract CPU cycles required by each generated task.",
                          DoubleValue(1e9),
                          MakeDoubleAccessor(&TaskClientApplication::m_requiredCycles),
                          MakeDoubleChecker<double>(1e6, 1e13))
            .AddAttribute("TaskArrivalInterval",
                          "Mean interval between generated tasks.",
                          TimeValue(Seconds(1.0)),
                          MakeTimeAccessor(&TaskClientApplication::m_taskArrivalInterval),
                          MakeTimeChecker(Seconds(0), Seconds(1000)))
            .AddAttribute("TaskDeadline",
                          "Optional deadline duration measured from task creation; zero disables it.",
                          TimeValue(Seconds(0)),
                          MakeTimeAccessor(&TaskClientApplication::m_taskDeadline),
                          MakeTimeChecker(Seconds(0), Seconds(10000)))
            .AddAttribute("ResultAckMode",
                          "ResultData returns output bytes; AckOnly returns a completion header.",
                          StringValue("ResultData"),
                          MakeStringAccessor(&TaskClientApplication::m_resultAckMode),
                          MakeStringChecker())
            .AddAttribute("OffloadProtocol",
                          "Transport used for remote task transfer: TCP or UDP.",
                          StringValue("TCP"),
                          MakeStringAccessor(&TaskClientApplication::m_offloadProtocol),
                          MakeStringChecker())
            .AddAttribute("PolicyEvaluationInterval",
                          "Refreshes remote queue and capacity snapshots at this interval; zero refreshes per task.",
                          TimeValue(Seconds(0)),
                          MakeTimeAccessor(&TaskClientApplication::m_policyInterval),
                          MakeTimeChecker(Seconds(0), Seconds(10)))
            .AddTraceSource("TaskCreated",
                            "Fired when a task is created.",
                            MakeTraceSourceAccessor(&TaskClientApplication::m_traceTaskCreated),
                            "ns3::TracedCallback::Uint64Uint32")
            .AddTraceSource("TaskOffloaded",
                            "Fired when a task is sent to a remote compute node.",
                            MakeTraceSourceAccessor(&TaskClientApplication::m_traceTaskOffloaded),
                            "ns3::TracedCallback::Uint64Uint32Uint32")
            .AddTraceSource("TaskCompleted",
                            "Fired when a task completes, with its end-to-end duration.",
                            MakeTraceSourceAccessor(&TaskClientApplication::m_traceTaskCompleted),
                            "ns3::TracedCallback::Uint64Time")
            .AddTraceSource("TaskDeadlineMiss",
                            "Fired when a completed task misses its configured deadline.",
                            MakeTraceSourceAccessor(&TaskClientApplication::m_traceTaskDeadlineMiss),
                            "ns3::TracedCallback::Uint64Time")
            .AddTraceSource("TaskRecordCompleted",
                            "Fired when a task completes, providing full milestone timing records.",
                            MakeTraceSourceAccessor(&TaskClientApplication::m_traceTaskRecordCompleted),
                            "ns3::TaskClientApplication::TaskRecordTracedCallback");
    return tid;
}

TaskClientApplication::TaskClientApplication()
    : m_policy(CreateObject<LocalOnlyPolicy>()),
      m_arrivalRng(CreateObject<UniformRandomVariable>())
{
    NS_LOG_FUNCTION(this);
    m_arrivalRng->SetAttribute("Min", DoubleValue(0.9));
    m_arrivalRng->SetAttribute("Max", DoubleValue(1.1));
}

TaskClientApplication::~TaskClientApplication()
{
    NS_LOG_FUNCTION(this);
}

void
TaskClientApplication::AddComputeTarget(Ptr<Node> targetNode,
                                        Address targetAddress,
                                        double estimatedDataRateMbps,
                                        double estimatedDelayMs)
{
    NS_ASSERT_MSG(targetNode != nullptr, "Cannot add a null compute target node");
    NS_ASSERT_MSG(!targetAddress.IsInvalid(), "Compute target address must be valid");

    ComputeTarget target;
    target.node = targetNode;
    target.state.nodeId = targetNode->GetId();
    target.state.isLocal = false;
    target.state.serverAddress = targetAddress;
    target.state.computeCapacityCyclesPerS = DEFAULT_COMPUTE_CAPACITY;
    target.state.estimatedDataRateMbps = estimatedDataRateMbps > 0.0
                                             ? estimatedDataRateMbps
                                             : DEFAULT_DATA_RATE_MBPS;
    target.state.estimatedOneWayDelayMs = std::max(0.0, estimatedDelayMs);
    target.state.distanceMeters = 0.0;

    for (auto& registered : m_computeTargets)
    {
        if (registered.state.nodeId == target.state.nodeId)
        {
            registered = target;
            RefreshCandidateStates();
            return;
        }
    }

    m_computeTargets.push_back(target);
    RefreshCandidateStates();
}

void
TaskClientApplication::SetOffloadPolicy(Ptr<OffloadPolicy> policy)
{
    if (policy != nullptr)
    {
        m_policy = policy;
    }
    else
    {
        m_policy = CreateObject<LocalOnlyPolicy>();
    }
}

bool
TaskClientApplication::GetTaskRecord(uint64_t taskId, TaskClientRecord& record) const
{
    auto it = m_records.find(taskId);
    if (it != m_records.end())
    {
        record = it->second;
        return true;
    }
    return false;
}

TaskStatus
TaskClientApplication::GetTaskStatus(uint64_t taskId) const
{
    auto it = m_records.find(taskId);
    return it == m_records.end() ? TaskStatus::CANCELLED : it->second.status;
}

int64_t
TaskClientApplication::AssignStreams(int64_t stream)
{
    if (m_arrivalRng == nullptr)
    {
        m_arrivalRng = CreateObject<UniformRandomVariable>();
        m_arrivalRng->SetAttribute("Min", DoubleValue(0.9));
        m_arrivalRng->SetAttribute("Max", DoubleValue(1.1));
    }
    m_arrivalRng->SetStream(stream);
    return 1;
}

void
TaskClientApplication::StartApplication()
{
    NS_LOG_FUNCTION(this);

    m_applicationActive = true;
    m_localCompute = GetNode()->GetObject<ComputeNodeModel>();
    if (m_localCompute == nullptr)
    {
        NS_LOG_WARN("Client node " << GetNode()->GetId()
                                   << " has no ComputeNodeModel; local-policy tasks will fail");
    }

    RefreshCandidateStates();
    ScheduleNextTask();
}

void
TaskClientApplication::StopApplication()
{
    NS_LOG_FUNCTION(this);

    m_applicationActive = false;
    if (m_nextTaskEvent.IsPending())
    {
        Simulator::Cancel(m_nextTaskEvent);
    }
    if (m_policyRefreshEvent.IsPending())
    {
        Simulator::Cancel(m_policyRefreshEvent);
    }

    std::vector<uint64_t> activeTasks;
    activeTasks.reserve(m_transfers.size());
    for (const auto& transfer : m_transfers)
    {
        activeTasks.push_back(transfer.first);
    }
    for (uint64_t taskId : activeTasks)
    {
        auto record = m_records.find(taskId);
        if (record != m_records.end() && record->second.status != TaskStatus::COMPLETED)
        {
            record->second.status = TaskStatus::CANCELLED;
        }
        CloseTransfer(taskId);
    }

    if (m_localCompute != nullptr)
    {
        const uint32_t localNodeId = GetNode()->GetId();
        for (auto& entry : m_records)
        {
            if (entry.second.targetNodeId == localNodeId &&
                entry.second.status != TaskStatus::COMPLETED &&
                entry.second.status != TaskStatus::FAILED_QUEUE_FULL &&
                entry.second.status != TaskStatus::CANCELLED)
            {
                m_localCompute->CancelTask(entry.first);
                entry.second.status = TaskStatus::CANCELLED;
            }
        }
    }
}

void
TaskClientApplication::DoDispose()
{
    NS_LOG_FUNCTION(this);

    StopApplication();
    m_localCompute = nullptr;
    m_policy = nullptr;
    m_computeTargets.clear();
    m_candidateStates.clear();
    m_records.clear();
    m_transfers.clear();
    m_socketTaskIds.clear();
    m_arrivalRng = nullptr;
    Application::DoDispose();
}

void
TaskClientApplication::ScheduleNextTask()
{
    if (m_taskArrivalInterval <= Seconds(0))
    {
        return;
    }

    const double jitter = m_arrivalRng != nullptr ? m_arrivalRng->GetValue() : 1.0;
    const Time delay = Seconds(m_taskArrivalInterval.GetSeconds() * jitter);
    m_nextTaskEvent = Simulator::Schedule(delay,
                                          &TaskClientApplication::GenerateAndScheduleNextTask,
                                          this);
}

void
TaskClientApplication::GenerateAndScheduleNextTask()
{
    GenerateTask();
    ScheduleNextTask();
}

void
TaskClientApplication::RefreshCandidateStates()
{
    m_candidateStates.clear();

    ComputeNodeState local;
    local.nodeId = GetNode() != nullptr ? GetNode()->GetId() : 0;
    local.isLocal = true;
    local.computeCapacityCyclesPerS = m_localCompute != nullptr
                                          ? m_localCompute->GetComputeCapacity()
                                          : DEFAULT_COMPUTE_CAPACITY;
    local.estimatedDataRateMbps = DEFAULT_DATA_RATE_MBPS;
    local.estimatedOneWayDelayMs = 0.0;
    if (m_localCompute != nullptr)
    {
        local.queueLength = m_localCompute->GetQueueLength();
        Ptr<ComputeQueue> queue = m_localCompute->GetQueue();
        if (queue != nullptr)
        {
            local.maxQueueLength = queue->GetMaxQueueLength();
        }
        TaskDescriptor emptyTask;
        emptyTask.SetRequiredCycles(0.0);
        local.estimatedWaitTime = m_localCompute->EstimateCompletion(emptyTask);
    }
    m_candidateStates.push_back(local);

    for (const auto& target : m_computeTargets)
    {
        ComputeNodeState state = target.state;
        Ptr<ComputeNodeModel> compute = target.node->GetObject<ComputeNodeModel>();
        if (compute != nullptr)
        {
            state.computeCapacityCyclesPerS = compute->GetComputeCapacity();
            state.queueLength = compute->GetQueueLength();
            Ptr<ComputeQueue> queue = compute->GetQueue();
            if (queue != nullptr)
            {
                state.maxQueueLength = queue->GetMaxQueueLength();
            }
            TaskDescriptor emptyTask;
            emptyTask.SetRequiredCycles(0.0);
            state.estimatedWaitTime = compute->EstimateCompletion(emptyTask);
        }
        m_candidateStates.push_back(state);
    }

    if (m_policyInterval > Seconds(0) && m_applicationActive)
    {
        if (m_policyRefreshEvent.IsPending())
        {
            Simulator::Cancel(m_policyRefreshEvent);
        }
        m_policyRefreshEvent = Simulator::Schedule(m_policyInterval,
                                                   &TaskClientApplication::RefreshCandidateStates,
                                                   this);
    }
}

uint64_t
TaskClientApplication::GenerateTask()
{
    const uint32_t sourceNodeId = GetNode() != nullptr ? GetNode()->GetId() : 0;
    const uint64_t sequence = m_nextTaskId++ & 0xffffffffULL;
    const uint64_t taskId = (static_cast<uint64_t>(sourceNodeId) << 32) | sequence;
    const Time now = Simulator::Now();

    TaskDescriptor task(taskId,
                        sourceNodeId,
                        m_inputBytes,
                        m_outputBytes,
                        m_requiredCycles,
                        now);
    if (m_outputRatio >= 0.0)
    {
        task.SetOutputRatio(m_outputRatio);
    }
    if (m_taskDeadline > Seconds(0))
    {
        task.SetDeadline(now + m_taskDeadline);
    }

    TaskClientRecord record;
    record.task = task;
    record.timeCreated = now;
    record.status = TaskStatus::CREATED;
    auto inserted = m_records.emplace(taskId, record);
    if (!inserted.second)
    {
        NS_LOG_ERROR("Generated duplicate task ID " << taskId);
        return taskId;
    }

    m_traceTaskCreated(taskId, sourceNodeId);
    if (m_policyInterval <= Seconds(0))
    {
        RefreshCandidateStates();
    }

    OffloadDecision decision = m_policy != nullptr
                                   ? m_policy->Select(task, m_candidateStates)
                                   : OffloadDecision(true, sourceNodeId);
    if (decision.executeLocally)
    {
        ExecuteLocally(task);
        return taskId;
    }

    auto target = std::find_if(m_candidateStates.begin(),
                               m_candidateStates.end(),
                               [&decision](const ComputeNodeState& candidate) {
                                   return !candidate.isLocal &&
                                          candidate.nodeId == decision.targetNodeId;
                               });
    if (target == m_candidateStates.end())
    {
        NS_LOG_WARN("Policy selected unknown remote node " << decision.targetNodeId
                                                             << "; executing task locally");
        ExecuteLocally(task);
        return taskId;
    }

    OffloadRemotely(task, *target);
    return taskId;
}

void
TaskClientApplication::ExecuteLocally(const TaskDescriptor& task)
{
    auto record = m_records.find(task.GetTaskId());
    if (record == m_records.end())
    {
        return;
    }
    record->second.targetNodeId = GetNode()->GetId();

    if (m_localCompute == nullptr)
    {
        record->second.status = TaskStatus::FAILED_NETWORK;
        NS_LOG_ERROR("Cannot execute task " << task.GetTaskId()
                                             << " locally: client has no ComputeNodeModel");
        return;
    }

    record->second.status = TaskStatus::QUEUED;
    const bool admitted = m_localCompute->SubmitTask(
        task,
        MakeCallback(&TaskClientApplication::OnLocalExecutionComplete, this));
    if (!admitted)
    {
        record->second.status = TaskStatus::FAILED_QUEUE_FULL;
    }
}

void
TaskClientApplication::OffloadRemotely(const TaskDescriptor& task,
                                       const ComputeNodeState& target)
{
    auto record = m_records.find(task.GetTaskId());
    if (record == m_records.end())
    {
        return;
    }

    if (target.serverAddress.IsInvalid())
    {
        record->second.status = TaskStatus::FAILED_NETWORK;
        NS_LOG_ERROR("Remote target " << target.nodeId << " has no server address");
        return;
    }
    if (m_offloadProtocol != "TCP" && m_offloadProtocol != "UDP")
    {
        record->second.status = TaskStatus::FAILED_NETWORK;
        NS_LOG_ERROR("Unsupported OffloadProtocol: " << m_offloadProtocol);
        return;
    }
    if (m_resultAckMode != "ResultData" && m_resultAckMode != "AckOnly")
    {
        record->second.status = TaskStatus::FAILED_NETWORK;
        NS_LOG_ERROR("Unsupported ResultAckMode: " << m_resultAckMode);
        return;
    }

    Ptr<Socket> socket = Socket::CreateSocket(
        GetNode(),
        m_offloadProtocol == "TCP" ? TcpSocketFactory::GetTypeId()
                                    : UdpSocketFactory::GetTypeId());
    if (socket == nullptr)
    {
        record->second.status = TaskStatus::FAILED_NETWORK;
        NS_LOG_ERROR("Could not create " << m_offloadProtocol << " socket for task "
                                          << task.GetTaskId());
        return;
    }

    ClientTransfer transfer;
    transfer.socket = socket;
    transfer.isUdp = m_offloadProtocol == "UDP";
    transfer.rxBuffer = Create<Packet>();
    m_transfers[task.GetTaskId()] = transfer;
    m_socketTaskIds[PeekPointer(socket)] = task.GetTaskId();

    record->second.targetNodeId = target.nodeId;
    record->second.status = TaskStatus::OFFLOADING_INPUT;
    m_traceTaskOffloaded(task.GetTaskId(), task.GetSourceNodeId(), target.nodeId);

    socket->SetRecvCallback(MakeCallback(&TaskClientApplication::HandleRead, this));
    socket->SetSendCallback(MakeCallback(&TaskClientApplication::HandleSendReady, this));
    if (!transfer.isUdp)
    {
        socket->SetConnectCallback(
            MakeCallback(&TaskClientApplication::HandleConnectSuccess, this),
            MakeCallback(&TaskClientApplication::HandleConnectFailure, this));
    }
    else if (socket->Bind() < 0)
    {
        FailTransfer(task.GetTaskId());
        return;
    }

    const int result = socket->Connect(target.serverAddress);
    if (result < 0)
    {
        FailTransfer(task.GetTaskId());
        return;
    }
    if (transfer.isUdp)
    {
        m_transfers[task.GetTaskId()].connected = true;
        TrySendNextChunk(task.GetTaskId());
    }
}

void
TaskClientApplication::OnLocalExecutionComplete(const TaskDescriptor& task,
                                                Time waitTime,
                                                Time computeTime)
{
    FinalizeTask(task.GetTaskId(), waitTime, computeTime);
}

void
TaskClientApplication::HandleConnectSuccess(Ptr<Socket> socket)
{
    const uint64_t taskId = GetTaskIdForSocket(socket);
    auto transfer = m_transfers.find(taskId);
    if (transfer == m_transfers.end())
    {
        return;
    }
    transfer->second.connected = true;
    TrySendNextChunk(taskId);
}

void
TaskClientApplication::HandleConnectFailure(Ptr<Socket> socket)
{
    const uint64_t taskId = GetTaskIdForSocket(socket);
    FailTransfer(taskId);
}

void
TaskClientApplication::HandleSendReady(Ptr<Socket> socket, uint32_t txSpace)
{
    (void)txSpace;
    const uint64_t taskId = GetTaskIdForSocket(socket);
    if (m_transfers.find(taskId) != m_transfers.end())
    {
        TrySendNextChunk(taskId);
    }
}

void
TaskClientApplication::TrySendNextChunk(uint64_t taskId)
{
    auto transferIt = m_transfers.find(taskId);
    auto recordIt = m_records.find(taskId);
    if (transferIt == m_transfers.end() || recordIt == m_records.end() ||
        !transferIt->second.connected)
    {
        return;
    }

    ClientTransfer& transfer = transferIt->second;
    const TaskDescriptor& task = recordIt->second.task;
    while (true)
    {
        if (!transfer.hasPendingFrame)
        {
            if (task.GetInputBytes() == 0 && transfer.zeroInputFrameSent)
            {
                return;
            }
            if (task.GetInputBytes() > 0 && transfer.inputBytesSent >= task.GetInputBytes())
            {
                return;
            }

            const uint64_t remaining = task.GetInputBytes() - transfer.inputBytesSent;
            const uint32_t chunkBytes = static_cast<uint32_t>(
                std::min<uint64_t>(remaining, MAX_CHUNK_BYTES));
            TaskPacketHeader header;
            header.SetPacketType(TaskPacketHeader::TASK_REQUEST);
            header.SetSourceNodeId(task.GetSourceNodeId());
            header.SetTargetNodeId(recordIt->second.targetNodeId);
            header.SetChunkBytes(chunkBytes);
            header.SetTaskId(task.GetTaskId());
            header.SetInputBytes(task.GetInputBytes());
            header.SetOutputBytes(task.GetOutputBytes());
            header.SetRequiredCycles(task.GetRequiredCycles());

            transfer.pendingFrame = Create<Packet>(chunkBytes);
            transfer.pendingFrame->AddHeader(header);
            transfer.pendingChunkBytes = chunkBytes;
            transfer.hasPendingFrame = true;
        }

        const uint32_t frameSize = transfer.pendingFrame->GetSize();
        const int sent = transfer.socket->Send(transfer.pendingFrame);
        if (sent < 0)
        {
            return; // The socket send callback will retry when buffer space is available.
        }
        if (sent == 0)
        {
            return;
        }

        if (transfer.isUdp && static_cast<uint32_t>(sent) != frameSize)
        {
            NS_LOG_WARN("UDP socket accepted a partial task datagram for task " << taskId);
            FailTransfer(taskId);
            return;
        }
        if (static_cast<uint32_t>(sent) < frameSize)
        {
            transfer.pendingFrame = transfer.pendingFrame->CreateFragment(
                static_cast<uint32_t>(sent), frameSize - static_cast<uint32_t>(sent));
            return;
        }

        transfer.pendingFrame = nullptr;
        transfer.hasPendingFrame = false;
        if (task.GetInputBytes() == 0)
        {
            transfer.zeroInputFrameSent = true;
        }
        else
        {
            transfer.inputBytesSent += transfer.pendingChunkBytes;
        }
        transfer.pendingChunkBytes = 0;
    }
}

void
TaskClientApplication::HandleRead(Ptr<Socket> socket)
{
    const uint64_t taskId = GetTaskIdForSocket(socket);
    auto transferIt = m_transfers.find(taskId);
    if (transferIt == m_transfers.end())
    {
        return;
    }

    if (transferIt->second.isUdp)
    {
        Address from;
        Ptr<Packet> packet;
        while ((packet = socket->RecvFrom(from)) != nullptr)
        {
            if (packet->GetSize() < TASK_HEADER_BYTES)
            {
                FailTransfer(taskId);
                return;
            }
            TaskPacketHeader header;
            if (packet->RemoveHeader(header) != TASK_HEADER_BYTES ||
                packet->GetSize() != header.GetChunkBytes() ||
                header.GetChunkBytes() > MAX_CHUNK_BYTES)
            {
                FailTransfer(taskId);
                return;
            }
            if (ProcessReplyFrame(taskId, header, packet->GetSize()))
            {
                return;
            }
        }
        return;
    }

    Ptr<Packet> packet;
    while ((packet = socket->Recv()) != nullptr)
    {
        if (packet->GetSize() == 0)
        {
            FailTransfer(taskId);
            return;
        }
        transferIt = m_transfers.find(taskId);
        if (transferIt == m_transfers.end())
        {
            return;
        }
        transferIt->second.rxBuffer->AddAtEnd(packet);
    }

    while (true)
    {
        transferIt = m_transfers.find(taskId);
        if (transferIt == m_transfers.end())
        {
            return;
        }
        Ptr<Packet> buffer = transferIt->second.rxBuffer;
        if (buffer->GetSize() < TASK_HEADER_BYTES)
        {
            return;
        }

        Ptr<Packet> headerPacket = buffer->Copy();
        TaskPacketHeader peekHeader;
        if (headerPacket->RemoveHeader(peekHeader) != TASK_HEADER_BYTES ||
            peekHeader.GetChunkBytes() > MAX_CHUNK_BYTES)
        {
            FailTransfer(taskId);
            return;
        }
        const uint32_t frameSize = TASK_HEADER_BYTES + peekHeader.GetChunkBytes();
        if (buffer->GetSize() < frameSize)
        {
            return;
        }

        TaskPacketHeader header;
        if (buffer->RemoveHeader(header) != TASK_HEADER_BYTES)
        {
            FailTransfer(taskId);
            return;
        }
        buffer->RemoveAtStart(header.GetChunkBytes());
        if (ProcessReplyFrame(taskId, header, header.GetChunkBytes()))
        {
            return;
        }
    }
}

bool
TaskClientApplication::ProcessReplyFrame(uint64_t taskId,
                                         const TaskPacketHeader& header,
                                         uint32_t payloadBytes)
{
    auto recordIt = m_records.find(taskId);
    auto transferIt = m_transfers.find(taskId);
    if (recordIt == m_records.end() || transferIt == m_transfers.end() ||
        header.GetTaskId() != taskId ||
        header.GetSourceNodeId() != GetNode()->GetId() ||
        header.GetTargetNodeId() != recordIt->second.targetNodeId)
    {
        FailTransfer(taskId);
        return true;
    }

    if (header.GetPacketType() == TaskPacketHeader::TASK_ACK_ONLY)
    {
        FinalizeTask(taskId, header.GetQueueDuration(), header.GetComputeDuration());
        return true;
    }
    if (header.GetPacketType() != TaskPacketHeader::TASK_RESULT ||
        payloadBytes != header.GetChunkBytes() || payloadBytes > MAX_CHUNK_BYTES)
    {
        FailTransfer(taskId);
        return true;
    }

    recordIt->second.status = TaskStatus::RETURNING_RESULT;
    transferIt->second.outputBytesReceived += payloadBytes;
    const uint64_t expectedBytes = recordIt->second.task.GetOutputBytes();
    if (transferIt->second.outputBytesReceived > expectedBytes)
    {
        FailTransfer(taskId);
        return true;
    }
    if (expectedBytes == 0 || transferIt->second.outputBytesReceived == expectedBytes)
    {
        FinalizeTask(taskId, header.GetQueueDuration(), header.GetComputeDuration());
        return true;
    }
    return false;
}

uint64_t
TaskClientApplication::GetTaskIdForSocket(Ptr<Socket> socket) const
{
    auto it = m_socketTaskIds.find(PeekPointer(socket));
    return it == m_socketTaskIds.end() ? std::numeric_limits<uint64_t>::max() : it->second;
}

void
TaskClientApplication::FailTransfer(uint64_t taskId)
{
    auto record = m_records.find(taskId);
    if (record != m_records.end() && record->second.status != TaskStatus::COMPLETED)
    {
        record->second.status = TaskStatus::FAILED_NETWORK;
    }
    NS_LOG_WARN("Network transfer failed for task " << taskId);
    CloseTransfer(taskId);
}

void
TaskClientApplication::CloseTransfer(uint64_t taskId)
{
    auto transfer = m_transfers.find(taskId);
    if (transfer == m_transfers.end())
    {
        return;
    }
    Ptr<Socket> socket = transfer->second.socket;
    if (socket != nullptr)
    {
        socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        socket->SetSendCallback(MakeNullCallback<void, Ptr<Socket>, uint32_t>());
        socket->SetConnectCallback(MakeNullCallback<void, Ptr<Socket>>(),
                                   MakeNullCallback<void, Ptr<Socket>>());
        socket->Close();
        m_socketTaskIds.erase(PeekPointer(socket));
    }
    m_transfers.erase(transfer);
}

void
TaskClientApplication::FinalizeTask(uint64_t taskId,
                                    Time queueDuration,
                                    Time computeDuration)
{
    auto recordIt = m_records.find(taskId);
    if (recordIt == m_records.end() || recordIt->second.status == TaskStatus::COMPLETED)
    {
        return;
    }

    TaskClientRecord& record = recordIt->second;
    record.timeDone = Simulator::Now();
    record.remoteQueueDuration = std::max(queueDuration, Seconds(0));
    record.remoteComputeDuration = std::max(computeDuration, Seconds(0));

    if (GetNode() != nullptr && record.targetNodeId == GetNode()->GetId())
    {
        record.timeRxComplete = record.timeCreated;
        record.timeExecStart = record.timeCreated + record.remoteQueueDuration;
        record.timeExecEnd = record.timeExecStart + record.remoteComputeDuration;
    }
    else
    {
        // The 64-byte reply carries queue and CPU durations, but no server timestamps.
        // Infer the server milestones by subtracting the estimated result-path duration.
        double rateMbps = DEFAULT_DATA_RATE_MBPS;
        double delayMs = DEFAULT_DELAY_MS;
        for (const auto& candidate : m_candidateStates)
        {
            if (!candidate.isLocal && candidate.nodeId == record.targetNodeId)
            {
                rateMbps = candidate.estimatedDataRateMbps;
                delayMs = candidate.estimatedOneWayDelayMs;
                break;
            }
        }
        rateMbps = rateMbps > 0.0 ? rateMbps : DEFAULT_DATA_RATE_MBPS;
        const uint64_t outputBytes = m_resultAckMode == "AckOnly"
                                         ? 0
                                         : record.task.GetOutputBytes();
        const uint64_t chunks = std::max<uint64_t>(
            1,
            (outputBytes + MAX_CHUNK_BYTES - 1) / MAX_CHUNK_BYTES);
        const uint64_t wireBytes = outputBytes + chunks * TASK_HEADER_BYTES;
        const Time returnPath = MilliSeconds(std::max(0.0, delayMs)) +
                                Seconds(static_cast<double>(wireBytes) * 8.0 /
                                        (rateMbps * 1e6));
        record.timeExecEnd = record.timeDone - returnPath;
        if (record.timeExecEnd < record.timeCreated)
        {
            record.timeExecEnd = record.timeCreated;
        }
        record.timeExecStart = record.timeExecEnd - record.remoteComputeDuration;
        if (record.timeExecStart < record.timeCreated)
        {
            record.timeExecStart = record.timeCreated;
        }
        record.timeRxComplete = record.timeExecStart - record.remoteQueueDuration;
        if (record.timeRxComplete < record.timeCreated)
        {
            record.timeRxComplete = record.timeCreated;
        }
    }

    record.status = TaskStatus::COMPLETED;
    m_traceTaskCompleted(taskId, record.timeDone - record.timeCreated);
    m_traceTaskRecordCompleted(record);
    if (record.task.HasDeadline() && record.timeDone > record.task.GetDeadline())
    {
        m_traceTaskDeadlineMiss(taskId, record.timeDone - record.task.GetDeadline());
    }
    CloseTransfer(taskId);
}

} // namespace ns3

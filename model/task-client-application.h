/**
 * @file task-client-application.h
 * @brief TaskClientApplication definition for M06 futurenet-edge module.
 * @author FutureNet Module Team
 *
 * Implements M06-FR-05, M06-FR-06, M06-FR-09, M06-FR-11:
 * Client application for task generation, local execution vs. remote offload,
 * 5-milestone timing decomposition (CG-08), and stream assignment (CG-09).
 */

#ifndef FUTURENET_TASK_CLIENT_APPLICATION_H
#define FUTURENET_TASK_CLIENT_APPLICATION_H

#include "compute-node-model.h"
#include "offload-policy.h"
#include "task-descriptor.h"
#include "task-packet-header.h"

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"

#include <map>
#include <vector>

namespace ns3
{

/**
 * @ingroup futurenet-edge
 * @brief Internal record tracking the 5 milestone timestamps of an active task.
 */
struct TaskClientRecord
{
    TaskDescriptor task;                  //!< Workload metadata
    Time timeCreated{Seconds(0)};         //!< t_create
    Time timeRxComplete{Seconds(0)};      //!< t_rx_complete
    Time timeExecStart{Seconds(0)};       //!< t_exec_start
    Time timeExecEnd{Seconds(0)};         //!< t_exec_end
    Time timeDone{Seconds(0)};            //!< t_done
    Time remoteQueueDuration{Seconds(0)}; //!< Embedded in server reply header
    Time remoteComputeDuration{Seconds(0)};//!< Embedded in server reply header
    uint32_t targetNodeId{0};             //!< Selected execution target node ID
    TaskStatus status{TaskStatus::CREATED};//!< Current lifecycle state
};

/**
 * @ingroup futurenet-edge
 * @brief Client application that generates computational tasks and manages offloading.
 */
class TaskClientApplication : public Application
{
  public:
    /**
     * @brief Register this type with the ns-3 TypeId system.
     * @return TypeId instance.
     */
    static TypeId GetTypeId();

    TaskClientApplication();
    ~TaskClientApplication() override;

    /**
     * @brief Registers an edge/cloud candidate compute node for offloading consideration.
     * @param targetNode Node instance hosting the EdgeComputeApplication.
     * @param targetAddress Network Address (IP and port) of server application.
     * @param estimatedDataRateMbps Estimated transmission throughput in Mbit/s.
     * @param estimatedDelayMs Estimated one-way propagation latency in ms.
     */
    void AddComputeTarget(Ptr<Node> targetNode,
                          Address targetAddress,
                          double estimatedDataRateMbps = 100.0,
                          double estimatedDelayMs = 5.0);

    /**
     * @brief Configures the offloading decision policy (M06-FR-08).
     * @param policy Pointer to OffloadPolicy implementation.
     */
    void SetOffloadPolicy(Ptr<OffloadPolicy> policy);

    /**
     * @brief Immediately generates and dispatches a single task.
     * @return Unique taskId assigned to the generated task.
     */
    uint64_t GenerateTask();

    /**
     * @brief Queries the current lifecycle status of a task by ID.
     * @param taskId Unique task ID.
     * @return Current TaskStatus.
     */
    TaskStatus GetTaskStatus(uint64_t taskId) const;

    /**
     * @brief Queries milestone record for a completed or active task.
     * @param taskId Unique task ID.
     * @param record Output record populated if found.
     * @return True if record found, false otherwise.
     */
    bool GetTaskRecord(uint64_t taskId, TaskClientRecord& record) const;


    /**
     * @brief Assigns a fixed random variable stream for reproducibility (CG-09, CG-10).
     * @param stream Stream index.
     * @return Number of streams assigned.
     */
    int64_t AssignStreams(int64_t stream);

  protected:
    void StartApplication() override;
    void StopApplication() override;
    void DoDispose() override;

  private:
    struct ComputeTarget
    {
        Ptr<Node> node;
        ComputeNodeState state;
    };

    struct ClientTransfer
    {
        Ptr<Socket> socket;
        bool isUdp{false};
        bool connected{false};
        bool zeroInputFrameSent{false};
        bool hasPendingFrame{false};
        uint32_t pendingChunkBytes{0};
        uint64_t inputBytesSent{0};
        uint64_t outputBytesReceived{0};
        Ptr<Packet> pendingFrame;
        Ptr<Packet> rxBuffer;
    };

    void ScheduleNextTask();
    void GenerateAndScheduleNextTask();
    void RefreshCandidateStates();
    void ExecuteLocally(const TaskDescriptor& task);
    void OffloadRemotely(const TaskDescriptor& task, const ComputeNodeState& target);
    void OnLocalExecutionComplete(const TaskDescriptor& task, Time waitTime, Time computeTime);
    void HandleConnectSuccess(Ptr<Socket> socket);
    void HandleConnectFailure(Ptr<Socket> socket);
    void HandleSendReady(Ptr<Socket> socket, uint32_t txSpace);
    void HandleRead(Ptr<Socket> socket);
    void TrySendNextChunk(uint64_t taskId);
    bool ProcessReplyFrame(uint64_t taskId, const TaskPacketHeader& header, uint32_t payloadBytes);
    uint64_t GetTaskIdForSocket(Ptr<Socket> socket) const;
    void FailTransfer(uint64_t taskId);
    void CloseTransfer(uint64_t taskId);
    void FinalizeTask(uint64_t taskId, Time queueDuration, Time computeDuration);

    Ptr<OffloadPolicy> m_policy;                    //!< Active selection policy
    Ptr<ComputeNodeModel> m_localCompute;           //!< Local compute engine (if present)
    std::vector<ComputeNodeState> m_candidateStates;//!< Candidate targets snapshot
    std::vector<ComputeTarget> m_computeTargets;    //!< Registered remote nodes and static link estimates
    std::map<uint64_t, TaskClientRecord> m_records; //!< Milestone tracking map
    std::map<uint64_t, ClientTransfer> m_transfers; //!< Per-task transport and framing state
    std::map<const Socket*, uint64_t> m_socketTaskIds; //!< Reverse lookup for socket callbacks

    // Configurable Attributes (M06 SRS Section 5)
    uint64_t m_inputBytes{1000000};                //!< TaskInputBytes (default: 1 MB)
    uint64_t m_outputBytes{100000};                //!< TaskOutputBytes (default: 100 kB)
    double m_outputRatio{-1.0};                    //!< TaskOutputRatio (default: -1.0 disabled)
    double m_requiredCycles{1e9};                  //!< RequiredCycles (default: 1e9)
    Time m_taskArrivalInterval{Seconds(1.0)};      //!< TaskArrivalInterval (default: 1.0 s)
    Time m_taskDeadline{Seconds(0)};               //!< TaskDeadline (default: 0 none)
    std::string m_resultAckMode{"ResultData"};     //!< ResultAckMode ("ResultData" or "AckOnly")
    std::string m_offloadProtocol{"TCP"};          //!< OffloadProtocol ("TCP" or "UDP")
    Time m_policyInterval{Seconds(0)};             //!< PolicyEvaluationInterval (0 = per task)

    uint64_t m_nextTaskId{1};                      //!< Incremental task ID counter
    bool m_applicationActive{false};                //!< Guards periodic policy refresh scheduling
    EventId m_nextTaskEvent;                       //!< Periodic generation event
    EventId m_policyRefreshEvent;                  //!< Periodic policy cache refresh event
    Ptr<RandomVariableStream> m_arrivalRng;        //!< Random stream for inter-arrival jitter

    // Trace sources (M06-FR-09, M06-FR-11)
    TracedCallback<uint64_t, uint32_t> m_traceTaskCreated;
    TracedCallback<uint64_t, uint32_t, uint32_t> m_traceTaskOffloaded;
    TracedCallback<uint64_t, Time> m_traceTaskCompleted;
    TracedCallback<uint64_t, Time> m_traceTaskDeadlineMiss;
    TracedCallback<const TaskClientRecord&> m_traceTaskRecordCompleted; //!< Full milestone record
};

} // namespace ns3

#endif // FUTURENET_TASK_CLIENT_APPLICATION_H

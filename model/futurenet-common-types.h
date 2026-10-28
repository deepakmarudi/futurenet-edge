/**
 * @file futurenet-common-types.h
 * @brief Common types and interface contract for FutureNet ns-3.46.1 modules.
 * @author FutureNet Module Team
 *
 * This file defines the standard cross-module data structures specified in
 * Section 4 of the Common Software Requirements and Design Guidelines for
 * Future Network Extensions for ns-3.46.1.
 */

#ifndef FUTURENET_COMMON_TYPES_H
#define FUTURENET_COMMON_TYPES_H

#include "ns3/nstime.h"
#include "ns3/vector.h"

#include <cmath>
#include <cstdint>

namespace ns3
{

/**
 * @ingroup futurenet-edge
 * @brief Common snapshot of node and network metrics across FutureNet modules (CG Section 4.1).
 */
struct NetworkState
{
    Time timestamp{Seconds(0)};         //!< Simulation time when the measurement was taken
    uint32_t nodeId{0};                 //!< Unique node identifier
    double oneWayDelayMs{NAN};          //!< Measured or estimated one-way delay in ms (NaN if unavailable)
    double throughputMbps{0.0};         //!< Measured or estimated throughput in Mbit/s
    double packetLossRatio{0.0};        //!< Packet loss ratio in dimensionless range [0, 1]
    double availableBandwidthMbps{0.0}; //!< Estimated available bandwidth in Mbit/s (>= 0)
    uint32_t queuePackets{0};           //!< Current number of packets or tasks in queue
    double nodeLoad{0.0};               //!< Normalized node utilization in range [0, 1]
    double energyJ{0.0};                //!< Remaining or consumed energy in Joules (valid if energyValid==true)
    bool energyValid{false};            //!< Flag indicating if energyJ contains valid telemetry
};

/**
 * @ingroup futurenet-edge
 * @brief Common link state representation across FutureNet modules (CG Section 4.2).
 */
struct LinkState
{
    uint32_t sourceNodeId{0};       //!< Source node identifier
    uint32_t destinationNodeId{0};  //!< Destination node identifier
    bool available{false};          //!< Link operational availability flag
    double dataRateMbps{0.0};       //!< Link transmission capacity in Mbit/s
    double propagationDelayMs{0.0}; //!< One-way physical propagation delay in ms
    double lossRatio{0.0};          //!< Link packet loss ratio in range [0, 1]
};

/**
 * @ingroup futurenet-edge
 * @brief Common mobility state representation across FutureNet modules (CG Section 4.3).
 */
struct MobilityState
{
    uint32_t nodeId{0};                //!< Node identifier
    Vector positionM{0.0, 0.0, 0.0};   //!< 3D spatial coordinates in meters
    Vector velocityMps{0.0, 0.0, 0.0}; //!< 3D velocity vector in meters per second
    Time timestamp{Seconds(0)};        //!< Simulation timestamp of this mobility sample
};

/**
 * @ingroup futurenet-edge
 * @brief Common traffic classification metadata across FutureNet modules (CG Section 4.4).
 */
struct FutureNetTrafficClass
{
    uint8_t priority{0};       //!< Priority level: 0 = highest, default range [0, 7]
    double importance{0.0};    //!< Normalized task or packet importance in range [0, 1]
    Time deadline{Seconds(0)}; //!< Completion deadline (relative or absolute)
    uint32_t flowId{0};        //!< Associated flow or task identifier
};

} // namespace ns3

#endif // FUTURENET_COMMON_TYPES_H

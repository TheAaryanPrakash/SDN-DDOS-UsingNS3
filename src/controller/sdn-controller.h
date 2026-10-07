#ifndef SDN_CONTROLLER_H
#define SDN_CONTROLLER_H

#include <ns3/ofswitch13-module.h>

#include <fstream>
#include <map>

namespace ns3
{

/**
 * Our project's SDN controller.
 *
 * Phase 5: a plain L2 learning switch controller -- reacts to packet-in
 * messages, learns which MAC sits behind which switch port, and installs
 * flow-mod rules so learned traffic is forwarded directly instead of
 * hitting the controller every time.
 *
 * Phase 8: traffic monitoring. Packet-in stops arriving once a flow is
 * learned and cached in the switch's flow table (that is the whole point
 * of an SDN flow table -- ongoing data-plane traffic bypasses the control
 * plane). So packet-in counts alone cannot see an ongoing flood. Instead,
 * the controller periodically polls each switch for OpenFlow PORT
 * STATISTICS (the "stats-port" dpctl command / OFPMP_PORT_STATS multipart
 * message) -- real per-port byte/packet counters maintained by the switch
 * datapath itself. Because every host in our topology (client, attacker,
 * server) has its own dedicated switch port, a per-port rate is, in this
 * topology, a per-source rate: exactly what Phase 9's threshold detection
 * needs.
 *
 * Phase 9: threshold-based detection. No ML, no statistics beyond a
 * straight comparison -- for each port, every time a new rate is computed
 * (Phase 8), compare it against a fixed, configurable PacketRateThreshold
 * (packets/sec). A port sending above the threshold is marked suspicious;
 * a port that drops back below it is unmarked. Every rate sample is logged
 * either way, and every transition (clean->suspicious or the reverse) is
 * logged separately with the simulation time, so detection latency can be
 * measured later from real data instead of guessed.
 *
 * Phase 10: mitigation. When a port is marked suspicious, the controller
 * installs a high-priority DROP flow-mod matching that port (an OpenFlow
 * flow-mod with no instructions silently drops anything it matches) --
 * Option 1 from the project plan, simpler and more reliable than
 * rate-limiting. The drop rule is removed again the moment the port's rate
 * falls back under the threshold, so a source stops being blocked as soon
 * as it stops flooding.
 *
 * Mitigation can be switched off per simulation (EnableMitigation
 * attribute, default true) without touching detection: this is what lets
 * the SAME controller class drive both "attack without mitigation"
 * (Experiment 2) and "attack with detection and mitigation" (Experiment 3)
 * from Phase 11, instead of duplicating the whole class.
 */
class SdnController : public OFSwitch13Controller
{
  public:
    SdnController();
    ~SdnController() override;

    static TypeId GetTypeId();
    void DoDispose() override;

    ofl_err HandlePacketIn(struct ofl_msg_packet_in* msg,
                            Ptr<const RemoteSwitch> swtch,
                            uint32_t xid) override;

    ofl_err HandleFlowRemoved(struct ofl_msg_flow_removed* msg,
                               Ptr<const RemoteSwitch> swtch,
                               uint32_t xid) override;

    ofl_err HandleMultipartReply(struct ofl_msg_multipart_reply_header* msg,
                                  Ptr<const RemoteSwitch> swtch,
                                  uint32_t xid) override;

  protected:
    void HandshakeSuccessful(Ptr<const RemoteSwitch> swtch) override;

  private:
    /** MAC address -> switch port it was last seen on. */
    typedef std::map<Mac48Address, uint32_t> L2Table_t;

    /** Switch datapath ID -> that switch's L2 learning table. */
    typedef std::map<uint64_t, L2Table_t> DatapathMap_t;

    DatapathMap_t m_l2Table; //!< Learning table, one per connected switch.

    /** Raw counters last read from a switch port, used to compute rates. */
    struct PortCounters
    {
        uint64_t rxPackets = 0;
        uint64_t rxBytes = 0;
    };

    /** Switch port number -> last known counters for that port. */
    typedef std::map<uint32_t, PortCounters> PortStatsMap_t;

    /** Switch datapath ID -> per-port counters for that switch. */
    typedef std::map<uint64_t, PortStatsMap_t> DatapathPortStatsMap_t;

    DatapathPortStatsMap_t m_lastPortStats; //!< Previous poll's counters, for rate calculation.

    Time m_monitorInterval;   //!< How often to poll port statistics.
    std::ofstream m_monitorFile; //!< CSV output: time, port, MAC, rates.
    bool m_monitorFileOpen = false;

    double m_pktRateThreshold; //!< Detection threshold, in packets/sec.
    std::ofstream m_detectionFile; //!< CSV output: detection/clearance events.
    bool m_detectionFileOpen = false;

    /** Whether a port is currently considered to be flooding. */
    typedef std::map<uint32_t, bool> PortSuspicionMap_t;

    /** Switch datapath ID -> per-port suspicion state for that switch. */
    typedef std::map<uint64_t, PortSuspicionMap_t> DatapathSuspicionMap_t;

    DatapathSuspicionMap_t m_suspicious; //!< Current suspicion state, one map per switch.

    bool m_mitigationEnabled; //!< Whether to actually install/remove DROP rules.

    /**
     * Send an OpenFlow port-stats request to the given switch, then
     * reschedule itself to run again after m_monitorInterval. Started once
     * per switch from HandshakeSuccessful() and keeps running for the rest
     * of the simulation.
     * \param swDpId The switch's datapath ID.
     */
    void RequestPortStats(uint64_t swDpId);

    /**
     * Process one OFPMP_PORT_STATS multipart reply: for each port, compute
     * packets/sec and bytes/sec since the last poll, log it to the CSV
     * output file, update m_lastPortStats, and run it through CheckThreshold.
     * \param swDpId The switch's datapath ID.
     * \param reply The parsed port-stats multipart reply.
     */
    void ProcessPortStatsReply(uint64_t swDpId, struct ofl_msg_multipart_reply_port* reply);

    /**
     * Compare one port's freshly computed rate against m_pktRateThreshold
     * and update/log its suspicion state if it changed. This is the entire
     * detection rule: no machine learning, no statistics beyond this single
     * comparison, matching the project's rule/threshold-based requirement.
     * \param swDpId The switch's datapath ID.
     * \param portNo The port being evaluated.
     * \param sourceMac Human-readable MAC label for this port, for logging.
     * \param packetsPerSec The just-measured rate for this port.
     */
    void CheckThreshold(uint64_t swDpId,
                        uint32_t portNo,
                        const std::string& sourceMac,
                        double packetsPerSec);

    /**
     * Install a high-priority DROP flow-mod for one switch port: a flow-mod
     * matching in_port=portNo with no instructions at all, which OpenFlow
     * 1.3 defines as "drop". Priority 200 is above both our learned
     * forwarding flows (100) and the table-miss entry (0), so it is checked
     * first and the source's traffic never reaches the rest of the pipeline.
     * \param swDpId The switch's datapath ID.
     * \param portNo The port to block.
     */
    void InstallMitigation(uint64_t swDpId, uint32_t portNo);

    /**
     * Remove the DROP flow-mod previously installed by InstallMitigation
     * for this port, restoring normal forwarding for that source.
     * \param swDpId The switch's datapath ID.
     * \param portNo The port to unblock.
     */
    void RemoveMitigation(uint64_t swDpId, uint32_t portNo);
};

} // namespace ns3

#endif /* SDN_CONTROLLER_H */

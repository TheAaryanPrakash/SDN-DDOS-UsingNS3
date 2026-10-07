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
 * Phase 9-10 (threshold detection + mitigation flow-mod rules) build on
 * top of this monitoring data in the same class.
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
     * output file, and update m_lastPortStats.
     * \param swDpId The switch's datapath ID.
     * \param reply The parsed port-stats multipart reply.
     */
    void ProcessPortStatsReply(uint64_t swDpId, struct ofl_msg_multipart_reply_port* reply);
};

} // namespace ns3

#endif /* SDN_CONTROLLER_H */

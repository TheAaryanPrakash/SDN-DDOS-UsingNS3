#include "sdn-controller.h"

NS_LOG_COMPONENT_DEFINE("SdnController");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(SdnController);

SdnController::SdnController()
    : m_monitorInterval(Seconds(1.0))
{
    NS_LOG_FUNCTION(this);
}

SdnController::~SdnController()
{
    NS_LOG_FUNCTION(this);
}

TypeId
SdnController::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::SdnController")
            .SetParent<OFSwitch13Controller>()
            .SetGroupName("SdnDdosProject")
            .AddConstructor<SdnController>()
            .AddAttribute("MonitorInterval",
                          "How often to poll switches for port statistics.",
                          TimeValue(Seconds(1.0)),
                          MakeTimeAccessor(&SdnController::m_monitorInterval),
                          MakeTimeChecker());
    return tid;
}

void
SdnController::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_l2Table.clear();
    if (m_monitorFileOpen)
    {
        m_monitorFile.close();
        m_monitorFileOpen = false;
    }
    OFSwitch13Controller::DoDispose();
}

void
SdnController::HandshakeSuccessful(Ptr<const RemoteSwitch> swtch)
{
    NS_LOG_FUNCTION(this << swtch);

    uint64_t swDpId = swtch->GetDpId();

    // Table-miss entry: anything that doesn't match a more specific flow
    // gets sent up to this controller (first 128 bytes is enough to read
    // the Ethernet/IP/TCP headers we need for decisions).
    DpctlExecute(swDpId, "flow-mod cmd=add,table=0,prio=0 apply:output=ctrl:128");
    DpctlExecute(swDpId, "set-config miss=128");

    // Start this switch with an empty learning table.
    m_l2Table[swDpId] = L2Table_t();
    m_lastPortStats[swDpId] = PortStatsMap_t();

    if (!m_monitorFileOpen)
    {
        m_monitorFile.open("monitor-stats.csv");
        m_monitorFile << "TimeSec,DatapathId,PortNo,SourceMac,RxPackets,RxBytes,"
                      << "PacketsPerSec,BytesPerSec\n";
        m_monitorFileOpen = true;
    }

    // Kick off periodic port-statistics polling for this switch. It keeps
    // rescheduling itself (see RequestPortStats) for the rest of the sim.
    Simulator::Schedule(m_monitorInterval, &SdnController::RequestPortStats, this, swDpId);
}

void
SdnController::RequestPortStats(uint64_t swDpId)
{
    NS_LOG_FUNCTION(this << swDpId);

    // Ask the switch datapath for OFPMP_PORT_STATS: real packet/byte
    // counters it maintains per port, independent of whatever is or isn't
    // in the flow table. The reply arrives later as a HandleMultipartReply
    // call. "ALL" requests stats for every port on this switch in one go.
    DpctlExecute(swDpId, "stats-port");

    Simulator::Schedule(m_monitorInterval, &SdnController::RequestPortStats, this, swDpId);
}

ofl_err
SdnController::HandleMultipartReply(struct ofl_msg_multipart_reply_header* msg,
                                     Ptr<const RemoteSwitch> swtch,
                                     uint32_t xid)
{
    NS_LOG_FUNCTION(this << swtch << xid);

    if (msg->type == OFPMP_PORT_STATS)
    {
        ProcessPortStatsReply(swtch->GetDpId(), (struct ofl_msg_multipart_reply_port*)msg);
    }

    ofl_msg_free((struct ofl_msg_header*)msg, nullptr);
    return 0;
}

void
SdnController::ProcessPortStatsReply(uint64_t swDpId, struct ofl_msg_multipart_reply_port* reply)
{
    NS_LOG_FUNCTION(this << swDpId);

    PortStatsMap_t& lastStats = m_lastPortStats[swDpId];
    L2Table_t& l2Table = m_l2Table[swDpId];
    double intervalSec = m_monitorInterval.GetSeconds();

    for (size_t i = 0; i < reply->stats_num; i++)
    {
        struct ofl_port_stats* portStats = reply->stats[i];
        uint32_t portNo = portStats->port_no;

        PortCounters previous = lastStats[portNo]; // default-constructed (0s) if new port
        uint64_t rxPackets = portStats->rx_packets;
        uint64_t rxBytes = portStats->rx_bytes;

        double packetsPerSec = (rxPackets - previous.rxPackets) / intervalSec;
        double bytesPerSec = (rxBytes - previous.rxBytes) / intervalSec;

        // Identify which learned MAC (if any) sits on this port, purely for
        // a human/CSV-readable label -- the detection logic in later phases
        // keys off the port number itself, which is authoritative.
        std::string sourceMac = "unknown";
        for (const auto& entry : l2Table)
        {
            if (entry.second == portNo)
            {
                std::ostringstream oss;
                oss << entry.first;
                sourceMac = oss.str();
                break;
            }
        }

        m_monitorFile << Simulator::Now().GetSeconds() << "," << swDpId << "," << portNo << ","
                      << sourceMac << "," << rxPackets << "," << rxBytes << "," << packetsPerSec
                      << "," << bytesPerSec << "\n";

        lastStats[portNo] = {rxPackets, rxBytes};
    }
}

ofl_err
SdnController::HandlePacketIn(struct ofl_msg_packet_in* msg,
                               Ptr<const RemoteSwitch> swtch,
                               uint32_t xid)
{
    NS_LOG_FUNCTION(this << swtch << xid);

    uint64_t swDpId = swtch->GetDpId();
    uint32_t outPort = OFPP_FLOOD;

    if (msg->reason != OFPR_NO_MATCH)
    {
        NS_LOG_WARN("Unexpected packet-in reason, ignoring.");
        ofl_msg_free((struct ofl_msg_header*)msg, nullptr);
        return 0;
    }

    uint32_t inPort;
    struct ofl_match_tlv* inPortTlv =
        oxm_match_lookup(OXM_OF_IN_PORT, (struct ofl_match*)msg->match);
    memcpy(&inPort, inPortTlv->value, OXM_LENGTH(OXM_OF_IN_PORT));

    Mac48Address srcMac;
    struct ofl_match_tlv* ethSrcTlv =
        oxm_match_lookup(OXM_OF_ETH_SRC, (struct ofl_match*)msg->match);
    srcMac.CopyFrom(ethSrcTlv->value);

    Mac48Address dstMac;
    struct ofl_match_tlv* ethDstTlv =
        oxm_match_lookup(OXM_OF_ETH_DST, (struct ofl_match*)msg->match);
    dstMac.CopyFrom(ethDstTlv->value);

    L2Table_t& l2Table = m_l2Table[swDpId];

    // Learn where the source MAC is.
    if (l2Table.find(srcMac) == l2Table.end())
    {
        l2Table[srcMac] = inPort;
        NS_LOG_DEBUG("Learned " << srcMac << " on port " << inPort);
    }

    // If we already know the destination's port, send directly there and
    // install a flow for exactly this (src, dst) pair -- NOT a flow keyed
    // on destination alone.
    //
    // Why the pair matters: an earlier version of this controller installed
    // flows matching only "eth_dst = <learned mac>", meant to fast-path all
    // future traffic TO that host. That seemed right, but it has a hidden
    // asymmetry bug -- a host that only ever REPLIES to others (like our
    // server, which never initiates anything) has its own replies match the
    // *requester's* already-installed eth_dst flow and get forwarded without
    // ever reaching the controller. Its own MAC never gets learned, so no
    // flow for traffic TOWARDS it ever gets installed either, and every
    // single packet addressed to it hits the table-miss entry forever. We
    // caught this by noticing PktsIn stayed equal to total Packets in
    // switch-stats for the whole simulation (see documentation/environment.md).
    //
    // Matching on the (src, dst) pair -- the same approach used by the
    // standard Ryu simple_switch_13 tutorial controller -- fixes this: only
    // the A->B direction gets fast-pathed once A->B has been seen. The B->A
    // direction is a different match, so it still hits table-miss on its own
    // first packet, and the controller learns B's MAC exactly as it did A's.
    if (!dstMac.IsBroadcast())
    {
        auto it = l2Table.find(dstMac);
        if (it != l2Table.end())
        {
            outPort = it->second;

            std::ostringstream cmd;
            cmd << "flow-mod cmd=add,table=0,idle=10,flags=0x0001"
                << ",prio=100 eth_src=" << srcMac << ",eth_dst=" << dstMac
                << " apply:output=" << outPort;
            DpctlExecute(swDpId, cmd.str());
        }
    }

    // Echo the packet back out to the switch via a packet-out message.
    struct ofl_msg_packet_out reply;
    reply.header.type = OFPT_PACKET_OUT;
    reply.buffer_id = msg->buffer_id;
    reply.in_port = inPort;
    reply.data_length = 0;
    reply.data = nullptr;
    if (msg->buffer_id == NO_BUFFER)
    {
        reply.data_length = msg->data_length;
        reply.data = msg->data;
    }

    auto action = (struct ofl_action_output*)xmalloc(sizeof(struct ofl_action_output));
    action->header.type = OFPAT_OUTPUT;
    action->port = outPort;
    action->max_len = 0;
    reply.actions_num = 1;
    reply.actions = (struct ofl_action_header**)&action;

    SendToSwitch(swtch, (struct ofl_msg_header*)&reply, xid);
    free(action);

    ofl_msg_free((struct ofl_msg_header*)msg, nullptr);
    return 0;
}

ofl_err
SdnController::HandleFlowRemoved(struct ofl_msg_flow_removed* msg,
                                  Ptr<const RemoteSwitch> swtch,
                                  uint32_t xid)
{
    NS_LOG_FUNCTION(this << swtch << xid);

    // Each flow now matches one specific (src, dst) pair rather than a
    // destination alone (see HandlePacketIn), so one flow expiring from
    // idle timeout just means that one conversation direction paused for a
    // while -- it says nothing about whether either host is still present.
    // We deliberately do NOT unlearn the MAC from m_l2Table here: that table
    // is our "who is behind which port" memory and should persist for the
    // session; only the fast-path flow-mod cache entries expire.
    ofl_msg_free_flow_removed(msg, true, nullptr);
    return 0;
}

} // namespace ns3

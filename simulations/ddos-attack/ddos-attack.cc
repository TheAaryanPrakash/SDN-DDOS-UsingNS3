// Phase 7: same legitimate clients as Phase 6, plus one attacker, and still
// NO detection/mitigation -- this is "Experiment 2: DDoS attack without
// mitigation" from the final comparison (Phase 11). The point of this file
// is only to prove the attack actually hurts the network; nothing reacts to
// it yet.
//
//   Client 1   Client 2   Client 3     Attacker
//        \        |         /             |
//               SDN Switch ----------------
//                    |
//                 Server   (bottleneck access link)
//
// How the attack is simulated:
//   The attacker is an ordinary ns-3 node running an OnOffApplication over
//   UDP, configured to be "on" 100% of the time (OnTime constant, OffTime
//   zero) at a high DataRate with small packets -- i.e. a continuous,
//   high-packet-rate UDP flood aimed at the server's IP:port. UDP is used
//   deliberately: it is connectionless and has no congestion control, so
//   (just like a real flood) the attacker never slows down in response to
//   loss or delay, unlike TCP.
//
//   To make the attack's effect visible without needing an unrealistically
//   huge packet rate, the server's own link to the switch is modeled as a
//   bottleneck access link (far slower than the client/attacker links --
//   think "the victim's internet connection"), using the ServerLinkRate
//   parameter. When the attacker floods faster than this link can drain,
//   the switch's server-facing port queue fills up and starts tail-dropping
//   packets -- including the legitimate clients' packets, which is exactly
//   how a real volumetric DDoS degrades service for everyone, not just the
//   attacker's own traffic.

#include <ns3/applications-module.h>
#include <ns3/core-module.h>
#include <ns3/csma-module.h>
#include <ns3/flow-monitor-module.h>
#include <ns3/internet-module.h>
#include <ns3/network-module.h>
#include <ns3/ofswitch13-module.h>
#include <ns3/traffic-control-module.h>

#include "sdn-controller.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("DdosAttack");

int
main(int argc, char* argv[])
{
    uint32_t numClients = 3;
    double simTime = 20.0;
    std::string perClientRate = "500Kbps";
    uint32_t clientPacketSize = 512;

    std::string attackRate = "20Mbps";
    uint32_t attackPacketSize = 64; // small packets => high packets/sec, typical of floods
    double attackStart = 10.0;
    double attackStop = -1.0; // -1 means "until simTime - 1"

    std::string hostLinkRate = "100Mbps"; // client/attacker access links
    std::string serverLinkRate = "10Mbps"; // victim's (bottleneck) access link

    CommandLine cmd;
    cmd.AddValue("numClients", "Number of legitimate client nodes", numClients);
    cmd.AddValue("simTime", "Simulation duration in seconds", simTime);
    cmd.AddValue("perClientRate", "UDP rate per legitimate client", perClientRate);
    cmd.AddValue("clientPacketSize", "Legitimate client UDP payload size", clientPacketSize);
    cmd.AddValue("attackRate", "Attacker's flood UDP data rate (e.g. 20Mbps)", attackRate);
    cmd.AddValue("attackPacketSize", "Attacker's flood packet size in bytes", attackPacketSize);
    cmd.AddValue("attackStart", "Attack start time in seconds", attackStart);
    cmd.AddValue("attackStop", "Attack stop time in seconds (-1 = until near sim end)", attackStop);
    cmd.AddValue("serverLinkRate", "Server's (bottleneck) access link data rate", serverLinkRate);
    cmd.Parse(argc, argv);

    if (attackStop < 0.0)
    {
        attackStop = simTime - 1.0;
    }

    // --- Nodes ---
    NodeContainer clients;
    clients.Create(numClients);
    Ptr<Node> attacker = CreateObject<Node>();
    Ptr<Node> server = CreateObject<Node>();
    Ptr<Node> switchNode = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    NodeContainer hosts;
    hosts.Add(clients);
    hosts.Add(attacker);
    hosts.Add(server);
    uint32_t serverIndex = numClients + 1;

    // --- Data plane ---
    // Client and attacker links are fast; the server's link is the
    // deliberately slower bottleneck described above. Delay is
    // LAN-realistic (microseconds) and deliberately NOT the bottleneck --
    // see documentation/environment.md for why a larger (millisecond-scale)
    // delay would itself cap max packet rate regardless of DataRate, since
    // CSMA holds the channel busy for a transmission's full propagation
    // delay.
    CsmaHelper hostLink;
    hostLink.SetChannelAttribute("DataRate", StringValue(hostLinkRate));
    hostLink.SetChannelAttribute("Delay", TimeValue(MicroSeconds(2)));

    CsmaHelper serverLink;
    serverLink.SetChannelAttribute("DataRate", StringValue(serverLinkRate));
    serverLink.SetChannelAttribute("Delay", TimeValue(MicroSeconds(2)));

    NetDeviceContainer hostDevices;
    NetDeviceContainer switchPorts;
    for (uint32_t i = 0; i < hosts.GetN(); i++)
    {
        NodeContainer pair(hosts.Get(i), switchNode);
        CsmaHelper& linkHelper = (i == serverIndex) ? serverLink : hostLink;
        NetDeviceContainer link = linkHelper.Install(pair);
        hostDevices.Add(link.Get(0));
        switchPorts.Add(link.Get(1));
    }

    // --- SDN control plane (same controller as Phase 5/6, no changes) ---
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
    // This experiment is "DDoS attack WITHOUT mitigation" (Experiment 2) --
    // detection still runs and logs to detections.csv, but mitigation flow
    // rules are deliberately disabled so the attack's full, unmitigated
    // impact can be measured. See simulations/ddos-mitigated for the
    // mitigated counterpart (Experiment 3), which uses this exact same
    // controller class with EnableMitigation left at its default (true).
    Ptr<SdnController> controllerApp = CreateObject<SdnController>();
    controllerApp->SetAttribute("EnableMitigation", BooleanValue(false));
    of13Helper->InstallController(controllerNode, controllerApp);
    of13Helper->InstallSwitch(switchNode, switchPorts);
    of13Helper->CreateOpenFlowChannels();

    // --- IP stack on hosts only ---
    InternetStackHelper internet;
    internet.Install(hosts);

    // ns-3 would otherwise auto-install an FqCoDel active-queue-management
    // (AQM) discipline on every device during address assignment below --
    // realistic for an ordinary host, but it actively reshapes/paces
    // traffic based on queueing delay, which silently caps the attacker's
    // effective send rate far below its configured DataRate (we caught this
    // empirically: a configured 20Mbps flood was only reaching the switch
    // at a few hundred packets/sec). Installing a plain FIFO queue disc
    // here (simple tail-drop, no AQM) makes a device's on-wire rate track
    // its application's configured DataRate directly, which is what a
    // controlled DDoS experiment needs. See documentation/environment.md.
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize", StringValue("2000p"));
    tch.Install(hostDevices);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer hostIfaces = ipv4.Assign(hostDevices);
    Ipv4Address serverAddr = hostIfaces.GetAddress(serverIndex);

    uint16_t port = 9;
    PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(server);
    sinkApp.Start(Seconds(0.5));
    sinkApp.Stop(Seconds(simTime));

    // --- Legitimate traffic: same as Phase 6 ---
    for (uint32_t i = 0; i < numClients; i++)
    {
        OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(serverAddr, port));
        onoff.SetAttribute("DataRate", StringValue(perClientRate));
        onoff.SetAttribute("PacketSize", UintegerValue(clientPacketSize));
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        double start = 1.0 + 0.1 * i;
        ApplicationContainer clientApp = onoff.Install(clients.Get(i));
        clientApp.Start(Seconds(start));
        clientApp.Stop(Seconds(simTime - 1.0));
    }

    // --- Attack traffic: one UDP flood aimed at the server ---
    OnOffHelper flood("ns3::UdpSocketFactory", InetSocketAddress(serverAddr, port));
    flood.SetAttribute("DataRate", StringValue(attackRate));
    flood.SetAttribute("PacketSize", UintegerValue(attackPacketSize));
    flood.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    flood.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer attackApp = flood.Install(attacker);
    attackApp.Start(Seconds(attackStart));
    attackApp.Stop(Seconds(attackStop));

    // --- Observability (same as Phase 6) ---
    of13Helper->EnableDatapathStats("switch-stats");
    hostLink.EnablePcap("host", hostDevices);

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    monitor->SerializeToXmlFile("flowmon.xml", true, true);

    Simulator::Destroy();

    return 0;
}

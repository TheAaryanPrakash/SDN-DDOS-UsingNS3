// Phase 10: identical topology and traffic to simulations/ddos-attack, but
// this time detection AND mitigation are both active -- "Experiment 3: DDoS
// attack with detection and mitigation" from the final comparison
// (Phase 11). Same SdnController class as every other experiment; the only
// difference is EnableMitigation is left at its default (true) instead of
// being turned off.
//
// Phase 11 extended this file with --numAttackers (default 1) and
// --enablePcap (default true, turn off for bulk experiment sweeps to save
// time/disk -- flowmon.xml, monitor-stats.csv and detections.csv carry all
// the data the Phase 12 graphs actually need) so the same file drives
// Experiments 3, 4, 5, and 6 from the command line, no new files needed.
//
//   Client 1   Client 2   Client 3     Attacker 1  ...  Attacker N
//        \        |         /             |              |
//               SDN Switch -----------------------------------
//                    |
//                 Server   (bottleneck access link)
//
// Expected pipeline, matching the project's overall workflow:
//   Normal traffic -> attack begins -> controller's periodic port-stats
//   polling (Phase 8) measures each attacker's rate -> threshold comparison
//   (Phase 9) flags any port over threshold -> controller pushes a DROP
//   flow-mod for that port (Phase 10) -> blocked attackers' packets never
//   reach the bottleneck link to the server -> legitimate clients'
//   throughput/loss recovers.

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

NS_LOG_COMPONENT_DEFINE("DdosMitigated");

int
main(int argc, char* argv[])
{
    uint32_t numClients = 3;
    double simTime = 20.0;
    std::string perClientRate = "500Kbps";
    uint32_t clientPacketSize = 512;

    uint32_t numAttackers = 1;
    std::string attackRate = "20Mbps"; // per attacker
    uint32_t attackPacketSize = 64;
    double attackStart = 10.0;
    double attackStop = -1.0;

    std::string hostLinkRate = "100Mbps";
    std::string serverLinkRate = "10Mbps";

    double threshold = 1000.0;    // packets/sec
    double monitorInterval = 1.0; // seconds

    bool enablePcap = true;

    CommandLine cmd;
    cmd.AddValue("numClients", "Number of legitimate client nodes", numClients);
    cmd.AddValue("simTime", "Simulation duration in seconds", simTime);
    cmd.AddValue("perClientRate", "UDP rate per legitimate client", perClientRate);
    cmd.AddValue("clientPacketSize", "Legitimate client UDP payload size", clientPacketSize);
    cmd.AddValue("numAttackers", "Number of attacker nodes", numAttackers);
    cmd.AddValue("attackRate", "Each attacker's flood UDP data rate (e.g. 20Mbps)", attackRate);
    cmd.AddValue("attackPacketSize", "Attacker's flood packet size in bytes", attackPacketSize);
    cmd.AddValue("attackStart", "Attack start time in seconds", attackStart);
    cmd.AddValue("attackStop", "Attack stop time in seconds (-1 = until near sim end)", attackStop);
    cmd.AddValue("serverLinkRate", "Server's (bottleneck) access link data rate", serverLinkRate);
    cmd.AddValue("threshold", "Detection threshold in packets/sec", threshold);
    cmd.AddValue("monitorInterval", "Controller polling interval in seconds", monitorInterval);
    cmd.AddValue("enablePcap", "Write per-port pcap captures (off saves time/disk in sweeps)",
                enablePcap);
    cmd.Parse(argc, argv);

    if (attackStop < 0.0)
    {
        attackStop = simTime - 1.0;
    }

    // --- Nodes ---
    NodeContainer clients;
    clients.Create(numClients);
    NodeContainer attackers;
    attackers.Create(numAttackers);
    Ptr<Node> server = CreateObject<Node>();
    Ptr<Node> switchNode = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    NodeContainer hosts;
    hosts.Add(clients);
    hosts.Add(attackers);
    hosts.Add(server);
    uint32_t serverIndex = numClients + numAttackers;

    // --- Data plane ---
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

    // --- SDN control plane: detection AND mitigation both active ---
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
    Ptr<SdnController> controllerApp = CreateObject<SdnController>();
    controllerApp->SetAttribute("PacketRateThreshold", DoubleValue(threshold));
    controllerApp->SetAttribute("MonitorInterval", TimeValue(Seconds(monitorInterval)));
    // EnableMitigation left at its default (true) -- this is the whole
    // point of this experiment.
    of13Helper->InstallController(controllerNode, controllerApp);
    of13Helper->InstallSwitch(switchNode, switchPorts);
    of13Helper->CreateOpenFlowChannels();

    // --- IP stack on hosts only ---
    InternetStackHelper internet;
    internet.Install(hosts);

    // See documentation/environment.md: a plain FIFO queue disc avoids the
    // auto-installed FqCoDel AQM reshaping traffic below its configured
    // application rate.
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

    // --- Legitimate traffic ---
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

    // --- Attack traffic: each attacker floods independently ---
    for (uint32_t i = 0; i < numAttackers; i++)
    {
        OnOffHelper flood("ns3::UdpSocketFactory", InetSocketAddress(serverAddr, port));
        flood.SetAttribute("DataRate", StringValue(attackRate));
        flood.SetAttribute("PacketSize", UintegerValue(attackPacketSize));
        flood.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        flood.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        ApplicationContainer attackApp = flood.Install(attackers.Get(i));
        attackApp.Start(Seconds(attackStart));
        attackApp.Stop(Seconds(attackStop));
    }

    // --- Observability ---
    of13Helper->EnableDatapathStats("switch-stats");
    if (enablePcap)
    {
        hostLink.EnablePcap("host", hostDevices);
    }

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    monitor->SerializeToXmlFile("flowmon.xml", true, true);

    Simulator::Destroy();

    return 0;
}

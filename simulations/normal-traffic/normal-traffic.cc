// Phase 6: multiple legitimate clients generating normal traffic, through
// the same SDN switch/controller as Phase 5, with no attack traffic yet.
// This is also "Experiment 1: Normal traffic" from the final comparison
// (Phase 11) -- the baseline that attack and mitigation runs get compared
// against.
//
//   Client 1   Client 2   Client 3   ...
//        \        |         /
//             SDN Switch
//                  |
//               Server
//
// Each client opens a steady low-rate UDP stream to the server. FlowMonitor
// records real per-flow throughput/delay/loss statistics to an XML file in
// results/, which the Python scripts (Phase 12) turn into graphs. Nothing
// here is hand-waved: every number in the output comes from the simulation
// actually running.

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

NS_LOG_COMPONENT_DEFINE("NormalTraffic");

int
main(int argc, char* argv[])
{
    uint32_t numClients = 3;
    double simTime = 20.0;
    std::string perClientRate = "500Kbps";
    uint32_t packetSize = 512;

    CommandLine cmd;
    cmd.AddValue("numClients", "Number of legitimate client nodes", numClients);
    cmd.AddValue("simTime", "Simulation duration in seconds", simTime);
    cmd.AddValue("perClientRate", "UDP rate per client (e.g. 500Kbps)", perClientRate);
    cmd.AddValue("packetSize", "UDP payload size in bytes", packetSize);
    cmd.Parse(argc, argv);

    // --- Nodes ---
    NodeContainer clients;
    clients.Create(numClients);
    Ptr<Node> server = CreateObject<Node>();
    Ptr<Node> switchNode = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    NodeContainer hosts;
    hosts.Add(clients);
    hosts.Add(server);
    uint32_t serverIndex = hosts.GetN() - 1;

    // --- Data plane: every host gets its own CSMA link into the switch ---
    CsmaHelper csmaHelper;
    csmaHelper.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    // See documentation/environment.md: a LAN-realistic (microsecond-scale,
    // not millisecond) propagation delay -- CSMA holds the channel busy for
    // a transmission's full propagation delay, so an inflated delay here
    // would cap max packet rate to roughly 1/delay for every host.
    csmaHelper.SetChannelAttribute("Delay", TimeValue(MicroSeconds(2)));

    NetDeviceContainer hostDevices;
    NetDeviceContainer switchPorts;
    for (uint32_t i = 0; i < hosts.GetN(); i++)
    {
        NodeContainer pair(hosts.Get(i), switchNode);
        NetDeviceContainer link = csmaHelper.Install(pair);
        hostDevices.Add(link.Get(0));
        switchPorts.Add(link.Get(1));
    }

    // --- SDN control plane ---
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
    Ptr<SdnController> controllerApp = CreateObject<SdnController>();
    of13Helper->InstallController(controllerNode, controllerApp);
    of13Helper->InstallSwitch(switchNode, switchPorts);
    of13Helper->CreateOpenFlowChannels();

    // --- IP stack on hosts only ---
    InternetStackHelper internet;
    internet.Install(hosts);

    // See documentation/environment.md -- without this, ns-3 auto-installs
    // an FqCoDel AQM queue disc during address assignment, which reshapes
    // traffic based on queueing delay rather than letting the application's
    // configured DataRate determine the on-wire rate. A plain FIFO keeps
    // behavior simple and predictable for these experiments.
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize", StringValue("2000p"));
    tch.Install(hostDevices);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer hostIfaces = ipv4.Assign(hostDevices);
    Ipv4Address serverAddr = hostIfaces.GetAddress(serverIndex);

    // --- Normal traffic: every client -> server, staggered start times ---
    uint16_t port = 9;
    PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(server);
    sinkApp.Start(Seconds(0.5));
    sinkApp.Stop(Seconds(simTime));

    for (uint32_t i = 0; i < numClients; i++)
    {
        OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(serverAddr, port));
        onoff.SetAttribute("DataRate", StringValue(perClientRate));
        onoff.SetAttribute("PacketSize", UintegerValue(packetSize));
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        // Stagger starts slightly so clients don't all begin in lockstep.
        double start = 1.0 + 0.1 * i;
        ApplicationContainer clientApp = onoff.Install(clients.Get(i));
        clientApp.Start(Seconds(start));
        clientApp.Stop(Seconds(simTime - 1.0));
    }

    // --- Observability ---
    of13Helper->EnableDatapathStats("switch-stats");
    csmaHelper.EnablePcap("host", hostDevices);

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    monitor->SerializeToXmlFile("flowmon.xml", true, true);

    Simulator::Destroy();

    return 0;
}

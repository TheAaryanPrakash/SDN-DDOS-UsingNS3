// Phase 5: the smallest working SDN simulation for this project.
//
//      Controller
//          |
//      SDN Switch
//       /       \
//   Client     Server
//
// Goals (nothing more, by design):
//   - the OFSwitch13 switch comes up and talks to our SdnController
//   - normal traffic (ping + a steady UDP stream) flows client -> server
//   - we can observe it: datapath stats CSV + pcap traces on the host ports
//
// DDoS traffic, multiple clients, detection and mitigation are added in
// later phases on top of this same controller.

#include <ns3/applications-module.h>
#include <ns3/core-module.h>
#include <ns3/csma-module.h>
#include <ns3/internet-apps-module.h>
#include <ns3/internet-module.h>
#include <ns3/network-module.h>
#include <ns3/ofswitch13-module.h>
#include <ns3/traffic-control-module.h>

#include "sdn-controller.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Baseline");

int
main(int argc, char* argv[])
{
    double simTime = 10.0;

    CommandLine cmd;
    cmd.AddValue("simTime", "Simulation duration in seconds", simTime);
    cmd.Parse(argc, argv);

    // --- Nodes ---
    Ptr<Node> client = CreateObject<Node>();
    Ptr<Node> server = CreateObject<Node>();
    Ptr<Node> switchNode = CreateObject<Node>();
    Ptr<Node> controllerNode = CreateObject<Node>();

    NodeContainer hosts;
    hosts.Add(client);
    hosts.Add(server);

    // --- Data plane: client and server each get a CSMA link into the switch ---
    CsmaHelper csmaHelper;
    csmaHelper.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    // A LAN-realistic propagation delay. CSMA models a shared bus where a
    // transmission holds the channel busy for its full propagation delay,
    // not just its serialization time -- an (unrealistic) millisecond-scale
    // delay here would cap the max sustainable packet rate to roughly
    // 1/delay regardless of any application's configured send rate. See
    // documentation/environment.md.
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

    // --- SDN control plane: our controller manages the OpenFlow switch ---
    Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper>();
    Ptr<SdnController> controllerApp = CreateObject<SdnController>();
    of13Helper->InstallController(controllerNode, controllerApp);
    of13Helper->InstallSwitch(switchNode, switchPorts);
    of13Helper->CreateOpenFlowChannels();

    // --- IP stack on the hosts only (never on switch/controller nodes) ---
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

    Ipv4Address serverAddr = hostIfaces.GetAddress(1);

    // --- Normal traffic ---
    // A steady low-rate UDP stream stands in for "ordinary client traffic"
    // that later phases will contrast against flood traffic.
    uint16_t port = 9;
    PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(server);
    sinkApp.Start(Seconds(0.5));
    sinkApp.Stop(Seconds(simTime));

    OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(serverAddr, port));
    onoff.SetAttribute("DataRate", StringValue("1Mbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(512));
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer clientApp = onoff.Install(client);
    clientApp.Start(Seconds(1.0));
    clientApp.Stop(Seconds(simTime - 1.0));

    // A simple ping is the clearest sanity check that the switch/controller
    // pipeline is actually forwarding packets end to end.
    PingHelper pingHelper(serverAddr);
    pingHelper.SetAttribute("VerboseMode", EnumValue(Ping::VerboseMode::VERBOSE));
    ApplicationContainer pingApps = pingHelper.Install(client);
    pingApps.Start(Seconds(1.0));

    // --- Observability ---
    // Per-second switch datapath statistics (packet/byte counters, flow
    // table size, etc.) and pcap captures on the host-facing switch ports.
    of13Helper->EnableDatapathStats("switch-stats");
    csmaHelper.EnablePcap("host", hostDevices);

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}

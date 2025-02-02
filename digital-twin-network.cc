#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/point-to-point-module.h"
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("HelloSimulator");

Ptr<OnOffApplication> onOffApp;
uint32_t optimizationIterations = 0; // Counter for optimization iterations
const uint32_t maxIterations = 1;   // Maximum number of optimization iterations

void WriteMetricsToFile(double throughput, double avgDelay, uint32_t packetLoss, std::string dataRate) {
    std::ofstream metricsFile;
    metricsFile.open("metrics.txt", std::ios::app);
    metricsFile << throughput << " " << avgDelay << " " << packetLoss << " " << dataRate << std::endl;
    metricsFile.close();
}

std::string ReadActionFromFile() {
    std::ifstream actionFile;
    actionFile.open("action.txt", std::ios::in);
    std::string action;
    actionFile >> action;
    actionFile.close();
    return action;
}

void AdjustDataRate(Ptr<FlowMonitor> monitor) {
    monitor->CheckForLostPackets();
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    double totalThroughput = 0.0, totalPacketLoss = 0.0, totalDelay = 0.0;
    int flowCount = 0;

    for (auto &flow : stats) {
        double duration = flow.second.timeLastRxPacket.GetSeconds() - flow.second.timeFirstTxPacket.GetSeconds();
        double throughput = (duration > 0) ? (flow.second.rxBytes * 8.0 / (1e6 * duration)) : 0;
        double packetLoss = (flow.second.txPackets > 0) ? ((double)(flow.second.txPackets - flow.second.rxPackets) / flow.second.txPackets) * 100 : 0;
        double avgDelay = (flow.second.rxPackets > 0) ? (flow.second.delaySum.GetSeconds() / flow.second.rxPackets) * 1000 : 0;

        totalThroughput += throughput;
        totalPacketLoss += packetLoss;
        totalDelay += avgDelay;
        flowCount++;
    }

    double avgThroughput = (flowCount > 0) ? totalThroughput / flowCount : 0.0;
    double avgPacketLoss = (flowCount > 0) ? totalPacketLoss / flowCount : 0.0;
    double avgDelay = (flowCount > 0) ? totalDelay / flowCount : 0.0;

    NS_LOG_UNCOND("Average Throughput: " << avgThroughput << " Mbps");
    NS_LOG_UNCOND("Average Packet Loss: " << avgPacketLoss << "%");
    NS_LOG_UNCOND("Average Delay: " << avgDelay << " ms");

    // Save metrics for RL agent
    WriteMetricsToFile(avgThroughput, avgDelay, avgPacketLoss, "");

    // Stop simulation to allow RL agent to process data
    Simulator::Stop();
}


int main(int argc, char *argv[]) {

   // LogComponentEnable("HelloSimulator", LOG_LEVEL_INFO);
   // LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
   // LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
   // LogComponentEnable("FlowMonitor", LOG_LEVEL_INFO);
   // LogComponentEnable("WifiMac", LOG_LEVEL_INFO);  // Log Wi-Fi MAC layer activity
   // LogComponentEnable("WifiPhy", LOG_LEVEL_INFO);  // Log Wi-Fi PHY layer activity

    uint32_t nIoTDevices = 4;
    double simulationTime = 60.0;
    std::string action = ReadActionFromFile(); // Read initial data rate

    NS_LOG_UNCOND("Initial Data Rate: " << action);

    CommandLine cmd;
    cmd.AddValue("nIoTDevices", "Number of IoT devices", nIoTDevices);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("action", "Action (data rate)", action);
    cmd.Parse(argc, argv);

    NodeContainer iotNodes;
    iotNodes.Create(nIoTDevices);
    Ptr<Node> gateway = iotNodes.Get(0);

    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX", DoubleValue(0.0),
                                  "MinY", DoubleValue(0.0),
                                  "DeltaX", DoubleValue(25.5),
                                  "DeltaY", DoubleValue(25.0),
                                  "GridWidth", UintegerValue(3),
                                  "LayoutType", StringValue("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(iotNodes);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    Ptr<YansWifiChannel> wifiChannel = channel.Create();
    phy.SetChannel(wifiChannel);

    WifiMacHelper mac;
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(Ssid("smart-home")));

    NetDeviceContainer apDevice = wifi.Install(phy, mac, gateway);

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(Ssid("smart-home")));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, NodeContainer(iotNodes.Get(1), iotNodes.Get(2), iotNodes.Get(3)));

    InternetStackHelper stack;
    stack.Install(iotNodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(apDevice);
    interfaces.Add(address.Assign(staDevices));

    uint16_t port = 9;

    OnOffHelper thermostat("ns3::UdpSocketFactory", InetSocketAddress(interfaces.GetAddress(0), port));
    thermostat.SetAttribute("DataRate", DataRateValue(DataRate(action)));
    thermostat.SetAttribute("PacketSize", UintegerValue(1024));
    thermostat.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=5]"));
    thermostat.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    ApplicationContainer thermostatApp = thermostat.Install(iotNodes.Get(1));
    onOffApp = thermostatApp.Get(0)->GetObject<OnOffApplication>();
    thermostatApp.Start(Seconds(1.0));
    thermostatApp.Stop(Seconds(simulationTime));

    OnOffHelper camera("ns3::UdpSocketFactory", InetSocketAddress(interfaces.GetAddress(0), port + 1));
    camera.SetAttribute("DataRate", DataRateValue(DataRate("10Mbps")));
    camera.SetAttribute("PacketSize", UintegerValue(1024));
    camera.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    camera.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    ApplicationContainer cameraApp = camera.Install(iotNodes.Get(2));
    cameraApp.Start(Seconds(1.0));
    cameraApp.Stop(Seconds(simulationTime));

    OnOffHelper lights("ns3::UdpSocketFactory", InetSocketAddress(interfaces.GetAddress(0), port + 2));
    lights.SetAttribute("DataRate", DataRateValue(DataRate("1Mbps")));
    lights.SetAttribute("PacketSize", UintegerValue(1024));
    lights.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=10]"));
    lights.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    ApplicationContainer lightsApp = lights.Install(iotNodes.Get(3));
    lightsApp.Start(Seconds(1.0));
    lightsApp.Stop(Seconds(simulationTime));

    PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(gateway);
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simulationTime));

    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    flowMonitor = flowHelper.InstallAll();

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Schedule(Seconds(simulationTime - 5), &AdjustDataRate, flowMonitor);

    Simulator::Run();
    Simulator::Destroy();

    return 0; // Exit NS-3 after one iteration
}

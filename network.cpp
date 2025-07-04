// network.cpp - Network monitoring implementation
#include "header.h"

// Global variables for network monitoring
static map<string, RX> current_rx_stats;
static map<string, TX> current_tx_stats;
static Networks current_networks;
static mutex network_mutex;
static bool network_data_ready = false;

// Parse /proc/net/dev to get network statistics
void parseNetworkDevFile()
{
    lock_guard<mutex> lock(network_mutex);

    ifstream file("/proc/net/dev");
    if (!file.is_open())
    {
        return;
    }

    string line;
    // Skip header lines
    getline(file, line);
    getline(file, line);

    current_rx_stats.clear();
    current_tx_stats.clear();

    while (getline(file, line))
    {
        // Remove leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == string::npos)
            continue;
        line = line.substr(start);

        // Find interface name (ends with ':')
        size_t colon_pos = line.find(':');
        if (colon_pos == string::npos)
            continue;

        string interface_name = line.substr(0, colon_pos);
        string stats_line = line.substr(colon_pos + 1);

        // Parse statistics
        istringstream iss(stats_line);
        vector<long long> values;
        long long value;

        while (iss >> value)
        {
            values.push_back(value);
        }

        if (values.size() >= 16)
        {
            // RX statistics (first 8 values)
            RX rx_stats;
            rx_stats.bytes = values[0];
            rx_stats.packets = values[1];
            rx_stats.errs = values[2];
            rx_stats.drop = values[3];
            rx_stats.fifo = values[4];
            rx_stats.frame = values[5];
            rx_stats.compressed = values[6];
            rx_stats.multicast = values[7];
            current_rx_stats[interface_name] = rx_stats;

            // TX statistics (next 8 values)
            TX tx_stats;
            tx_stats.bytes = values[8];
            tx_stats.packets = values[9];
            tx_stats.errs = values[10];
            tx_stats.drop = values[11];
            tx_stats.fifo = values[12];
            tx_stats.colls = values[13];
            tx_stats.carrier = values[14];
            tx_stats.compressed = values[15];
            current_tx_stats[interface_name] = tx_stats;
        }
    }

    file.close();
    network_data_ready = true;
}

// Get network interfaces with IPv4 addresses
Networks getNetworkInterfaces()
{
    Networks networks;
    networks.ip4s.clear();

    struct ifaddrs *ifaddr, *ifa;
    char addressBuffer[INET_ADDRSTRLEN];

    if (getifaddrs(&ifaddr) == -1)
    {
        return networks;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        // Check for IPv4 addresses
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in *sa_in = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &(sa_in->sin_addr), addressBuffer, INET_ADDRSTRLEN);

            IP4 ip4;
            ip4.name = strdup(ifa->ifa_name);
            strcpy(ip4.addressBuffer, addressBuffer);
            networks.ip4s.push_back(ip4);
        }
    }

    freeifaddrs(ifaddr);
    current_networks = networks;
    return networks;
}

// Smart unit conversion for network bytes
string formatNetworkBytes(uint64_t bytes)
{
    if (bytes < 1024)
    {
        return to_string(bytes) + " B";
    }
    else if (bytes < 1024 * 1024)
    {
        double kb = bytes / 1024.0;
        if (kb < 100)
        {
            return to_string((int)(kb * 100) / 100.0) + " KB";
        }
        else
        {
            return to_string((int)kb) + " KB";
        }
    }
    else if (bytes < 1024 * 1024 * 1024)
    {
        double mb = bytes / (1024.0 * 1024.0);
        if (mb < 100)
        {
            return to_string((int)(mb * 100) / 100.0) + " MB";
        }
        else
        {
            return to_string((int)mb) + " MB";
        }
    }
    else
    {
        double gb = bytes / (1024.0 * 1024.0 * 1024.0);
        return to_string((int)(gb * 100) / 100.0) + " GB";
    }
}

// Calculate progress for 2GB scale (0.0 to 1.0)
float calculateNetworkProgress(uint64_t bytes)
{
    const uint64_t max_scale = 2ULL * 1024 * 1024 * 1024; // 2GB
    if (bytes >= max_scale)
        return 1.0f;
    return (float)bytes / (float)max_scale;
}

// Render network interfaces list
void renderNetworkInterfaces()
{
    if (ImGui::CollapsingHeader("Network Interfaces"))
    {
        ImGui::Columns(2, "NetworkInterfaces", true);
        ImGui::Text("Interface");
        ImGui::NextColumn();
        ImGui::Text("IPv4 Address");
        ImGui::NextColumn();
        ImGui::Separator();

        for (const auto &ip4 : current_networks.ip4s)
        {
            ImGui::Text("%s", ip4.name);
            ImGui::NextColumn();
            ImGui::Text("%s", ip4.addressBuffer);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }
}

// Render RX statistics table
void renderRXTable()
{
    if (!network_data_ready)
        return;

    lock_guard<mutex> lock(network_mutex);

    if (ImGui::BeginTable("RX_Table", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Interface");
        ImGui::TableSetupColumn("Bytes");
        ImGui::TableSetupColumn("Packets");
        ImGui::TableSetupColumn("Errs");
        ImGui::TableSetupColumn("Drop");
        ImGui::TableSetupColumn("Fifo");
        ImGui::TableSetupColumn("Frame");
        ImGui::TableSetupColumn("Compressed");
        ImGui::TableSetupColumn("Multicast");
        ImGui::TableHeadersRow();

        for (const auto &pair : current_rx_stats)
        {
            const string &interface = pair.first;
            const RX &stats = pair.second;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", interface.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", formatNetworkBytes(stats.bytes).c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", stats.packets);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", stats.errs);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", stats.drop);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%d", stats.fifo);
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%d", stats.frame);
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%d", stats.compressed);
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%d", stats.multicast);
        }

        ImGui::EndTable();
    }
}

// Render TX statistics table
void renderTXTable()
{
    if (!network_data_ready)
        return;

    lock_guard<mutex> lock(network_mutex);

    if (ImGui::BeginTable("TX_Table", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Interface");
        ImGui::TableSetupColumn("Bytes");
        ImGui::TableSetupColumn("Packets");
        ImGui::TableSetupColumn("Errs");
        ImGui::TableSetupColumn("Drop");
        ImGui::TableSetupColumn("Fifo");
        ImGui::TableSetupColumn("Colls");
        ImGui::TableSetupColumn("Carrier");
        ImGui::TableSetupColumn("Compressed");
        ImGui::TableHeadersRow();

        for (const auto &pair : current_tx_stats)
        {
            const string &interface = pair.first;
            const TX &stats = pair.second;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", interface.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", formatNetworkBytes(stats.bytes).c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", stats.packets);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", stats.errs);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", stats.drop);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%d", stats.fifo);
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%d", stats.colls);
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%d", stats.carrier);
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%d", stats.compressed);
        }

        ImGui::EndTable();
    }
}

// Render network usage bars for RX
void renderRXUsageBars()
{
    if (!network_data_ready)
        return;

    lock_guard<mutex> lock(network_mutex);

    ImGui::Text("RX (Incoming) Network Usage:");
    ImGui::Separator();

    for (const auto &pair : current_rx_stats)
    {
        const string &interface = pair.first;
        const RX &stats = pair.second;

        float progress = calculateNetworkProgress(stats.bytes);
        string usage_text = formatNetworkBytes(stats.bytes) + " / 2GB";

        ImGui::Text("%s", interface.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);

        // Use green color for RX (incoming)
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
        ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f), usage_text.c_str());
        ImGui::PopStyleColor();
    }
}

// Render network usage bars for TX
void renderTXUsageBars()
{
    if (!network_data_ready)
        return;

    lock_guard<mutex> lock(network_mutex);

    ImGui::Text("TX (Outgoing) Network Usage:");
    ImGui::Separator();

    for (const auto &pair : current_tx_stats)
    {
        const string &interface = pair.first;
        const TX &stats = pair.second;

        float progress = calculateNetworkProgress(stats.bytes);
        string usage_text = formatNetworkBytes(stats.bytes) + " / 2GB";

        ImGui::Text("%s", interface.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);

        // Use blue color for TX (outgoing)
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.2f, 0.8f, 1.0f));
        ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f), usage_text.c_str());
        ImGui::PopStyleColor();
    }
}
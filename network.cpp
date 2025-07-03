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

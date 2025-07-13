/**
 * @file network.cpp
 * @brief Network monitoring implementation for Linux systems
 * @details This file provides functionality to monitor network interfaces,
 *          parse network statistics from /proc/net/dev, and render network
 *          information using ImGui for visualization.
 * @author Stephen Kisengese
 * @date 2025
 */

#include "header.h"

// =============================================================================
// GLOBAL VARIABLES AND STATE MANAGEMENT
// =============================================================================

/**
 * @brief Global map storing current RX (receive) statistics for all network interfaces
 * @details Maps interface name to RX statistics structure containing bytes, packets, errors, etc.
 */
static map<string, RX> current_rx_stats;

/**
 * @brief Global map storing current TX (transmit) statistics for all network interfaces
 * @details Maps interface name to TX statistics structure containing bytes, packets, errors, etc.
 */
static map<string, TX> current_tx_stats;

/**
 * @brief Global structure storing information about all active network interfaces
 * @details Contains IPv4 addresses and interface names for all detected interfaces
 */
static Networks current_networks;

/**
 * @brief Mutex for thread-safe access to network statistics data
 * @details Protects concurrent access to current_rx_stats and current_tx_stats
 */
static mutex network_mutex;

/**
 * @brief Flag indicating whether network data has been successfully parsed and is ready for use
 * @details Set to true after successful parsing of /proc/net/dev, false otherwise
 */
static bool network_data_ready = false;

// =============================================================================
// NETWORK STATISTICS PARSING
// =============================================================================

/**
 * @brief Parse /proc/net/dev file to extract network interface statistics
 * @details Reads the Linux kernel's network device statistics file and populates
 *          the global RX and TX statistics maps. The file format contains:
 *          - Header lines (first 2 lines are skipped)
 *          - One line per interface with format: "interface: rx_stats tx_stats"
 *          - Each line contains 16 numeric values (8 RX + 8 TX statistics)
 * 
 * @note This function is thread-safe and uses a mutex lock
 * @note Sets network_data_ready to true upon successful parsing
 * @note Clears previous statistics before parsing new data
 * 
 * @warning Requires read access to /proc/net/dev (typically available to all users)
 * 
 * RX Statistics (bytes 0-7):
 * - bytes: Total bytes received
 * - packets: Total packets received
 * - errs: Total receive errors
 * - drop: Total packets dropped
 * - fifo: FIFO buffer errors
 * - frame: Frame alignment errors
 * - compressed: Compressed packets received
 * - multicast: Multicast packets received
 * 
 * TX Statistics (bytes 8-15):
 * - bytes: Total bytes transmitted
 * - packets: Total packets transmitted
 * - errs: Total transmit errors
 * - drop: Total packets dropped
 * - fifo: FIFO buffer errors
 * - colls: Collisions detected
 * - carrier: Carrier losses
 * - compressed: Compressed packets transmitted
 */
void parseNetworkDevFile()
{
    lock_guard<mutex> lock(network_mutex);

    ifstream file("/proc/net/dev");
    if (!file.is_open())
    {
        return;
    }

    string line;
    // Skip header lines (contain column descriptions)
    getline(file, line);
    getline(file, line);

    // Clear previous statistics
    current_rx_stats.clear();
    current_tx_stats.clear();

    while (getline(file, line))
    {
        // Remove leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == string::npos)
            continue;
        line = line.substr(start);

        // Find interface name (terminated by ':')
        size_t colon_pos = line.find(':');
        if (colon_pos == string::npos)
            continue;

        string interface_name = line.substr(0, colon_pos);
        string stats_line = line.substr(colon_pos + 1);

        // Parse numeric statistics from the line
        istringstream iss(stats_line);
        vector<long long> values;
        long long value;

        while (iss >> value)
        {
            values.push_back(value);
        }

        // Ensure we have all 16 required statistics values
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

// =============================================================================
// NETWORK INTERFACE DISCOVERY
// =============================================================================

/**
 * @brief Get all network interfaces with their IPv4 addresses
 * @details Uses getifaddrs() system call to enumerate all network interfaces
 *          and extract their IPv4 addresses. Updates the global current_networks
 *          structure with the discovered interfaces.
 * 
 * @return Networks structure containing all discovered IPv4 interfaces
 * @retval Networks.ip4s Vector of IP4 structures with interface names and addresses
 * 
 * @note Only IPv4 addresses are collected (AF_INET family)
 * @note Skips interfaces without addresses (ifa_addr == NULL)
 * @note Memory for interface names is allocated with strdup() - ensure proper cleanup
 * @note Updates global current_networks variable as side effect
 * 
 * @warning Caller should handle the case where getifaddrs() fails
 * @warning Memory allocated for IP4.name should be freed when no longer needed
 * 
 * @example
 * Networks nets = getNetworkInterfaces();
 * for (const auto& ip4 : nets.ip4s) {
 *     printf("Interface: %s, Address: %s\n", ip4.name, ip4.addressBuffer);
 * }
 */
Networks getNetworkInterfaces()
{
    Networks networks;
    networks.ip4s.clear();

    struct ifaddrs *ifaddr, *ifa;
    char addressBuffer[INET_ADDRSTRLEN];

    // Get linked list of interface addresses
    if (getifaddrs(&ifaddr) == -1)
    {
        return networks;
    }

    // Iterate through all interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        // Check for IPv4 addresses only
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in *sa_in = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &(sa_in->sin_addr), addressBuffer, INET_ADDRSTRLEN);

            // Create IP4 structure and add to networks
            IP4 ip4;
            ip4.name = strdup(ifa->ifa_name);
            strcpy(ip4.addressBuffer, addressBuffer);
            networks.ip4s.push_back(ip4);
        }
    }

    // Clean up system resources
    freeifaddrs(ifaddr);
    current_networks = networks;
    return networks;
}

// =============================================================================
// UTILITY FUNCTIONS FOR DATA FORMATTING
// =============================================================================

/**
 * @brief Format network byte values with appropriate units (B, KB, MB, GB)
 * @details Converts raw byte values to human-readable format with automatic
 *          unit selection based on magnitude. Uses decimal precision for
 *          values under 100 in each unit category.
 * 
 * @param bytes Raw byte count to format
 * @return string Formatted string with value and unit (e.g., "1.5 MB", "256 KB")
 * 
 * @note Uses 1024-based conversion (binary prefixes)
 * @note Provides decimal precision for values < 100 in each unit
 * @note Maximum unit is GB (values >= 1GB are shown in GB)
 * 
 * @example
 * formatNetworkBytes(1024) returns "1 KB"
 * formatNetworkBytes(1536) returns "1.5 KB"
 * formatNetworkBytes(1048576) returns "1 MB"
 * formatNetworkBytes(1073741824) returns "1.0 GB"
 */
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

/**
 * @brief Calculate progress percentage for network usage visualization
 * @details Converts byte values to a progress value (0.0 to 1.0) using
 *          a 2GB scale for visualization purposes. Values at or above
 *          2GB will show as 100% progress.
 * 
 * @param bytes Raw byte count to convert
 * @return float Progress value between 0.0 and 1.0
 * @retval 0.0 No bytes transferred
 * @retval 1.0 2GB or more bytes transferred
 * 
 * @note Uses 2GB (2^31 bytes) as the maximum scale
 * @note Primarily used for ImGui progress bars
 * @note Linear scaling between 0 and 2GB
 * 
 * @example
 * calculateNetworkProgress(0) returns 0.0
 * calculateNetworkProgress(1073741824) returns 0.5 (1GB = 50% of 2GB)
 * calculateNetworkProgress(2147483648) returns 1.0 (2GB = 100%)
 */
float calculateNetworkProgress(uint64_t bytes)
{
    const uint64_t max_scale = 2ULL * 1024 * 1024 * 1024; // 2GB
    if (bytes >= max_scale)
        return 1.0f;
    return (float)bytes / (float)max_scale;
}

// =============================================================================
// IMGUI RENDERING FUNCTIONS
// =============================================================================

/**
 * @brief Render network interfaces list in ImGui collapsible header
 * @details Creates a two-column table showing interface names and their
 *          corresponding IPv4 addresses. Uses ImGui::CollapsingHeader for
 *          space-efficient display.
 * 
 * @note Requires current_networks to be populated via getNetworkInterfaces()
 * @note Creates a collapsible section titled "Network Interfaces"
 * @note Uses ImGui::Columns for tabular layout
 * 
 * @warning Must be called within an ImGui rendering context
 * @warning Assumes current_networks.ip4s is valid and populated
 * 
 * Layout:
 * - Column 1: Interface name (e.g., "eth0", "wlan0")
 * - Column 2: IPv4 address (e.g., "192.168.1.100")
 */
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

/**
 * @brief Render RX (receive) statistics in an ImGui table
 * @details Creates a comprehensive table showing all receive statistics
 *          for each network interface. Includes byte formatting for
 *          human-readable display.
 * 
 * @note Requires network_data_ready to be true (call parseNetworkDevFile() first)
 * @note Thread-safe with mutex locking
 * @note Returns early if network data is not ready
 * 
 * @warning Must be called within an ImGui rendering context
 * @warning Requires parseNetworkDevFile() to be called beforehand
 * 
 * Table Columns:
 * - Interface: Network interface name
 * - Bytes: Total bytes received (formatted with units)
 * - Packets: Total packets received
 * - Errs: Receive errors
 * - Drop: Dropped packets
 * - Fifo: FIFO buffer errors
 * - Frame: Frame alignment errors
 * - Compressed: Compressed packets
 * - Multicast: Multicast packets
 * 
 * Features:
 * - Scrollable table with borders
 * - Resizable columns
 * - Automatic byte formatting (B, KB, MB, GB)
 */
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

/**
 * @brief Render TX (transmit) statistics in an ImGui table
 * @details Creates a comprehensive table showing all transmit statistics
 *          for each network interface. Similar to RX table but with
 *          TX-specific columns.
 * 
 * @note Requires network_data_ready to be true (call parseNetworkDevFile() first)
 * @note Thread-safe with mutex locking
 * @note Returns early if network data is not ready
 * 
 * @warning Must be called within an ImGui rendering context
 * @warning Requires parseNetworkDevFile() to be called beforehand
 * 
 * Table Columns:
 * - Interface: Network interface name
 * - Bytes: Total bytes transmitted (formatted with units)
 * - Packets: Total packets transmitted
 * - Errs: Transmit errors
 * - Drop: Dropped packets
 * - Fifo: FIFO buffer errors
 * - Colls: Collision count
 * - Carrier: Carrier losses
 * - Compressed: Compressed packets
 * 
 * Features:
 * - Scrollable table with borders
 * - Resizable columns
 * - Automatic byte formatting (B, KB, MB, GB)
 */
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

/**
 * @brief Render RX (receive) usage as progress bars
 * @details Creates visual progress bars for each network interface showing
 *          receive usage as a percentage of a 2GB scale. Uses green color
 *          to indicate incoming traffic.
 * 
 * @note Requires network_data_ready to be true (call parseNetworkDevFile() first)
 * @note Thread-safe with mutex locking
 * @note Returns early if network data is not ready
 * 
 * @warning Must be called within an ImGui rendering context
 * @warning Requires parseNetworkDevFile() to be called beforehand
 * 
 * Visual Features:
 * - Green progress bars (RGB: 0.2, 0.8, 0.2) for incoming traffic
 * - Progress scale: 0 to 2GB (100%)
 * - Text overlay showing "current usage / 2GB"
 * - Interface name displayed alongside each bar
 * - Full-width progress bars
 * 
 * Layout:
 * - Section title: "RX (Incoming) Network Usage:"
 * - Separator line
 * - One progress bar per interface
 * - Interface name on the left, progress bar fills remaining width
 */
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

        // Use green color for RX (incoming traffic)
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
        ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f), usage_text.c_str());
        ImGui::PopStyleColor();
    }
}

/**
 * @brief Render TX (transmit) usage as progress bars
 * @details Creates visual progress bars for each network interface showing
 *          transmit usage as a percentage of a 2GB scale. Uses blue color
 *          to indicate outgoing traffic.
 * 
 * @note Requires network_data_ready to be true (call parseNetworkDevFile() first)
 * @note Thread-safe with mutex locking
 * @note Returns early if network data is not ready
 * 
 * @warning Must be called within an ImGui rendering context
 * @warning Requires parseNetworkDevFile() to be called beforehand
 * 
 * Visual Features:
 * - Blue progress bars (RGB: 0.2, 0.2, 0.8) for outgoing traffic
 * - Progress scale: 0 to 2GB (100%)
 * - Text overlay showing "current usage / 2GB"
 * - Interface name displayed alongside each bar
 * - Full-width progress bars
 * 
 * Layout:
 * - Section title: "TX (Outgoing) Network Usage:"
 * - Separator line
 * - One progress bar per interface
 * - Interface name on the left, progress bar fills remaining width
 */
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

        // Use blue color for TX (outgoing traffic)
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.2f, 0.8f, 1.0f));
        ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f), usage_text.c_str());
        ImGui::PopStyleColor();
    }
}

// =============================================================================
// USAGE EXAMPLE AND INTEGRATION NOTES
// =============================================================================

/*
 * TYPICAL USAGE PATTERN:
 * 
 * 1. Initialize network monitoring:
 *    Networks networks = getNetworkInterfaces();
 *    parseNetworkDevFile();
 * 
 * 2. In your main loop (ImGui render loop):
 *    // Update network data periodically
 *    parseNetworkDevFile();
 *    
 *    // Render network information
 *    renderNetworkInterfaces();
 *    renderRXTable();
 *    renderTXTable();
 *    renderRXUsageBars();
 *    renderTXUsageBars();
 * 
 * 3. Performance considerations:
 *    - parseNetworkDevFile() should be called periodically (e.g., every 1-2 seconds)
 *    - Rendering functions can be called every frame
 *    - Use ImGui::CollapsingHeader to hide unused sections
 * 
 * THREAD SAFETY:
 * - All functions are thread-safe through mutex locking
 * - parseNetworkDevFile() can be called from a background thread
 * - Rendering functions must be called from the main ImGui thread
 * 
 * MEMORY MANAGEMENT:
 * - IP4.name strings are allocated with strdup() and should be freed
 * - Global maps are automatically managed
 * - No manual cleanup required for statistics data
 * 
 * ERROR HANDLING:
 * - Functions gracefully handle missing /proc/net/dev file
 * - Interface enumeration failures are handled silently
 * - Invalid data is skipped without crashing
 * 
 * PLATFORM REQUIREMENTS:
 * - Linux system with /proc/net/dev support
 * - POSIX-compliant system for getifaddrs()
 * - ImGui library for rendering functions
 * - C++11 or later for mutex and threading support
 */
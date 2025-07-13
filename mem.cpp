/**
 * @file system_monitor.cpp
 * @brief System monitoring application with process management and memory tracking
 * @details This file implements a system monitor that displays real-time information about
 *          system memory (RAM, SWAP, disk usage) and running processes. It uses ImGui for
 *          the user interface and reads system information from /proc filesystem.
 * 
 * Features:
 * - Real-time memory usage monitoring (RAM, SWAP, disk)
 * - Process list with CPU and memory usage
 * - Process filtering and selection
 * - Sortable process table
 * - Color-coded usage indicators
 * 
 * @author System Monitor Team
 * @version 1.0
 * @date 2024
 */

#include "header.h"

//=============================================================================
// DATA STRUCTURES
//=============================================================================

/**
 * @struct ProcessCPUData
 * @brief Stores CPU usage tracking data for individual processes
 * @details This structure maintains historical CPU time data to calculate
 *          CPU usage percentage over time intervals.
 */
struct ProcessCPUData
{
    long long prev_utime;                           ///< Previous user CPU time in ticks
    long long prev_stime;                           ///< Previous system CPU time in ticks
    float cpu_percent;                              ///< Calculated CPU usage percentage
    chrono::steady_clock::time_point last_update;   ///< Timestamp of last update
};

//=============================================================================
// GLOBAL VARIABLES
//=============================================================================

// Process selection and filtering
static set<int> selected_pids;                     ///< Set of currently selected process IDs
static char process_filter[256] = "";              ///< Process name filter string
static map<int, ProcessCPUData> process_cpu_data;  ///< Map of PID to CPU usage data
static chrono::steady_clock::time_point last_process_update; ///< Last process data update time
static mutex process_cpu_mutex;                    ///< Mutex for thread-safe CPU data access

//=============================================================================
// MEMORY MONITORING FUNCTIONS
//=============================================================================

/**
 * @brief Retrieves current system memory information
 * @return MemoryInfo structure containing RAM, SWAP, and disk usage data
 * @details Parses /proc/meminfo for memory statistics and uses statvfs()
 *          for disk usage information. All values are returned in bytes.
 * 
 * Memory types tracked:
 * - RAM: Total and available memory
 * - SWAP: Total and used swap space
 * - Disk: Total and used disk space for root filesystem
 */
MemoryInfo getMemoryInfo()
{
    MemoryInfo info = {};

    // Parse /proc/meminfo for RAM and SWAP information
    ifstream meminfo("/proc/meminfo");
    string line;

    while (getline(meminfo, line))
    {
        if (line.find("MemTotal:") == 0)
        {
            sscanf(line.c_str(), "MemTotal: %lu kB", &info.total_ram);
            info.total_ram *= 1024; // Convert from kB to bytes
        }
        else if (line.find("MemAvailable:") == 0)
        {
            sscanf(line.c_str(), "MemAvailable: %lu kB", &info.available_ram);
            info.available_ram *= 1024; // Convert from kB to bytes
        }
        else if (line.find("SwapTotal:") == 0)
        {
            sscanf(line.c_str(), "SwapTotal: %lu kB", &info.total_swap);
            info.total_swap *= 1024; // Convert from kB to bytes
        }
        else if (line.find("SwapFree:") == 0)
        {
            unsigned long swap_free;
            sscanf(line.c_str(), "SwapFree: %lu kB", &swap_free);
            info.used_swap = info.total_swap - (swap_free * 1024);
        }
    }

    // Calculate used RAM from total and available
    info.used_ram = info.total_ram - info.available_ram;

    // Get disk usage using statvfs system call
    struct statvfs disk_info;
    if (statvfs("/", &disk_info) == 0)
    {
        info.total_disk = disk_info.f_blocks * disk_info.f_frsize;
        info.used_disk = (disk_info.f_blocks - disk_info.f_bavail) * disk_info.f_frsize;
    }

    return info;
}

/**
 * @brief Calculates memory usage percentage
 * @param used Amount of memory used (in bytes)
 * @param total Total amount of memory available (in bytes)
 * @return Usage percentage as float (0.0 to 100.0)
 * @details Handles division by zero by returning 0.0 if total is zero
 */
float calculateMemoryUsage(unsigned long used, unsigned long total)
{
    if (total == 0)
        return 0.0f;
    return (float(used) / float(total)) * 100.0f;
}

/**
 * @brief Formats byte values to human-readable format
 * @param bytes Number of bytes to format
 * @return Formatted string with appropriate unit (B, KB, MB, GB, TB)
 * @details Automatically selects the most appropriate unit and formats
 *          with 1 decimal place for units larger than bytes
 * 
 * @example
 * formatBytes(1024) returns "1.0 KB"
 * formatBytes(1048576) returns "1.0 MB"
 */
string formatBytes(unsigned long bytes)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = bytes;

    // Find appropriate unit by dividing by 1024
    while (size >= 1024.0 && unit_index < 4)
    {
        size /= 1024.0;
        unit_index++;
    }

    // Format with appropriate precision
    stringstream ss;
    ss << fixed;
    if (unit_index == 0)
    {
        ss.precision(0); // No decimal places for bytes
    }
    else
    {
        ss.precision(1); // One decimal place for larger units
    }
    ss << size << " " << units[unit_index];
    return ss.str();
}

/**
 * @brief Determines color based on usage percentage
 * @param percentage Usage percentage (0.0 to 100.0)
 * @return ImVec4 color value for ImGui rendering
 * @details Color coding:
 *          - Green: < 70% usage (safe)
 *          - Yellow: 70-90% usage (warning)
 *          - Red: > 90% usage (critical)
 */
ImVec4 getUsageColor(float percentage)
{
    if (percentage < 70.0f)
    {
        return ImVec4(0.0f, 0.8f, 0.0f, 1.0f); // Green
    }
    else if (percentage < 90.0f)
    {
        return ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
    }
    else
    {
        return ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
    }
}

/**
 * @brief Renders memory usage bars in the ImGui interface
 * @details Creates visual progress bars for RAM, SWAP, and disk usage
 *          with color-coded indicators and formatted text labels.
 *          Each bar shows percentage, used/total amounts, and visual indicator.
 * 
 * Layout:
 * - RAM Usage: Always displayed
 * - SWAP Usage: Only displayed if swap is available
 * - Disk Usage: Shows root filesystem usage
 */
void renderMemoryBars()
{
    MemoryInfo mem_info = getMemoryInfo();

    // RAM Usage Bar
    float ram_percentage = calculateMemoryUsage(mem_info.used_ram, mem_info.total_ram);
    ImGui::Text("RAM Usage:");
    ImGui::SameLine();
    ImGui::Text("%.1f%% (%s / %s)",
                ram_percentage,
                formatBytes(mem_info.used_ram).c_str(),
                formatBytes(mem_info.total_ram).c_str());

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, getUsageColor(ram_percentage));
    ImGui::ProgressBar(ram_percentage / 100.0f, ImVec2(-1, 0));
    ImGui::PopStyleColor();

    ImGui::Separator();

    // SWAP Usage Bar (only if swap is available)
    if (mem_info.total_swap > 0)
    {
        float swap_percentage = calculateMemoryUsage(mem_info.used_swap, mem_info.total_swap);
        ImGui::Text("SWAP Usage:");
        ImGui::SameLine();
        ImGui::Text("%.1f%% (%s / %s)",
                    swap_percentage,
                    formatBytes(mem_info.used_swap).c_str(),
                    formatBytes(mem_info.total_swap).c_str());

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, getUsageColor(swap_percentage));
        ImGui::ProgressBar(swap_percentage / 100.0f, ImVec2(-1, 0));
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::Text("SWAP Usage: Not available");
        ImGui::ProgressBar(0.0f, ImVec2(-1, 0));
    }

    ImGui::Separator();

    // Disk Usage Bar
    float disk_percentage = calculateMemoryUsage(mem_info.used_disk, mem_info.total_disk);
    ImGui::Text("Disk Usage (/):");
    ImGui::SameLine();
    ImGui::Text("%.1f%% (%s / %s)",
                disk_percentage,
                formatBytes(mem_info.used_disk).c_str(),
                formatBytes(mem_info.total_disk).c_str());

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, getUsageColor(disk_percentage));
    ImGui::ProgressBar(disk_percentage / 100.0f, ImVec2(-1, 0));
    ImGui::PopStyleColor();
}

//=============================================================================
// PROCESS MONITORING FUNCTIONS
//=============================================================================

/**
 * @brief Retrieves detailed information about a specific process
 * @param pid Process ID to query
 * @return Proc structure containing process information
 * @details Reads process information from /proc/[pid]/ filesystem:
 *          - /proc/[pid]/comm: Process name
 *          - /proc/[pid]/stat: Process statistics (CPU times, memory, state)
 * 
 * Process state codes:
 * - R: Running
 * - S: Sleeping (interruptible)
 * - D: Disk sleep (uninterruptible)
 * - I: Idle
 * - Z: Zombie
 * - T: Stopped
 */
Proc getProcessInfo(int pid)
{
    Proc proc = {};
    proc.pid = pid;

    // Read process name from /proc/[pid]/comm
    string comm_path = "/proc/" + to_string(pid) + "/comm";
    ifstream comm_file(comm_path);
    if (comm_file.is_open())
    {
        getline(comm_file, proc.name);
        // Remove trailing newline if present
        if (!proc.name.empty() && proc.name.back() == '\n')
        {
            proc.name.pop_back();
        }
    }

    // Read process statistics from /proc/[pid]/stat
    string stat_path = "/proc/" + to_string(pid) + "/stat";
    ifstream stat_file(stat_path);
    if (stat_file.is_open())
    {
        string line;
        getline(stat_file, line);

        // Parse the stat line - format: pid (comm) state ppid ...
        // Process name in parentheses may contain spaces, so we need careful parsing
        size_t first_paren = line.find('(');
        size_t last_paren = line.rfind(')');

        if (first_paren != string::npos && last_paren != string::npos && last_paren > first_paren)
        {
            // Extract PID (before first parenthesis)
            string pid_str = line.substr(0, first_paren);
            proc.pid = stoi(pid_str);

            // Extract process name (between parentheses) - this might be truncated
            string full_name = line.substr(first_paren + 1, last_paren - first_paren - 1);
            if (proc.name.empty())
            {
                proc.name = full_name;
            }

            // Extract remaining fields after last parenthesis
            string remaining = line.substr(last_paren + 1);
            istringstream iss(remaining);
            vector<string> fields;
            string field;

            while (iss >> field)
            {
                fields.push_back(field);
            }

            // Map fields according to /proc/[pid]/stat format
            if (fields.size() >= 22)
            {
                proc.state = fields[0][0];      // State (field 3)
                proc.utime = stoll(fields[11]); // User CPU time in ticks (field 14)
                proc.stime = stoll(fields[12]); // System CPU time in ticks (field 15)
                proc.vsize = stoll(fields[20]); // Virtual memory size in bytes (field 23)
                proc.rss = stoll(fields[21]);   // Resident set size in pages (field 24)
            }
        }
    }

    return proc;
}

/**
 * @brief Retrieves list of all running processes
 * @return Vector of Proc structures containing process information
 * @details Scans /proc directory for numeric subdirectories (PIDs)
 *          and retrieves information for each valid process.
 *          Skips processes that disappear during scanning.
 */
vector<Proc> getAllProcesses()
{
    vector<Proc> processes;

    DIR *proc_dir = opendir("/proc");
    if (proc_dir == nullptr)
    {
        return processes;
    }

    struct dirent *entry;
    while ((entry = readdir(proc_dir)) != nullptr)
    {
        // Check if directory name is numeric (indicates a PID)
        if (entry->d_type == DT_DIR)
        {
            string dir_name = entry->d_name;
            if (all_of(dir_name.begin(), dir_name.end(), ::isdigit))
            {
                int pid = stoi(dir_name);
                try
                {
                    Proc proc = getProcessInfo(pid);
                    if (!proc.name.empty())
                    {
                        processes.push_back(proc);
                    }
                }
                catch (...)
                {
                    // Process might have disappeared during scanning, skip it
                    continue;
                }
            }
        }
    }

    closedir(proc_dir);
    return processes;
}

/**
 * @brief Updates CPU usage data for all processes
 * @details Calculates CPU usage percentage by comparing current CPU times
 *          with previous readings. Updates are throttled to every 3 seconds
 *          to provide stable measurements.
 * 
 * CPU Usage Calculation:
 * - Reads utime and stime from /proc/[pid]/stat
 * - Calculates difference from previous reading
 * - Converts CPU ticks to percentage using system clock ticks per second
 * - Thread-safe using mutex protection
 */
void updateProcessCPUData()
{
    auto now = chrono::steady_clock::now();

    // Update only every 3 seconds to get stable CPU measurements
    if (chrono::duration_cast<chrono::milliseconds>(now - last_process_update).count() < 3000)
    {
        return;
    }

    vector<Proc> current_processes = getAllProcesses();
    lock_guard<mutex> lock(process_cpu_mutex);
    
    for (const auto &proc : current_processes)
    {
        long long current_total = proc.utime + proc.stime;

        auto it = process_cpu_data.find(proc.pid);
        if (it != process_cpu_data.end())
        {
            // Calculate CPU usage from previous measurement
            long long prev_total = it->second.prev_utime + it->second.prev_stime;
            long long cpu_diff = current_total - prev_total;

            // Calculate percentage based on time difference
            auto time_diff = chrono::duration_cast<chrono::milliseconds>(now - it->second.last_update);
            if (time_diff.count() > 0)
            {
                // Convert CPU ticks to percentage
                // sysconf(_SC_CLK_TCK) gives ticks per second
                double time_sec = time_diff.count() / 1000.0;
                double cpu_percent = (cpu_diff / time_sec) / sysconf(_SC_CLK_TCK) * 100.0;

                // Update data with bounds checking
                it->second.prev_utime = proc.utime;
                it->second.prev_stime = proc.stime;
                it->second.cpu_percent = min(cpu_percent, 100.0);
                it->second.last_update = now;
            }
        }
        else
        {
            // First time seeing this process - initialize data
            process_cpu_data[proc.pid] = {proc.utime, proc.stime, 0.0f, now};
        }
    }

    last_process_update = now;
}

/**
 * @brief Calculates process memory usage percentage
 * @param proc Process structure containing memory information
 * @param total_memory Total system memory in bytes
 * @return Memory usage percentage as float (0.0 to 100.0)
 * @details Converts RSS (Resident Set Size) from pages to bytes
 *          assuming 4KB pages (standard on most systems)
 */
float calculateProcessMemory(const Proc &proc, unsigned long total_memory)
{
    if (total_memory == 0)
        return 0.0f;
    // RSS is in pages, typically 4KB each
    unsigned long memory_bytes = proc.rss * 4096;
    return (float(memory_bytes) / float(total_memory)) * 100.0f;
}

/**
 * @brief Filters processes by name using case-insensitive substring matching
 * @param processes Vector of processes to filter
 * @param filter Filter string to match against process names
 * @return Vector of processes matching the filter
 * @details Performs case-insensitive substring search.
 *          Returns all processes if filter is empty.
 */
vector<Proc> filterProcesses(const vector<Proc> &processes, const string &filter)
{
    if (filter.empty())
    {
        return processes;
    }

    vector<Proc> filtered;
    string lower_filter = filter;
    transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(), ::tolower);

    for (const auto &proc : processes)
    {
        string lower_name = proc.name;
        transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

        if (lower_name.find(lower_filter) != string::npos)
        {
            filtered.push_back(proc);
        }
    }

    return filtered;
}

/**
 * @brief Handles process selection logic
 * @details Currently provides a placeholder for selection handling.
 *          Selection state is maintained in the selected_pids global set.
 *          Could be extended to provide actions on selected processes.
 */
void handleProcessSelection()
{
    // Currently we just show selected processes as highlighted
    // This function could be extended to provide actions on selected processes
    // such as killing, changing priority, etc.
}

/**
 * @brief Retrieves cached CPU usage for a specific process
 * @param pid Process ID to query
 * @return CPU usage percentage as float (0.0 to 100.0)
 * @details Thread-safe access to cached CPU usage data.
 *          Returns 0.0 if process data is not available.
 */
float getProcessCPUUsage(int pid)
{
    lock_guard<mutex> lock(process_cpu_mutex);
    auto it = process_cpu_data.find(pid);
    if (it != process_cpu_data.end())
    {
        return it->second.cpu_percent;
    }
    return 0.0f;
}

//=============================================================================
// USER INTERFACE FUNCTIONS
//=============================================================================

/**
 * @brief Renders the main process table with filtering and sorting
 * @param processes Reference to vector of processes to display
 * @details Creates an ImGui table with the following features:
 *          - Process filtering by name
 *          - Multi-selection with Ctrl+Click
 *          - Sortable columns (PID, Name, State, CPU%, Memory%)
 *          - Color-coded process states and high resource usage
 *          - Real-time CPU and memory usage updates
 * 
 * Table Columns:
 * - PID: Process ID (sortable)
 * - Name: Process name (sortable)
 * - State: Process state with color coding (sortable)
 * - CPU %: CPU usage percentage (sortable)
 * - Memory %: Memory usage percentage (sortable)
 * 
 * Interaction:
 * - Click to select single process
 * - Ctrl+Click to select multiple processes
 * - Click column headers to sort
 * - Type in filter box to filter by name
 */
void renderProcessTable(vector<Proc> &processes)
{
    MemoryInfo mem_info = getMemoryInfo();
    updateProcessCPUData();  // Update process CPU data periodically

    // Process Filter Input
    ImGui::Text("Filter processes:");
    ImGui::SameLine();
    ImGui::InputText("##ProcessFilter", process_filter, sizeof(process_filter));

    // Apply filter to process list
    vector<Proc> filtered_processes = filterProcesses(processes, string(process_filter));

    // Display process count and selection info
    ImGui::Text("Processes: %zu (Selected: %zu)", filtered_processes.size(), selected_pids.size());
    
    // Clear selection button
    ImGui::SameLine();
    if (ImGui::Button("Clear Selection"))
    {
        selected_pids.clear();
    }

    // User instructions
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Tip: Ctrl+Click to select multiple processes, Click column headers to sort");

    // Create sortable, resizable table
    if (ImGui::BeginTable("ProcessTable", 5,
                          ImGuiTableFlags_Sortable |
                              ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersOuter |
                              ImGuiTableFlags_BordersV))
    {
        // Setup table columns with sizing and sorting options
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 80.0f, 0);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 150.0f, 1);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_None | ImGuiTableColumnFlags_WidthFixed, 100.0f, 2);
        ImGui::TableSetupColumn("CPU %", ImGuiTableColumnFlags_None | ImGuiTableColumnFlags_WidthFixed, 80.0f, 3);
        ImGui::TableSetupColumn("Memory %", ImGuiTableColumnFlags_None | ImGuiTableColumnFlags_WidthFixed, 100.0f, 4);
        ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row when scrolling
        ImGui::TableHeadersRow();

        // Handle table sorting
        ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs();
        if (sort_specs && sort_specs->SpecsDirty)
        {
            if (sort_specs->SpecsCount > 0)
            {
                const ImGuiTableColumnSortSpecs *spec = &sort_specs->Specs[0];

                // Sort processes based on selected column and direction
                sort(filtered_processes.begin(), filtered_processes.end(),
                     [spec, &mem_info](const Proc &a, const Proc &b)
                     {
                         bool ascending = spec->SortDirection == ImGuiSortDirection_Ascending;

                         switch (spec->ColumnUserID)
                         {
                         case 0: // PID
                             return ascending ? a.pid < b.pid : a.pid > b.pid;
                         case 1: // Name
                             return ascending ? a.name < b.name : a.name > b.name;
                         case 2: // State
                             return ascending ? a.state < b.state : a.state > b.state;
                         case 3: // CPU %
                             return ascending ? getProcessCPUUsage(a.pid) < getProcessCPUUsage(b.pid)
                                              : getProcessCPUUsage(a.pid) > getProcessCPUUsage(b.pid);
                         case 4: // Memory %
                             return ascending ? calculateProcessMemory(a, mem_info.total_ram) < calculateProcessMemory(b, mem_info.total_ram)
                                              : calculateProcessMemory(a, mem_info.total_ram) > calculateProcessMemory(b, mem_info.total_ram);
                         default:
                             return false;
                         }
                     });
            }
            sort_specs->SpecsDirty = false;
        }

        // Render table rows
        for (const auto &proc : filtered_processes)
        {
            ImGui::TableNextRow();
            bool is_selected = selected_pids.find(proc.pid) != selected_pids.end();
            
            // PID column with selection handling
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(("##" + to_string(proc.pid)).c_str(), is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns))
            {
                // Handle multi-selection with Ctrl+Click
                ImGuiIO &io = ImGui::GetIO();
                if (io.KeyCtrl)
                {
                    // Toggle selection
                    if (is_selected)
                    {
                        selected_pids.erase(proc.pid);
                    }
                    else
                    {
                        selected_pids.insert(proc.pid);
                    }
                }
                else
                {
                    // Single selection
                    selected_pids.clear();
                    selected_pids.insert(proc.pid);
                }
            }

            // Display PID in the same cell as selection
            ImGui::SameLine();
            ImGui::Text("%d", proc.pid);

            // Name column
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", proc.name.c_str());

            // State column with color coding
            ImGui::TableSetColumnIndex(2);
            string state_str;
            ImVec4 state_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // Default white
            
            // Map process state to human-readable string and color
            switch (proc.state)
            {
            case 'R':
                state_str = "Running";
                state_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
                break;
            case 'S':
                state_str = "Sleeping";
                state_color = ImVec4(0.0f, 0.7f, 1.0f, 1.0f); // Blue
                break;
            case 'D':
                state_str = "Disk Sleep";
                state_color = ImVec4(1.0f, 0.7f, 0.0f, 1.0f); // Orange
                break;
            case 'I':
                state_str = "Idle";
                state_color = ImVec4(1.0f, 0.0f, 1.0f, 1.0f); // Magenta
                break;
            case 'Z':
                state_str = "Zombie";
                state_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
                break;
            case 'T':
                state_str = "Stopped";
                state_color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); // Gray
                break;
            default:
                state_str = string(1, proc.state);
                break;
            }
            ImGui::TextColored(state_color, "%s", state_str.c_str());

            // CPU % column with highlighting for high usage
            ImGui::TableSetColumnIndex(3);
            float cpu_usage = getProcessCPUUsage(proc.pid);
            if (cpu_usage > 0.1f)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%.1f%%", cpu_usage);
            }
            else
            {
                ImGui::Text("%.1f%%", cpu_usage);
            }

            // Memory % column with highlighting for high usage
            ImGui::TableSetColumnIndex(4);
            float memory_usage = calculateProcessMemory(proc, mem_info.total_ram);
            if (memory_usage > 1.0f)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "%.1f%%", memory_usage);
            }
            else
            {
                ImGui::Text("%.1f%%", memory_usage);
            }
        }

        ImGui::EndTable();
    }
}

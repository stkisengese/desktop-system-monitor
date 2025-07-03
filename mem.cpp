#include "header.h"

// Global variables for process selection and filtering
static set<int> selected_pids;
static char process_filter[256] = "";

// Memory information function
MemoryInfo getMemoryInfo()
{
    MemoryInfo info = {};

    // Parse /proc/meminfo for RAM and SWAP
    ifstream meminfo("/proc/meminfo");
    string line;

    while (getline(meminfo, line))
    {
        if (line.find("MemTotal:") == 0)
        {
            sscanf(line.c_str(), "MemTotal: %lu kB", &info.total_ram);
            info.total_ram *= 1024; // Convert to bytes
        }
        else if (line.find("MemAvailable:") == 0)
        {
            sscanf(line.c_str(), "MemAvailable: %lu kB", &info.available_ram);
            info.available_ram *= 1024; // Convert to bytes
        }
        else if (line.find("SwapTotal:") == 0)
        {
            sscanf(line.c_str(), "SwapTotal: %lu kB", &info.total_swap);
            info.total_swap *= 1024; // Convert to bytes
        }
        else if (line.find("SwapFree:") == 0)
        {
            unsigned long swap_free;
            sscanf(line.c_str(), "SwapFree: %lu kB", &swap_free);
            info.used_swap = info.total_swap - (swap_free * 1024);
        }
    }

    info.used_ram = info.total_ram - info.available_ram;

    // Get disk usage using statvfs
    struct statvfs disk_info;
    if (statvfs("/", &disk_info) == 0)
    {
        info.total_disk = disk_info.f_blocks * disk_info.f_frsize;
        info.used_disk = (disk_info.f_blocks - disk_info.f_bavail) * disk_info.f_frsize;
    }

    return info;
}

// Calculate memory usage percentage
float calculateMemoryUsage(unsigned long used, unsigned long total)
{
    if (total == 0)
        return 0.0f;
    return (float(used) / float(total)) * 100.0f;
}

// Format bytes to human-readable format
string formatBytes(unsigned long bytes)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = bytes;

    while (size >= 1024.0 && unit_index < 4)
    {
        size /= 1024.0;
        unit_index++;
    }

    stringstream ss;
    ss << fixed;
    if (unit_index == 0)
    {
        ss.precision(0);
    }
    else
    {
        ss.precision(1);
    }
    ss << size << " " << units[unit_index];
    return ss.str();
}

// Get color based on usage percentage
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

// Render memory usage bars
void renderMemoryBars()
{
    MemoryInfo mem_info = getMemoryInfo();

    // RAM Usage
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

    // SWAP Usage
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

    // Disk Usage
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

// Get process information from /proc/[pid]/
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
        // Remove newline if present
        if (!proc.name.empty() && proc.name.back() == '\n')
        {
            proc.name.pop_back();
        }
    }

    // Read process stats from /proc/[pid]/stat
    string stat_path = "/proc/" + to_string(pid) + "/stat";
    ifstream stat_file(stat_path);
    if (stat_file.is_open())
    {
        string line;
        getline(stat_file, line);

        // Parse the stat line - be careful with process names containing spaces
        istringstream iss(line);
        string token;
        vector<string> tokens;

        // Split by spaces but handle the process name in parentheses
        bool in_name = false;
        string current_token;

        for (char c : line)
        {
            if (c == '(')
            {
                in_name = true;
                current_token += c;
            }
            else if (c == ')')
            {
                in_name = false;
                current_token += c;
                tokens.push_back(current_token);
                current_token.clear();
            }
            else if (c == ' ' && !in_name)
            {
                if (!current_token.empty())
                {
                    tokens.push_back(current_token);
                    current_token.clear();
                }
            }
            else
            {
                current_token += c;
            }
        }
        if (!current_token.empty())
        {
            tokens.push_back(current_token);
        }

        if (tokens.size() >= 24)
        {
            proc.state = tokens[2][0];      // State is the 3rd field
            proc.utime = stoll(tokens[13]); // User time
            proc.stime = stoll(tokens[14]); // System time
            proc.vsize = stoll(tokens[22]); // Virtual memory size
            proc.rss = stoll(tokens[23]);   // Resident set size
        }
    }

    return proc;
}

// Get all running processes
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
        // Check if directory name is numeric (PID)
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
                    // Process might have disappeared, skip it
                    continue;
                }
            }
        }
    }

    closedir(proc_dir);
    return processes;
}

// Calculate process memory usage percentage
float calculateProcessMemory(const Proc &proc, unsigned long total_memory)
{
    if (total_memory == 0)
        return 0.0f;
    // RSS is in pages, typically 4KB each
    unsigned long memory_bytes = proc.rss * 4096;
    return (float(memory_bytes) / float(total_memory)) * 100.0f;
}

// Filter processes by name
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

// Handle process selection
void handleProcessSelection()
{
    // Currently we just show selcted accounts as highlighted
}

// Render process table
void renderProcessTable(vector<Proc> &processes)
{
    MemoryInfo mem_info = getMemoryInfo();

    // Filter input
    ImGui::Text("Filter processes:");
    ImGui::SameLine();
    bool filter_changed = ImGui::InputText("##ProcessFilter", process_filter, sizeof(process_filter));

    // Filter processes
    vector<Proc> filtered_processes = filterProcesses(processes, string(process_filter));

    // Show count
    ImGui::Text("Processes: %zu (Selected: %zu)", filtered_processes.size(), selected_pids.size());
    // Clear selection button
    ImGui::SameLine();
    if (ImGui::Button("Clear Selection"))
    {
        selected_pids.clear();
    }

    // Instructions
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Tip: Ctrl+Click to select multiple processes, Click column headers to sort");

    // Process table
    if (ImGui::BeginTable("ProcessTable", 5,
                          ImGuiTableFlags_Sortable |
                              ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersOuter |
                              ImGuiTableFlags_BordersV))
    {

        // Setup columns
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 80.0f, 0);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 150.0f, 1);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_None | ImGuiTableColumnFlags_WidthFixed, 100.0f, 2);
        ImGui::TableSetupColumn("CPU %", ImGuiTableColumnFlags_None | ImGuiTableColumnFlags_WidthFixed, 80.0f, 3);
        ImGui::TableSetupColumn("Memory %", ImGuiTableColumnFlags_None | ImGuiTableColumnFlags_WidthFixed, 100.0f, 4);
        ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row
        ImGui::TableHeadersRow();

        // Handle sorting
        ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs();
        if (sort_specs && sort_specs->SpecsDirty)
        {
            if (sort_specs->SpecsCount > 0)
            {
                const ImGuiTableColumnSortSpecs *spec = &sort_specs->Specs[0];

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
                             return ascending ? calculateProcessCPU(a) < calculateProcessCPU(b)
                                              : calculateProcessCPU(a) > calculateProcessCPU(b);
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

        // Render rows
        for (const auto &proc : filtered_processes)
        {
            ImGui::TableNextRow();
            bool is_selected = selected_pids.find(proc.pid) != selected_pids.end(); // Check if this process is selected
            ImGui::TableSetColumnIndex(0);                                          // PID column with selectable
            if (ImGui::Selectable(("##" + to_string(proc.pid)).c_str(), is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns))
            {
                // Handle selection with Ctrl+Click for multi-select
                ImGuiIO &io = ImGui::GetIO();
                if (io.KeyCtrl)
                {
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
                    selected_pids.clear();
                    selected_pids.insert(proc.pid);
                }
            }

            // Display PID in the same cell
            ImGui::SameLine();
            ImGui::Text("%d", proc.pid);

            // Name column
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", proc.name.c_str());

            // State column
            ImGui::TableSetColumnIndex(2);
            string state_str;
            ImVec4 state_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // Default white
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

            // CPU % column
            ImGui::TableSetColumnIndex(3);
            float cpu_usage = calculateProcessCPU(proc);
            if (cpu_usage > 0.1f)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%.1f%%", cpu_usage);
            }
            else
            {
                ImGui::Text("%.1f%%", cpu_usage);
            }

            // Memory % column
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

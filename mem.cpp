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

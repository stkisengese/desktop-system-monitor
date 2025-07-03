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

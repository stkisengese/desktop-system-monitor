#include "header.h"

// get cpu id and information, you can use `proc/cpuinfo`
string CPUinfo()
{
    char CPUBrandString[0x40];
    unsigned int CPUInfo[4] = {0, 0, 0, 0};

    // unix system
    // for windoes maybe we must add the following
    // __cpuid(regs, 0);
    // regs is the array of 4 positions
    __cpuid(0x80000000, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);
    unsigned int nExIds = CPUInfo[0];

    memset(CPUBrandString, 0, sizeof(CPUBrandString));

    for (unsigned int i = 0x80000000; i <= nExIds; ++i)
    {
        __cpuid(i, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);

        if (i == 0x80000002)
            memcpy(CPUBrandString, CPUInfo, sizeof(CPUInfo));
        else if (i == 0x80000003)
            memcpy(CPUBrandString + 16, CPUInfo, sizeof(CPUInfo));
        else if (i == 0x80000004)
            memcpy(CPUBrandString + 32, CPUInfo, sizeof(CPUInfo));
    }
    string str(CPUBrandString);
    return str;
}

// getOsName, this will get the OS of the current computer
const char *getOsName()
{
#ifdef _WIN32
    return "Windows 32-bit";
#elif _WIN64
    return "Windows 64-bit";
#elif __APPLE__ || __MACH__
    return "Mac OSX";
#elif __linux__
    return "Linux";
#elif __FreeBSD__
    return "FreeBSD";
#elif __unix || __unix__
    return "Unix";
#else
    return "Other";
#endif
}

// Function to read hostname from /proc/sys/kernel/hostname
string getHostname()
{
    ifstream file("/proc/sys/kernel/hostname");
    string hostname;

    if (file.is_open())
    {
        getline(file, hostname);
        file.close();

        // Remove any trailing whitespace/newlines
        hostname.erase(hostname.find_last_not_of(" \t\n\r\f\v") + 1);
    }
    else
    {
        // Fallback to gethostname if /proc file is not available
        char buffer[256];
        if (gethostname(buffer, sizeof(buffer)) == 0)
        {
            hostname = string(buffer);
        }
        else
        {
            hostname = "unknown";
        }
    }

    return hostname;
}

// Get username using environment variable or getpwuid
string getUsername()
{
    // Try environment variable first
    const char *user = getenv("USER");
    if (user != nullptr)
    {
        return string(user);
    }

    // Fallback to getpwuid
    struct passwd *pw = getpwuid(getuid());
    if (pw != nullptr && pw->pw_name != nullptr)
    {
        return string(pw->pw_name);
    }

    return "unknown";
}

// Parse /proc/stat for CPU statistics
CPUStats getCurrentCPUStats()
{
    CPUStats stats = {0};
    std::ifstream file("/proc/stat");
    std::string line;

    while (std::getline(file, line))
    {
        if (line.find("cpu ") != std::string::npos)
        {
            std::istringstream iss(line);
            std::string cpu_label;

            iss >> cpu_label >>
                stats.user >> stats.nice >> stats.system >> stats.idle >>
                stats.iowait >> stats.irq >> stats.softirq >> stats.steal >>
                stats.guest >> stats.guestNice;

            file.close();
            return stats;
        }
    }

    file.close();
    return stats;
}

// Calculate CPU usage percentage between two stat readings
float calculateCPUUsage(CPUStats prev, CPUStats curr)
{
    // Calculate total time for both readings
    long long int prevTotal = prev.user + prev.nice + prev.system + prev.idle +
                              prev.iowait + prev.irq + prev.softirq + prev.steal;
    long long int currTotal = curr.user + curr.nice + curr.system + curr.idle +
                              curr.iowait + curr.irq + curr.softirq + curr.steal;

    // Calculate idle time for both readings
    long long int prevIdle = prev.idle + prev.iowait;
    long long int currIdle = curr.idle + curr.iowait;

    // Calculate differences
    long long int totalDiff = currTotal - prevTotal;
    long long int idleDiff = currIdle - prevIdle;

    // Avoid division by zero
    if (totalDiff == 0) return 0.0f;

    // Calculate CPU usage percentage
    float usage = ((float)(totalDiff - idleDiff) / totalDiff) * 100.0f;

    // Ensure the result is within valid range
    if (usage < 0.0f) usage = 0.0f;
    if (usage > 100.0f) usage = 100.0f;
    return usage;
}

// Parse /proc/stat for process counts
map<string, int> getProcessCounts()
{
    map<string, int> counts;
    ifstream file("/proc/stat");
    string line;
    
    // Initialize counts
    counts["total"] = 0;
    counts["running"] = 0;
    counts["blocked"] = 0;
    counts["sleeping"] = 0;
    counts["zombie"] = 0;
    counts["stopped"] = 0;
    
    if (file.is_open()) {
        while (getline(file, line)) {
            if (line.substr(0, 9) == "processes") {
                istringstream iss(line);
                string label;
                int total;
                iss >> label >> total;
                counts["total"] = total;
            } else if (line.substr(0, 13) == "procs_running") {
                istringstream iss(line);
                string label;
                int running;
                iss >> label >> running;
                counts["running"] = running;
            } else if (line.substr(0, 13) == "procs_blocked") {
                istringstream iss(line);
                string label;
                int blocked;
                iss >> label >> blocked;
                counts["blocked"] = blocked;
            }
        }
        file.close();
    } else {
        cerr << "Error: Unable to open /proc/stat" << endl;
        return counts;
    }
    
    // Get more detailed process states by reading /proc/*/stat files
    DIR* proc_dir = opendir("/proc");
    if (proc_dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(proc_dir)) != nullptr) {
            // Check if directory name is a number (PID)
            string dirname = entry->d_name;
            if (dirname.find_first_not_of("0123456789") == string::npos && !dirname.empty()) {
                string stat_path = "/proc/" + dirname + "/stat";
                ifstream stat_file(stat_path);
                
                if (stat_file.is_open()) {
                    string stat_line;
                    if (getline(stat_file, stat_line)) {
                        istringstream iss(stat_line);
                        string token;
                        
                        // Skip PID and command name (can contain spaces and parentheses)
                        iss >> token; // PID
                        iss >> token; // Command name in parentheses
                        
                        char state;
                        iss >> state; // Process state
                        
                        switch (state) {
                            case 'S': // Sleeping
                            case 'D': // Uninterruptible sleep
                                counts["sleeping"]++;
                                break;
                            case 'Z': // Zombie
                                counts["zombie"]++;
                                break;
                            case 'T': // Stopped
                            case 't': // Tracing stop
                                counts["stopped"]++;
                                break;
                        }
                    }
                    stat_file.close();
                }
            }
        }
        closedir(proc_dir);
    }
    
    return counts;
}

// Aggregate all system information
SystemInfo getSystemInfo()
{
    SystemInfo info;
    
    // Get basic system information
    info.os_name = getOsName();
    info.hostname = getHostname();
    info.username = getUsername();
    info.cpu_model = CPUinfo();
    
    // Get process counts
    map<string, int> processCounts = getProcessCounts();
    info.total_processes = processCounts["total"];
    info.running_processes = processCounts["running"];
    info.sleeping_processes = processCounts["sleeping"];
    info.zombie_processes = processCounts["zombie"];
    info.stopped_processes = processCounts["stopped"];
    
    return info;
}
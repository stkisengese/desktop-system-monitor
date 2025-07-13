/**
 * @file system.cpp
 * This application provides real-time monitoring of system resources including:
 * - CPU usage percentage with historical graphing
 * - Thermal temperature monitoring with alerts
 * - Fan speed and PWM level monitoring
 * - Process counts and system information
 *
 * The application uses ImGui for the graphical interface and reads system information
 * from /proc filesystem and /sys/class hardware monitoring interfaces.
 *
 * @author Stephen Kisengese
 * @version 1.0
 * @date 2025
 */

#include "header.h"

/* ========================================================================
 * GLOBAL VARIABLES AND CONFIGURATION
 * ======================================================================== */

// Global variables for CPU graph monitoring
vector<float> cpu_history;             ///< Historical CPU usage data (max 100 points)
bool graph_paused = false;             ///< Global pause state for CPU graph updates
float graph_fps = 10.0f;               ///< Graph update frequency (1-30 FPS)
float graph_scale = 100.0f;            ///< Y-axis scale for CPU graph (100% or 200%)
mutex cpu_mutex;                       ///< Mutex for thread-safe CPU data access
atomic<float> current_cpu_usage(0.0f); ///< Current CPU usage percentage

// Global variables for thermal monitoring
vector<float> thermal_history;           ///< Historical temperature data (max 100 points)
bool thermal_paused = false;             ///< Global pause state for thermal graph updates
float thermal_fps = 10.0f;               ///< Thermal update frequency (1-30 FPS)
float thermal_scale = 100.0f;            ///< Y-axis scale for thermal graph (°C)
mutex thermal_mutex;                     ///< Mutex for thread-safe thermal data access
atomic<float> current_temperature(0.0f); ///< Current temperature in Celsius
atomic<bool> thermal_available(false);   ///< Whether thermal sensors are available

// Global variables for fan monitoring
vector<int> fan_speed_history;     ///< Historical fan speed data (max 100 points)
bool fan_paused = false;           ///< Global pause state for fan graph updates
float fan_fps = 10.0f;             ///< Fan update frequency (1-30 FPS)
float fan_scale = 5000.0f;         ///< Y-axis scale for fan graph (RPM)
mutex fan_mutex;                   ///< Mutex for thread-safe fan data access
atomic<int> current_fan_speed(0);  ///< Current fan speed in RPM
atomic<int> current_fan_level(0);  ///< Current fan PWM level (0-255)
atomic<bool> fan_active(false);    ///< Whether fan is currently active
atomic<bool> fan_available(false); ///< Whether fan sensors are available

/* ========================================================================
 * SYSTEM INFORMATION FUNCTIONS
 * ======================================================================== */

/**
 * @brief Retrieves CPU brand information using CPUID instruction
 *
 * Uses the CPUID instruction to query the processor for brand string information.
 * This function works on x86/x86_64 processors and extracts the full CPU model name.
 *
 * @return std::string containing the CPU brand string (e.g., "Intel(R) Core(TM) i7-8700K")
 *
 * @note This function uses inline assembly and is platform-specific to x86/x86_64
 * @note For Windows systems, __cpuid() intrinsic should be used instead
 */
string CPUinfo()
{
    char CPUBrandString[0x40];
    unsigned int CPUInfo[4] = {0, 0, 0, 0};

    // Query maximum extended function number
    // For Unix systems using inline assembly
    // For Windows, use: __cpuid(regs, 0x80000000); where regs is array of 4 positions
    __cpuid(0x80000000, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);
    unsigned int nExIds = CPUInfo[0];

    // Initialize brand string buffer
    memset(CPUBrandString, 0, sizeof(CPUBrandString));

    // Extract brand string from extended CPUID functions 0x80000002-0x80000004
    for (unsigned int i = 0x80000000; i <= nExIds; ++i)
    {
        __cpuid(i, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);

        // Brand string is stored across three 16-byte chunks
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

/**
 * @brief Determines the operating system at compile time
 *
 * Uses preprocessor macros to identify the target operating system.
 * This is determined at compile time and returns a string constant.
 *
 * @return const char* pointing to OS name string
 *
 * @note The detection order matters - more specific macros should be checked first
 */
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

/**
 * @brief Retrieves the system hostname
 *
 * Attempts to read hostname from /proc/sys/kernel/hostname first (Linux-specific),
 * then falls back to the POSIX gethostname() function if that fails.
 *
 * @return std::string containing the hostname, or "unknown" if unable to determine
 *
 * @note Automatically trims whitespace and newlines from the hostname
 */
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
        // Fallback to POSIX gethostname if /proc file is not available
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

/**
 * @brief Retrieves the current username
 *
 * First attempts to get username from USER environment variable,
 * then falls back to getpwuid() system call with current user ID.
 *
 * @return std::string containing the username, or "unknown" if unable to determine
 *
 * @note Environment variable method is faster but may not always be available
 */
string getUsername()
{
    // Try environment variable first (faster)
    const char *user = getenv("USER");
    if (user != nullptr)
    {
        return string(user);
    }

    // Fallback to password database lookup
    struct passwd *pw = getpwuid(getuid());
    if (pw != nullptr && pw->pw_name != nullptr)
    {
        return string(pw->pw_name);
    }

    return "unknown";
}

/* ========================================================================
 * CPU MONITORING FUNCTIONS
 * ======================================================================== */

/**
 * @brief Parses /proc/stat to get current CPU statistics
 *
 * Reads the first line of /proc/stat which contains aggregate CPU time statistics.
 * The format is: cpu user nice system idle iowait irq softirq steal guest guest_nice
 *
 * @return CPUStats structure containing all CPU time counters
 *
 * @note All values are in jiffies (typically 1/100th of a second)
 * @note Returns zeroed structure if /proc/stat cannot be read
 */
CPUStats getCurrentCPUStats()
{
    CPUStats stats = {0};
    std::ifstream file("/proc/stat");
    std::string line;

    while (std::getline(file, line))
    {
        // Look for the aggregate CPU line (starts with "cpu ")
        if (line.find("cpu ") != std::string::npos)
        {
            std::istringstream iss(line);
            std::string cpu_label;

            // Parse all CPU time fields
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

/**
 * @brief Calculates CPU usage percentage between two stat readings
 *
 * Computes the percentage of CPU time spent in non-idle states between
 * two snapshots of CPU statistics. Uses the standard formula:
 * CPU% = (total_diff - idle_diff) / total_diff * 100
 *
 * @param prev Previous CPU statistics snapshot
 * @param curr Current CPU statistics snapshot
 * @return float CPU usage percentage (0.0 to 100.0)
 *
 * @note Returns 0.0% if no time has elapsed between readings
 * @note Result is clamped to valid range [0.0, 100.0]
 */
float calculateCPUUsage(CPUStats prev, CPUStats curr)
{
    // Calculate total time for both readings (sum of all CPU time categories)
    long long int prevTotal = prev.user + prev.nice + prev.system + prev.idle +
                              prev.iowait + prev.irq + prev.softirq + prev.steal;
    long long int currTotal = curr.user + curr.nice + curr.system + curr.idle +
                              curr.iowait + curr.irq + curr.softirq + curr.steal;

    // Calculate idle time for both readings (idle + iowait)
    long long int prevIdle = prev.idle + prev.iowait;
    long long int currIdle = curr.idle + curr.iowait;

    // Calculate time differences
    long long int totalDiff = currTotal - prevTotal;
    long long int idleDiff = currIdle - prevIdle;

    // Avoid division by zero
    if (totalDiff == 0)
        return 0.0f;

    // Calculate CPU usage percentage
    float usage = ((float)(totalDiff - idleDiff) / totalDiff) * 100.0f;

    // Ensure the result is within valid range
    if (usage < 0.0f)
        usage = 0.0f;
    if (usage > 100.0f)
        usage = 100.0f;
    return usage;
}

/**
 * @brief Retrieves current process counts by state
 *
 * Combines information from /proc/stat (running/blocked counts) with
 * individual process state information from /proc//stat files.
 *
 * @return std::map<std::string, int> containing process counts by state:
 *         - "total": Total number of processes
 *         - "running": Currently running processes
 *         - "blocked": Blocked processes (uninterruptible sleep)
 *         - "sleeping": Sleeping processes (interruptible sleep)
 *         - "zombie": Zombie processes
 *         - "stopped": Stopped processes
 *
 * @note This function may be slow with many processes due to individual stat file reads
 */
map<string, int> getProcessCounts()
{
    map<string, int> counts;

    // Initialize all counters
    counts["total"] = 0;
    counts["running"] = 0;
    counts["blocked"] = 0;
    counts["sleeping"] = 0;
    counts["zombie"] = 0;
    counts["stopped"] = 0;

    // Get running and blocked counts from /proc/stat (more efficient)
    ifstream file("/proc/stat");
    string line;

    if (file.is_open())
    {
        while (getline(file, line))
        {
            if (line.substr(0, 13) == "procs_running")
            {
                istringstream iss(line);
                string label;
                int running;
                iss >> label >> running;
                counts["running"] = running;
            }
            else if (line.substr(0, 13) == "procs_blocked")
            {
                istringstream iss(line);
                string label;
                int blocked;
                iss >> label >> blocked;
                counts["blocked"] = blocked;
            }
        }
        file.close();
    }
    else
    {
        cerr << "Error: Unable to open /proc/stat" << endl;
        return counts;
    }

    // Count all processes by examining individual /proc/*/stat files
    DIR *proc_dir = opendir("/proc");
    if (proc_dir != nullptr)
    {
        struct dirent *entry;
        while ((entry = readdir(proc_dir)) != nullptr)
        {
            // Check if directory name is a number (PID)
            string dirname = entry->d_name;
            if (dirname.find_first_not_of("0123456789") == string::npos && !dirname.empty())
            {
                string stat_path = "/proc/" + dirname + "/stat";
                ifstream stat_file(stat_path);

                if (stat_file.is_open())
                {
                    string stat_line;
                    if (getline(stat_file, stat_line))
                    {
                        istringstream iss(stat_line);
                        string token;

                        // Skip PID and command name (can contain spaces and parentheses)
                        iss >> token; // PID
                        iss >> token; // Command name in parentheses

                        char state;
                        iss >> state; // Process state (3rd field)

                        counts["total"]++; // Count every process we can read

                        // Categorize by process state
                        switch (state)
                        {
                        case 'S': // Interruptible sleep
                        case 'D': // Uninterruptible sleep (blocked)
                            counts["sleeping"]++;
                            break;
                        case 'Z': // Zombie
                            counts["zombie"]++;
                            break;
                        case 'T': // Stopped (signal)
                        case 't': // Tracing stop
                            counts["stopped"]++;
                            break;
                        case 'R': // Running
                            // Already counted from /proc/stat
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

/**
 * @brief Aggregates all system information into a single structure
 *
 * Collects comprehensive system information including OS details,
 * hardware information, and current process statistics.
 *
 * @return SystemInfo structure containing all collected system data
 *
 * @note This function calls multiple system information gathering functions
 * @note Process counting may take some time with many running processes
 */
SystemInfo getSystemInfo()
{
    SystemInfo info;

    // Get basic system identification
    info.os_name = getOsName();
    info.hostname = getHostname();
    info.username = getUsername();
    info.cpu_model = CPUinfo();

    // Get current process statistics
    map<string, int> processCounts = getProcessCounts();
    info.total_processes = processCounts["total"];
    info.running_processes = processCounts["running"];
    info.sleeping_processes = processCounts["sleeping"];
    info.zombie_processes = processCounts["zombie"];
    info.stopped_processes = processCounts["stopped"];

    return info;
}

/**
 * @brief Updates CPU usage history data
 *
 * Called periodically to update the CPU usage history buffer.
 * Calculates current CPU usage and adds it to the history if not paused.
 * Maintains a rolling buffer of the last 100 data points.
 *
 * @note Thread-safe using cpu_mutex
 * @note Skips the first reading to establish baseline
 * @note History is not updated when graph_paused is true
 */
void updateCPUHistory()
{
    static CPUStats prev_stats;
    static bool first_run = true;

    CPUStats curr_stats = getCurrentCPUStats();

    if (!first_run)
    {
        // Calculate usage and update atomic variable
        float usage = calculateCPUUsage(prev_stats, curr_stats);
        current_cpu_usage.store(usage);

        // Add to history if not paused
        if (!graph_paused)
        {
            lock_guard<mutex> lock(cpu_mutex);
            cpu_history.push_back(usage);

            // Maintain rolling buffer of last 100 points
            if (cpu_history.size() > 100)
            {
                cpu_history.erase(cpu_history.begin());
            }
        }
    }
    else
    {
        first_run = false;
    }

    prev_stats = curr_stats;
}

/**
 * @brief Renders the CPU performance monitoring interface
 *
 * Creates a complete ImGui interface for CPU monitoring including:
 * - Control buttons (pause/resume)
 * - FPS and scale adjustment sliders
 * - Real-time CPU usage display
 * - Historical usage graph with overlay
 * - Graph statistics and status
 *
 * @note Uses thread-safe access to shared CPU data
 * @note Graph overlay shows current CPU percentage
 * @note All UI elements are properly laid out using ImGui columns
 */
void renderCPUGraph()
{
    ImGui::Text("CPU Performance Monitor");
    ImGui::Separator();

    // Control panel with 3 columns
    ImGui::Columns(3, "cpu_controls", false);

    // Column 1: Pause/Resume button
    if (ImGui::Button(graph_paused ? "Resume##cpu" : "Pause##cpu", ImVec2(80, 0)))
    {
        graph_paused = !graph_paused;
    }

    ImGui::NextColumn();

    // Column 2: FPS control slider
    ImGui::Text("FPS:");
    ImGui::SetNextItemWidth(300);
    ImGui::SliderFloat("##cpu_fps", &graph_fps, 1.0f, 30.0f, "%.0f");

    ImGui::NextColumn();

    // Column 3: Y-axis scale control slider
    ImGui::Text("Y-Scale:");
    ImGui::SetNextItemWidth(300);
    ImGui::SliderFloat("##cpu_scale", &graph_scale, 60.0f, 200.0f, "%.0f%%");

    ImGui::Columns(1);
    ImGui::Spacing();

    // Display current CPU usage
    float cpu_percent = current_cpu_usage.load();
    ImGui::Text("Current CPU Usage: %.1f%%", cpu_percent);

    // Render graph if data is available
    if (!cpu_history.empty())
    {
        lock_guard<mutex> lock(cpu_mutex);

        // Calculate canvas dimensions
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        canvas_size.y = min(canvas_size.y, 200.0f); // Limit height to 200px

        // Create a copy of the data for plotting to avoid holding the lock too long
        vector<float> plot_data = cpu_history;

        // Release lock before plotting
        cpu_mutex.unlock();

        // Plot the line graph
        ImGui::PlotLines("##cpu_graph",
                         plot_data.data(),
                         plot_data.size(),
                         0,           // values_offset
                         nullptr,     // overlay_text
                         0.0f,        // scale_min
                         graph_scale, // scale_max
                         canvas_size);

        // Add custom overlay text with background
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        ImVec2 text_pos = ImVec2(canvas_pos.x + 10, canvas_pos.y + 10);

        // Semi-transparent background for overlay text
        ImVec2 text_size = ImGui::CalcTextSize("CPU: 100.0%");
        draw_list->AddRectFilled(
            ImVec2(text_pos.x - 5, text_pos.y - 2),
            ImVec2(text_pos.x + text_size.x + 5, text_pos.y + text_size.y + 2),
            IM_COL32(0, 0, 0, 128));

        // White overlay text
        char overlay_text[32];
        snprintf(overlay_text, sizeof(overlay_text), "CPU: %.1f%%", cpu_percent);
        draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), overlay_text);

        // Re-acquire lock for any remaining operations
        cpu_mutex.lock();
    }
    else
    {
        ImGui::Text("Collecting CPU data...");
    }

    // Display graph statistics
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Graph Info:");
    ImGui::Text("Data Points: %zu/100", cpu_history.size());
    ImGui::Text("Status: %s", graph_paused ? "Paused" : "Running");
    ImGui::Text("Update Rate: %.0f FPS", graph_fps);
}

/* ========================================================================
 * THERMAL MONITORING FUNCTIONS
 * ======================================================================== */

/**
 * @brief Retrieves thermal sensor information from system
 *
 * Attempts to read temperature from various thermal sensor paths in order:
 * 1. /sys/class/thermal/thermal_zone* - Generic thermal zones
 * 2. /sys/class/hwmon/hwmon/temp_input - Hardware monitoring sensors
 *
 * @return ThermalInfo structure containing temperature and availability status
 *
 * @note Temperature is converted from millicelsius to celsius
 * @note Returns first successfully read sensor value
 * @note Sets available=false if no sensors can be read
 */
ThermalInfo getThermalInfo()
{
    ThermalInfo info;
    info.available = false;
    info.temperature = 0.0f;

    // Try different thermal sensor paths in order of preference
    vector<string> thermal_paths = {
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/thermal/thermal_zone1/temp",
        "/sys/class/hwmon/hwmon0/temp1_input",
        "/sys/class/hwmon/hwmon1/temp1_input",
        "/sys/class/hwmon/hwmon2/temp1_input"};

    for (const string &path : thermal_paths)
    {
        ifstream file(path);
        if (file.is_open())
        {
            string temp_str;
            if (getline(file, temp_str))
            {
                try
                {
                    long temp_raw = stol(temp_str);
                    // Convert from millicelsius to celsius
                    info.temperature = temp_raw / 1000.0f;
                    info.available = true;
                    file.close();
                    return info;
                }
                catch (const exception &e)
                {
                    // Continue to next path if parsing fails
                }
            }
            file.close();
        }
    }

    return info;
}

/**
 * @brief Updates thermal history data
 *
 * Called periodically to update the thermal history buffer.
 * Reads current temperature and adds it to history if not paused.
 * Maintains a rolling buffer of the last 100 data points.
 *
 * @note Thread-safe using thermal_mutex
 * @note History is not updated when thermal_paused is true
 * @note Updates thermal_available atomic flag
 */
void updateThermalHistory()
{
    ThermalInfo thermal_info = getThermalInfo();
    thermal_available.store(thermal_info.available);

    if (thermal_info.available)
    {
        current_temperature.store(thermal_info.temperature);

        // Add to history if not paused
        if (!thermal_paused)
        {
            lock_guard<mutex> lock(thermal_mutex);
            thermal_history.push_back(thermal_info.temperature);

            // Maintain rolling buffer of last 100 points
            if (thermal_history.size() > 100)
            {
                thermal_history.erase(thermal_history.begin());
            }
        }
    }
}

/**
 * @brief Renders the thermal monitoring interface
 *
 * Creates a complete ImGui interface for thermal monitoring including:
 * - Sensor availability check and warning
 * - Control buttons (pause/resume)
 * - FPS and scale adjustment sliders
 * - Real-time temperature display (Celsius and Fahrenheit)
 * - Temperature status warnings (Normal/Caution/Warning)
 * - Historical temperature graph with overlay
 * - Graph statistics and status
 *
 * @note Shows warning message if no thermal sensors are detected
 * @note Temperature warnings: >80°C = Warning, >70°C = Caution, else Normal
 * @note Uses thread-safe access to shared thermal data
 */
void renderThermalGraph()
{
    ImGui::Text("Thermal Monitor");
    ImGui::Separator();

    // Check if thermal sensors are available
    if (!thermal_available.load())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No thermal sensors detected");
        ImGui::Text("Thermal monitoring is not available on this system.");
        return;
    }

    // Control panel with 3 columns
    ImGui::Columns(3, "thermal_controls", false);

    // Column 1: Pause/Resume button
    if (ImGui::Button(thermal_paused ? "Resume##thermal" : "Pause##thermal", ImVec2(80, 0)))
    {
        thermal_paused = !thermal_paused;
    }

    ImGui::NextColumn();

    // Column 2: FPS control slider
    ImGui::Text("FPS:");
    ImGui::SetNextItemWidth(300);
    ImGui::SliderFloat("##thermal_fps", &thermal_fps, 1.0f, 30.0f, "%.0f");

    ImGui::NextColumn();

    // Column 3: Y-axis scale control slider
    ImGui::Text("Y-Scale:");
    ImGui::SetNextItemWidth(300);
    ImGui::SliderFloat("##thermal_scale", &thermal_scale, 60.0f, 120.0f, "%.0f°C");

    ImGui::Columns(1);
    ImGui::Spacing();

    // Display current temperature in both Celsius and Fahrenheit
    float temp = current_temperature.load();
    ImGui::Text("Current Temperature: %.1f°C (%.1f°F)", temp, (temp * 9.0f / 5.0f) + 32.0f);

    // Temperature status indication with color coding
    if (temp > 80.0f)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "WARNING: High Temperature!");
    }
    else if (temp > 70.0f)
    {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "CAUTION: Elevated Temperature");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Temperature Normal");
    }

    // Render graph if data is available
    if (!thermal_history.empty())
    {
        lock_guard<mutex> lock(thermal_mutex);

        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        canvas_size.y = min(canvas_size.y, 200.0f);

        // Create copy of data for plotting
        vector<float> plot_data = thermal_history;
        thermal_mutex.unlock();

        // Plot the line graph
        ImGui::PlotLines("##thermal_graph",
                         plot_data.data(),
                         plot_data.size(),
                         0, nullptr, 0.0f, thermal_scale, canvas_size);

        // Add custom overlay text with background
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        ImVec2 text_pos = ImVec2(canvas_pos.x + 10, canvas_pos.y + 10);

        char overlay_text[32];
        snprintf(overlay_text, sizeof(overlay_text), "%.1f°C", temp);
        ImVec2 text_size = ImGui::CalcTextSize(overlay_text);

        // Semi-transparent background
        draw_list->AddRectFilled(
            ImVec2(text_pos.x - 5, text_pos.y - 2),
            ImVec2(text_pos.x + text_size.x + 5, text_pos.y + text_size.y + 2),
            IM_COL32(0, 0, 0, 128));

        // White overlay text
        draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), overlay_text);

        thermal_mutex.lock();
    }
    else
    {
        ImGui::Text("Collecting thermal data...");
    }

    // Display graph statistics
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Graph Info:");
    ImGui::Text("Data Points: %zu/100", thermal_history.size());
    ImGui::Text("Status: %s", thermal_paused ? "Paused" : "Running");
    ImGui::Text("Update Rate: %.0f FPS", thermal_fps);
}

/* ========================================================================
 * FAN MONITORING FUNCTIONS
 * ======================================================================== */

/**
 * @brief Retrieves fan sensor information from system hardware monitoring
 *
 * Searches through /sys/class/hwmon/ directories for fan sensors.
 * Attempts to read fan speed, PWM level, and enable status from hardware
 * monitoring interface files.
 *
 * @return FanInfo structure containing:
 *         - available: Whether fan sensors were found
 *         - speed: Fan speed in RPM
 *         - level: PWM level (0-255)
 *         - active: Whether fan is currently enabled/active
 *
 * @note Searches for fan1_input through fan4_input files
 * @note PWM level represents duty cycle (0-255 range)
 * @note If no enable file found, assumes active when speed > 0
 * @note Returns first successfully read fan sensor
 */
FanInfo getFanInfo()
{
    FanInfo info;
    info.available = false;
    info.speed = 0;
    info.level = 0;
    info.active = false;

    // Search for fan sensors in hwmon directories
    try
    {
        for (const auto &entry : filesystem::directory_iterator("/sys/class/hwmon/"))
        {
            if (entry.is_directory())
            {
                string hwmon_path = entry.path().string();

                // Check for fan speed (fan1_input, fan2_input, etc.)
                for (int fan_num = 1; fan_num <= 4; fan_num++)
                {
                    string speed_path = hwmon_path + "/fan" + to_string(fan_num) + "_input";
                    ifstream speed_file(speed_path);

                    if (speed_file.is_open())
                    {
                        string speed_str;
                        if (getline(speed_file, speed_str))
                        {
                            try
                            {
                                info.speed = stoi(speed_str);
                                info.available = true;
                                speed_file.close();

                                // Check for fan enable status
                                string enable_path = hwmon_path + "/fan" + to_string(fan_num) + "_enable";
                                ifstream enable_file(enable_path);
                                if (enable_file.is_open())
                                {
                                    string enable_str;
                                    if (getline(enable_file, enable_str))
                                    {
                                        info.active = (stoi(enable_str) == 1);
                                    }
                                    enable_file.close();
                                }
                                else
                                {
                                    // If no enable file, assume active if speed > 0
                                    info.active = (info.speed > 0);
                                }

                                // Check for PWM level
                                string pwm_path = hwmon_path + "/pwm" + to_string(fan_num);
                                ifstream pwm_file(pwm_path);
                                if (pwm_file.is_open())
                                {
                                    string pwm_str;
                                    if (getline(pwm_file, pwm_str))
                                    {
                                        info.level = stoi(pwm_str);
                                    }
                                    pwm_file.close();
                                }

                                return info;
                            }
                            catch (const exception &e)
                            {
                                // Continue to next fan
                            }
                        }
                        speed_file.close();
                    }
                }
            }
        }
    }
    catch (const exception &e)
    {
        // Directory doesn't exist or can't be accessed
    }

    return info;
}

/**
 * @brief Updates fan monitoring history data
 *
 * Called periodically to update the fan speed history buffer.
 * Reads current fan information and adds speed data to history if not paused.
 * Maintains a rolling buffer of the last 100 data points.
 *
 * @note Thread-safe using fan_mutex
 * @note History is not updated when fan_paused is true
 * @note Updates atomic variables for current fan status
 * @note Updates fan_available, current_fan_speed, current_fan_level, fan_active
 */
void updateFanHistory()
{
    FanInfo fan_info = getFanInfo();
    fan_available.store(fan_info.available);

    if (fan_info.available)
    {
        current_fan_speed.store(fan_info.speed);
        current_fan_level.store(fan_info.level);
        fan_active.store(fan_info.active);

        if (!fan_paused)
        {
            lock_guard<mutex> lock(fan_mutex);
            fan_speed_history.push_back(fan_info.speed);

            // Keep only last 100 data points
            if (fan_speed_history.size() > 100)
            {
                fan_speed_history.erase(fan_speed_history.begin());
            }
        }
    }
}

/**
 * @brief Renders fan status information display
 *
 * Creates a status display showing current fan information including:
 * - Fan availability check with warning if not detected
 * - Fan active/inactive status with color coding
 * - Current fan speed in RPM
 * - PWM level (0-255) and percentage
 * - Speed classification (High/Medium/Low/Stopped)
 *
 * @note Shows warning message if no fan sensors are detected
 * @note Speed classifications: >4000 RPM = High, >2500 RPM = Medium, >0 RPM = Low, 0 RPM = Stopped
 * @note Uses color coding for different status levels
 * @note All information displayed on single line for compact layout
 */
void renderFanStatus()
{
    if (!fan_available.load())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No fan sensors detected");
        ImGui::Text("Fan monitoring is not available on this system.");
        return;
    }

    ImGui::Text("Fan Status Information");
    ImGui::Separator();

    // Fan status, speed, and PWM level on a single line
    bool is_active = fan_active.load();
    int speed = current_fan_speed.load();
    int level = current_fan_level.load();
    float level_percent = (level / 255.0f) * 100.0f;

    ImGui::Text("Status: ");
    ImGui::SameLine();
    if (is_active)
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Inactive");
    }

    ImGui::SameLine();
    ImGui::Text("  |  Speed: %d RPM", speed);

    ImGui::SameLine();
    ImGui::Text("  |  PWM: %d (%.1f%%)", level, level_percent);

    // Speed indicator
    if (speed > 4000)
    {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "High Speed");
    }
    else if (speed > 2500)
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Medium Speed");
    }
    else if (speed > 0)
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Low Speed");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Stopped");
    }
}

/**
 * @brief Renders the complete fan monitoring interface
 *
 * Creates a comprehensive ImGui interface for fan monitoring including:
 * - Fan status display (calls renderFanStatus)
 * - Control buttons (pause/resume)
 * - FPS and scale adjustment sliders
 * - Real-time fan speed graph with overlay
 * - Graph statistics and status information
 *
 * @note Returns early if no fan sensors are available
 * @note Graph shows RPM values over time with customizable scale
 * @note Uses thread-safe access to shared fan data
 **/
void renderFanGraph()
{
    ImGui::Text("Fan Speed Monitor");
    ImGui::Separator();

    // Display fan status first
    renderFanStatus();
    if (!fan_available.load())
    {
        return;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Fan Speed Graph");
    ImGui::Columns(3, "fan_controls", false); // Control buttons and sliders

    // Pause/Resume button
    if (ImGui::Button(fan_paused ? "Resume##fan" : "Pause##fan", ImVec2(80, 0)))
    {
        fan_paused = !fan_paused;
    }

    ImGui::NextColumn();

    // FPS control slider
    ImGui::Text("FPS:");
    ImGui::SetNextItemWidth(300);
    ImGui::SliderFloat("##fan_fps", &fan_fps, 1.0f, 30.0f, "%.0f");

    ImGui::NextColumn();

    // Y-axis scale control slider
    ImGui::Text("Y-Scale:");
    ImGui::SetNextItemWidth(300);
    ImGui::SliderFloat("##fan_scale", &fan_scale, 2000.0f, 8000.0f, "%.0f RPM");

    ImGui::Columns(1);
    ImGui::Spacing();

    // Graph plotting
    if (!fan_speed_history.empty())
    {
        lock_guard<mutex> lock(fan_mutex);

        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        canvas_size.y = max(min(canvas_size.y, 200.0f), 150.0f);
        // canvas_size.y = min(canvas_size.y, 200.0f);

        // Convert int vector to float for plotting
        vector<float> plot_data;
        plot_data.reserve(fan_speed_history.size());
        for (int speed : fan_speed_history)
        {
            plot_data.push_back(static_cast<float>(speed));
        }

        fan_mutex.unlock();

        // Plot the graph
        ImGui::PlotLines("##fan_graph",
                         plot_data.data(),
                         plot_data.size(),
                         0, nullptr, 0.0f, fan_scale, canvas_size);

        // Add overlay text on the graph
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        ImVec2 text_pos = ImVec2(canvas_pos.x + 10, canvas_pos.y + 10);

        char overlay_text[32];
        snprintf(overlay_text, sizeof(overlay_text), "%d RPM", current_fan_speed.load());
        ImVec2 text_size = ImGui::CalcTextSize(overlay_text);

        draw_list->AddRectFilled(
            ImVec2(text_pos.x - 5, text_pos.y - 2),
            ImVec2(text_pos.x + text_size.x + 5, text_pos.y + text_size.y + 2),
            IM_COL32(0, 0, 0, 128));
        // Draw the overlay text
        draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), overlay_text);

        fan_mutex.lock();
    }
    else
    {
        ImGui::Text("Collecting fan data...");
    }

    // Graph statistics
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Graph Info:");
    ImGui::Text("Data Points: %zu/100", fan_speed_history.size());
    ImGui::Text("Status: %s", fan_paused ? "Paused" : "Running");
    ImGui::Text("Update Rate: %.0f FPS", fan_fps);
}
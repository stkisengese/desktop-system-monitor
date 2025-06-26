#ifndef header_H
#define header_H

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <dirent.h>
#include <vector>
#include <iostream>
#include <cstring>
#include <string>
#include <cmath>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <atomic>
#include <mutex>
#include <pwd.h>   // For getpwuid
#include <fstream> // lib to read from file
// for the name of the computer and the logged in user
#include <unistd.h>
#include <limits.h>
// this is for us to get the cpu information
// mostly in unix system
// not sure if it will work in windows
#include <cpuid.h>
// this is for the memory usage and other memory visualization
// for linux gotta find a way for windows
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <ctime>       // for time and date
#include <sys/types.h> // ifconfig ip addresses
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <map>

using namespace std;

struct CPUStats
{
    long long int user;
    long long int nice;
    long long int system;
    long long int idle;
    long long int iowait;
    long long int irq;
    long long int softirq;
    long long int steal;
    long long int guest;
    long long int guestNice;
};

// processes `stat`
struct Proc
{
    int pid;
    string name;
    char state;
    long long int vsize;
    long long int rss;
    long long int utime;
    long long int stime;
};

struct IP4
{
    char *name;
    char addressBuffer[INET_ADDRSTRLEN];
};

struct Networks
{
    vector<IP4> ip4s;
};

struct TX
{
    int bytes;
    int packets;
    int errs;
    int drop;
    int fifo;
    int frame;
    int compressed;
    int multicast;
};

struct RX
{
    int bytes;
    int packets;
    int errs;
    int drop;
    int fifo;
    int colls;
    int carrier;
    int compressed;
};

struct SystemInfo
{
    string os_name;
    string hostname;
    string username;
    string cpu_model;
    int total_processes;
    int running_processes;
    int sleeping_processes;
    int zombie_processes;
    int stopped_processes;
};

struct MemoryInfo
{
    unsigned long total_ram;
    unsigned long available_ram;
    unsigned long used_ram;
    unsigned long total_swap;
    unsigned long used_swap;
    unsigned long total_disk;
    unsigned long used_disk;
};

struct ThermalInfo
{
    float temperature;
    bool available;
};

struct FanInfo
{
    int speed;
    int level;
    bool active;
    bool available;
};

// system information
string CPUinfo();
const char *getOsName();
string getHostname();
string getUsername();
SystemInfo getSystemInfo();
map<string, int> getProcessCounts();
CPUStats getCurrentCPUStats();
float calculateCPUUsage(CPUStats prev, CPUStats curr);

// CPU Graph Global Variables (extern declarations)
extern vector<float> cpu_history;
extern bool graph_paused;
extern float graph_fps;
extern float graph_scale;

// CPU Graph Functions
void updateCPUHistory();
void renderCPUGraph();

// Updated system window function signature
void systemWindow(const char *id, ImVec2 size, ImVec2 position);

#endif

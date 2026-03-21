#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

namespace memforge {

struct NetworkConnection {
    std::string protocol;    // "TCP" or "UDP"
    std::string localAddr;
    uint16_t localPort = 0;
    std::string remoteAddr;
    uint16_t remotePort = 0;
    std::string state;       // ESTABLISHED, LISTENING, etc.
    bool isNew = false;      // newly detected this refresh
    bool isClosing = false;  // was present last refresh but gone now
};

class PacketInspector {
public:
    PacketInspector() = default;
    ~PacketInspector();

    // Get all network connections for a process (one-shot)
    static std::vector<NetworkConnection> GetConnections(DWORD pid);

    // Monitor connections over time
    void StartMonitoring(DWORD pid);
    void StopMonitoring();

    const std::vector<NetworkConnection>& GetCurrentConnections() const { return m_connections; }
    bool IsMonitoring() const { return m_monitoring.load(); }

    // Get stats
    size_t GetTotalConnectionsSeen() const { return m_totalSeen; }

private:
    void MonitorLoop();

    static std::string TcpStateToString(DWORD state);
    static std::string IpToString(DWORD ip);

    std::vector<NetworkConnection> m_connections;
    std::vector<NetworkConnection> m_prevConnections;
    std::atomic<bool> m_monitoring{false};
    std::thread m_thread;
    mutable std::mutex m_mutex;
    DWORD m_pid = 0;
    size_t m_totalSeen = 0;
};

} // namespace memforge

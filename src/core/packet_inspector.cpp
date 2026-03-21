#include <WinSock2.h>
#include "core/packet_inspector.h"

// iphlpapi must come after windows/winsock headers
#include <iphlpapi.h>
#include <tcpmib.h>

#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>

#pragma comment(lib, "iphlpapi.lib")

namespace memforge {

PacketInspector::~PacketInspector() {
    StopMonitoring();
}

std::string PacketInspector::IpToString(DWORD ip) {
    std::ostringstream ss;
    ss << (ip & 0xFF) << "."
       << ((ip >> 8) & 0xFF) << "."
       << ((ip >> 16) & 0xFF) << "."
       << ((ip >> 24) & 0xFF);
    return ss.str();
}

std::string PacketInspector::TcpStateToString(DWORD state) {
    switch (state) {
        case MIB_TCP_STATE_CLOSED:      return "CLOSED";
        case MIB_TCP_STATE_LISTEN:      return "LISTENING";
        case MIB_TCP_STATE_SYN_SENT:    return "SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD:    return "SYN_RCVD";
        case MIB_TCP_STATE_ESTAB:       return "ESTABLISHED";
        case MIB_TCP_STATE_FIN_WAIT1:   return "FIN_WAIT1";
        case MIB_TCP_STATE_FIN_WAIT2:   return "FIN_WAIT2";
        case MIB_TCP_STATE_CLOSE_WAIT:  return "CLOSE_WAIT";
        case MIB_TCP_STATE_CLOSING:     return "CLOSING";
        case MIB_TCP_STATE_LAST_ACK:    return "LAST_ACK";
        case MIB_TCP_STATE_TIME_WAIT:   return "TIME_WAIT";
        case MIB_TCP_STATE_DELETE_TCB:   return "DELETE_TCB";
        default: return "UNKNOWN";
    }
}

std::vector<NetworkConnection> PacketInspector::GetConnections(DWORD pid) {
    std::vector<NetworkConnection> connections;

    // ── TCP connections ──
    {
        DWORD bufSize = 0;
        GetExtendedTcpTable(nullptr, &bufSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        if (bufSize > 0) {
            std::vector<uint8_t> buf(bufSize);
            if (GetExtendedTcpTable(buf.data(), &bufSize, FALSE, AF_INET,
                                     TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
                auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
                for (DWORD i = 0; i < table->dwNumEntries; i++) {
                    auto& row = table->table[i];
                    if (row.dwOwningPid == pid) {
                        NetworkConnection conn;
                        conn.protocol = "TCP";
                        conn.localAddr = IpToString(row.dwLocalAddr);
                        conn.localPort = static_cast<uint16_t>(ntohs(static_cast<uint16_t>(row.dwLocalPort)));
                        conn.remoteAddr = IpToString(row.dwRemoteAddr);
                        conn.remotePort = static_cast<uint16_t>(ntohs(static_cast<uint16_t>(row.dwRemotePort)));
                        conn.state = TcpStateToString(row.dwState);
                        connections.push_back(conn);
                    }
                }
            }
        }
    }

    // ── UDP connections ──
    {
        DWORD bufSize = 0;
        GetExtendedUdpTable(nullptr, &bufSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
        if (bufSize > 0) {
            std::vector<uint8_t> buf(bufSize);
            if (GetExtendedUdpTable(buf.data(), &bufSize, FALSE, AF_INET,
                                     UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
                auto* table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buf.data());
                for (DWORD i = 0; i < table->dwNumEntries; i++) {
                    auto& row = table->table[i];
                    if (row.dwOwningPid == pid) {
                        NetworkConnection conn;
                        conn.protocol = "UDP";
                        conn.localAddr = IpToString(row.dwLocalAddr);
                        conn.localPort = static_cast<uint16_t>(ntohs(static_cast<uint16_t>(row.dwLocalPort)));
                        conn.remoteAddr = "*";
                        conn.remotePort = 0;
                        conn.state = "OPEN";
                        connections.push_back(conn);
                    }
                }
            }
        }
    }

    return connections;
}

void PacketInspector::StartMonitoring(DWORD pid) {
    if (m_monitoring.load()) return;

    m_pid = pid;
    m_monitoring.store(true);
    m_totalSeen = 0;

    m_thread = std::thread(&PacketInspector::MonitorLoop, this);
}

void PacketInspector::StopMonitoring() {
    m_monitoring.store(false);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void PacketInspector::MonitorLoop() {
    while (m_monitoring.load()) {
        auto newConns = GetConnections(m_pid);

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            // Mark new connections
            for (auto& nc : newConns) {
                nc.isNew = true;
                nc.isClosing = false;
                for (auto& prev : m_prevConnections) {
                    if (prev.protocol == nc.protocol &&
                        prev.localAddr == nc.localAddr &&
                        prev.localPort == nc.localPort &&
                        prev.remoteAddr == nc.remoteAddr &&
                        prev.remotePort == nc.remotePort) {
                        nc.isNew = false;
                        break;
                    }
                }
                if (nc.isNew) m_totalSeen++;
            }

            // Mark closed connections
            for (auto& prev : m_prevConnections) {
                bool found = false;
                for (auto& nc : newConns) {
                    if (prev.protocol == nc.protocol &&
                        prev.localAddr == nc.localAddr &&
                        prev.localPort == nc.localPort &&
                        prev.remoteAddr == nc.remoteAddr &&
                        prev.remotePort == nc.remotePort) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // Add as closing entry
                    NetworkConnection closing = prev;
                    closing.isClosing = true;
                    closing.isNew = false;
                    newConns.push_back(closing);
                }
            }

            m_prevConnections = m_connections;
            m_connections = newConns;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace memforge

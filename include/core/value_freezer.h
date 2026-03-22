#pragma once
#include <Windows.h>
#include <cstdint>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include "core/memory_scanner.h"
#include "core/memory_writer.h"

// Issue 12: Use std::jthread for cooperative stop support
#include <stop_token>

namespace memforge {

struct FrozenValue {
    uintptr_t address;
    ScanValue value;
    ValueType type;
    std::string description;
    bool active;
    int id;
};

class ValueFreezer {
public:
    ValueFreezer();
    ~ValueFreezer();

    void Attach(HANDLE hProcess);
    void Detach();

    // Add a value to freeze
    int AddEntry(uintptr_t address, const ScanValue& value, ValueType type,
                  const std::string& description = "");

    // Remove a frozen value by id
    void RemoveEntry(int id);

    // Toggle freeze on/off for an entry
    void ToggleEntry(int id);

    // Update the frozen value
    void UpdateEntryValue(int id, const ScanValue& newValue);
    void UpdateEntryDescription(int id, const std::string& desc);

    // Start/stop the freezer thread
    void Start();
    void Stop();

    // Get all entries
    std::vector<FrozenValue>& GetEntries() { return m_entries; }
    const std::vector<FrozenValue>& GetEntries() const { return m_entries; }

    bool IsRunning() const { return m_running.load(); }

    // Freeze interval in milliseconds
    void SetInterval(int ms) { m_intervalMs = ms; }
    int GetInterval() const { return m_intervalMs; }

private:
    // Issue 12: jthread takes a stop_token for cooperative cancellation
    void FreezerLoop(std::stop_token st);

    HANDLE m_hProcess = nullptr;
    MemoryWriter m_writer;
    std::vector<FrozenValue> m_entries;
    std::mutex m_mutex;
    std::atomic<bool> m_running{false};
    // Issue 12: Use std::jthread instead of std::thread
    std::jthread m_thread;
    int m_nextId = 1;
    int m_intervalMs = 50; // 20 times per second
};

} // namespace memforge

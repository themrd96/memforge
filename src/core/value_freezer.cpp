#include "core/value_freezer.h"

namespace memforge {

ValueFreezer::ValueFreezer() {}

ValueFreezer::~ValueFreezer() {
    Stop();
}

void ValueFreezer::Attach(HANDLE hProcess) {
    m_hProcess = hProcess;
    m_writer.Attach(hProcess);
}

void ValueFreezer::Detach() {
    Stop();
    m_hProcess = nullptr;
    m_writer.Detach();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

int ValueFreezer::AddEntry(uintptr_t address, const ScanValue& value, ValueType type,
                            const std::string& description) {
    std::lock_guard<std::mutex> lock(m_mutex);
    FrozenValue entry;
    entry.address = address;
    entry.value = value;
    entry.type = type;
    entry.description = description;
    entry.active = true;
    entry.id = m_nextId++;
    m_entries.push_back(entry);
    return entry.id;
}

void ValueFreezer::RemoveEntry(int id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
                       [id](const FrozenValue& e) { return e.id == id; }),
        m_entries.end()
    );
}

void ValueFreezer::ToggleEntry(int id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& e : m_entries) {
        if (e.id == id) {
            e.active = !e.active;
            break;
        }
    }
}

void ValueFreezer::UpdateEntryValue(int id, const ScanValue& newValue) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& e : m_entries) {
        if (e.id == id) {
            e.value = newValue;
            break;
        }
    }
}

void ValueFreezer::UpdateEntryDescription(int id, const std::string& desc) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& e : m_entries) {
        if (e.id == id) {
            e.description = desc;
            break;
        }
    }
}

void ValueFreezer::Start() {
    if (m_running.load()) return;
    m_running.store(true);
    m_thread = std::thread(&ValueFreezer::FreezerLoop, this);
}

void ValueFreezer::Stop() {
    m_running.store(false);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void ValueFreezer::FreezerLoop() {
    while (m_running.load()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& entry : m_entries) {
                if (entry.active && m_hProcess) {
                    m_writer.WriteValue(entry.address, entry.value, entry.type);
                }
            }
        }
        Sleep(m_intervalMs);
    }
}

} // namespace memforge

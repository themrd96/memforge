#include "gui/app.h"
#include "core/packet_inspector.h"
#include <imgui.h>

namespace memforge {

void DrawNetwork(App& app) {
    ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Network Inspector", &app.showNetwork)) {
        ImGui::End();
        return;
    }

    bool monitoring = app.packetInspector.IsMonitoring();

    if (!monitoring) {
        if (ImGui::Button("Start Monitoring") && app.processAttached) {
            app.packetInspector.StartMonitoring(app.targetPid);
        }
    } else {
        if (ImGui::Button("Stop Monitoring")) {
            app.packetInspector.StopMonitoring();
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Refresh Once") && app.processAttached) {
        app.networkConnections = PacketInspector::GetConnections(app.targetPid);
    }
    ImGui::SameLine();
    ImGui::Text("Total connections seen: %zu", app.packetInspector.GetTotalConnectionsSeen());

    ImGui::Separator();

    // Use monitored connections if monitoring, otherwise use one-shot
    const auto& conns = monitoring ?
        app.packetInspector.GetCurrentConnections() : app.networkConnections;

    if (ImGui::BeginTable("NetConns", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_Sortable,
                          ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Protocol", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Local Address", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Local Port", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Remote Address", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Remote Port", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        int netRowId = 0;
        for (auto& conn : conns) {
            ImGui::TableNextRow();
            ImGui::PushID(netRowId++);

            // Color new/closing connections
            ImVec4 textColor(1.0f, 1.0f, 1.0f, 1.0f);
            if (conn.isNew) {
                textColor = ImVec4(0.2f, 1.0f, 0.3f, 1.0f);
            } else if (conn.isClosing) {
                textColor = ImVec4(1.0f, 0.3f, 0.2f, 0.7f);
            }

            // Build a copyable summary for the whole row
            char rowSummary[256];
            snprintf(rowSummary, sizeof(rowSummary), "%s %s:%u -> %s:%u [%s]",
                     conn.protocol.c_str(), conn.localAddr.c_str(), conn.localPort,
                     conn.remoteAddr.c_str(), conn.remotePort, conn.state.c_str());

            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(textColor, "%s", conn.protocol.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(textColor, "%s", conn.localAddr.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                ImGui::SetClipboardText(conn.localAddr.c_str());
            }

            ImGui::TableSetColumnIndex(2);
            char localPortStr[16];
            snprintf(localPortStr, sizeof(localPortStr), "%u", conn.localPort);
            ImGui::TextColored(textColor, "%s", localPortStr);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                ImGui::SetClipboardText(localPortStr);
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(textColor, "%s", conn.remoteAddr.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                ImGui::SetClipboardText(conn.remoteAddr.c_str());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Right-click to copy address");
            }

            ImGui::TableSetColumnIndex(4);
            char remotePortStr[16];
            snprintf(remotePortStr, sizeof(remotePortStr), "%u", conn.remotePort);
            ImGui::TextColored(textColor, "%s", remotePortStr);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                ImGui::SetClipboardText(remotePortStr);
            }

            ImGui::TableSetColumnIndex(5);
            ImGui::TextColored(textColor, "%s", conn.state.c_str());

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace memforge

#include "recording/RecordingPanel.h"

#ifdef _DEBUG

/// module
#include "recording/RecordingSystem.h"

/// externals
#include <imgui/imgui.h>

/// stl
#include <cstdio>
#include <string>
#include <vector>

namespace LogGuide {

namespace {

// "Default" + デバイス名一覧を BeginCombo で選ばせる。selected は -1(=Default) 起点のインデックス。
void DeviceCombo(const char* label, const std::vector<RecordingSystem::DeviceEntry>& devices, int& selected) {
    const char* preview = (selected < 0 || selected >= static_cast<int>(devices.size()))
                              ? "Default"
                              : devices[selected].name.c_str();
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable("Default", selected < 0)) {
            selected = -1;
        }
        for (int i = 0; i < static_cast<int>(devices.size()); ++i) {
            const bool isSelected = (selected == i);
            if (ImGui::Selectable(devices[i].name.c_str(), isSelected)) {
                selected = i;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void MonitorCombo(const char* label, const std::vector<std::string>& monitors, int& selected) {
    const char* preview = (selected >= 0 && selected < static_cast<int>(monitors.size()))
                              ? monitors[selected].c_str()
                              : "(none)";
    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
            const bool isSelected = (selected == i);
            if (ImGui::Selectable(monitors[i].c_str(), isSelected)) {
                selected = i;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

} // namespace

void DrawRecordingPanel(RecordingSystem& system) {
    if (!ImGui::Begin("Media Recorder")) {
        ImGui::End();
        return;
    }

    const bool recording = system.IsRecording();
    RecordingSettings& s = system.Settings();

    // --- ソース選択（録画中は編集不可）---
    ImGui::BeginDisabled(recording);

    ImGui::SeparatorText("Camera track (camera.mp4)");
    ImGui::Checkbox("Record camera track", &s.recordCameraTrack);
    if (s.recordCameraTrack) {
        DeviceCombo("Camera", system.Cameras(), s.cameraIndex);
        ImGui::Checkbox("Record microphone", &s.recordMic);
        if (s.recordMic) {
            DeviceCombo("Microphone", system.Microphones(), s.micIndex);
        }
        int wh[2] = {static_cast<int>(s.cameraWidth), static_cast<int>(s.cameraHeight)};
        if (ImGui::InputInt2("Camera WxH", wh)) {
            s.cameraWidth  = static_cast<uint32_t>(wh[0] < 0 ? 0 : wh[0]);
            s.cameraHeight = static_cast<uint32_t>(wh[1] < 0 ? 0 : wh[1]);
        }
    }

    ImGui::SeparatorText("Screen track (screen.mp4)");
    ImGui::Checkbox("Record screen track", &s.recordScreenTrack);
    if (s.recordScreenTrack) {
        MonitorCombo("Monitor", system.Monitors(), s.monitorIndex);
        ImGui::Checkbox("Record system audio (eRender loopback)", &s.recordSystemAudio);
    }

    ImGui::SeparatorText("Common");
    int fps = static_cast<int>(s.fps);
    if (ImGui::InputInt("FPS", &fps)) {
        s.fps = static_cast<uint32_t>(fps < 1 ? 1 : fps);
    }
    char outBuf[260];
    std::snprintf(outBuf, sizeof(outBuf), "%s", s.outputRoot.c_str());
    if (ImGui::InputText("Output root", outBuf, sizeof(outBuf))) {
        s.outputRoot = outBuf;
    }
    if (ImGui::Button("Refresh devices")) {
        system.RefreshDevices();
    }

    ImGui::EndDisabled();

    // --- 録画コントロール ---
    ImGui::SeparatorText("Control");
    if (!recording) {
        if (ImGui::Button("Start recording", ImVec2(160, 0))) {
            system.StartRecording();
        }
    } else {
        if (ImGui::Button("Stop recording", ImVec2(160, 0))) {
            system.StopRecording();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "REC  %.1fs", system.GetElapsedSeconds());
    }

    if (!system.GetLastError().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", system.GetLastError().c_str());
    }

    // --- 直近セッションの結果 ---
    if (system.HasLastSession()) {
        const SessionInfo& last = system.LastSession();
        ImGui::SeparatorText("Last session");
        ImGui::Text("ID: %s", last.sessionId.c_str());
        ImGui::Text("Duration: %.1fs", last.durationSeconds);
        ImGui::TextWrapped("Dir: %s", last.directory.c_str());
        for (const auto& t : last.tracks) {
            ImGui::BulletText("%s: %ux%u @%ufps%s", t.file.c_str(), t.width, t.height, t.fps,
                t.hasAudio ? " +audio" : "");
        }
    }

    ImGui::End();
}

} // namespace LogGuide

#else

namespace LogGuide {
class RecordingSystem;
void DrawRecordingPanel(RecordingSystem&) {}
} // namespace LogGuide

#endif // _DEBUG

#include "recording/RecordingPanel.h"

#ifdef _DEBUG

/// module
#include "analysis/AudioAnalysisPipeline.h"
#include "analysis/VideoAnalysisPipeline.h"
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

    // --- AI 解析の状態 ---
    if (const AudioAnalysisPipeline* analysis = system.Analysis()) {
        const AudioAnalysisPipeline::Status st = analysis->GetStatus();
        ImGui::SeparatorText("AI Analysis");
        if (st.finalizing) {
            // 録画停止後、要約・バッチ解析がバックグラウンドで進行中（UI はブロックしない）。
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                               "解析処理中... (要約生成 / 残チャンク解析)");
        } else if (recording) {
            const char* deg = st.degradeLevel == 0 ? "normal"
                            : st.degradeLevel == 1 ? "medium(GPU逼迫)"
                                                   : "解析停止(録音のみ)";
            ImGui::Text("録画解析中: chunks=%llu speech=%llu RTF=%.2f [%s]",
                        static_cast<unsigned long long>(st.chunksProcessed),
                        static_cast<unsigned long long>(st.speechEvents),
                        st.lastRtf, deg);
        } else {
            ImGui::Text("待機中 (transcribe=%d classify=%d summarize=%d)",
                        st.transcribeEnabled ? 1 : 0, st.classifyEnabled ? 1 : 0,
                        st.summarizeEnabled ? 1 : 0);
        }

        // 映像シグナル解析。diff の実測値を出す（logguide.toml の static_threshold 調整用）。
        if (const VideoAnalysisPipeline* video = system.VideoAnalysis()) {
            const VideoAnalysisPipeline::Status vs = video->GetStatus();
            if (!vs.enabled) {
                ImGui::TextDisabled("映像解析: 無効");
            } else if (vs.running) {
                ImGui::Text("映像解析中: samples=%llu events=%llu diff=%.4f",
                            static_cast<unsigned long long>(vs.samples),
                            static_cast<unsigned long long>(vs.events), vs.lastDiff);
            } else {
                ImGui::Text("映像解析: 待機中");
            }
        }

        // モデル欠落などで無効化された機能の警告。
        for (const auto& w : system.AnalysisWarnings()) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "⚠ %s", w.c_str());
        }
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

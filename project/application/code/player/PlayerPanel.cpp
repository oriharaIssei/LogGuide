#include "player/PlayerPanel.h"

#ifdef _DEBUG

/// module
#include "player/DualPlayerController.h"
#include "player/FileDialog.h"
#include "player/VideoTexture.h"

/// externals
#include <imgui/imgui.h>

/// stl
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <string>

namespace LogGuide {

namespace {

const wchar_t* kMp4Filter     = L"MP4 Video (*.mp4)\0*.mp4\0All Files (*.*)\0*.*\0\0";
const wchar_t* kSessionFilter = L"Session Manifest (session.json)\0session.json\0JSON (*.json)\0*.json\0\0";

std::string FormatTime(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int minutes = static_cast<int>(seconds) / 60;
    const double rem  = seconds - minutes * 60;
    char buf[32]      = {};
    std::snprintf(buf, sizeof(buf), "%d:%06.3f", minutes, rem);
    return buf;
}

// 空きスロットのインデックス。無ければ 0(=上書き)。
int NextSlot(const DualPlayerController& player) {
    for (int i = 0; i < DualPlayerController::kSlotCount; ++i) {
        if (!player.GetSlotView(i).loaded) {
            return i;
        }
    }
    return 0;
}

bool HasJsonExtension(const std::string& path) {
    if (path.size() < 5) {
        return false;
    }
    std::string tail = path.substr(path.size() - 5);
    for (auto& c : tail) {
        c = static_cast<char>(::tolower(c));
    }
    return tail == ".json";
}

void HandleIncomingFile(DualPlayerController& player, const std::string& path) {
    if (HasJsonExtension(path)) {
        player.OpenSession(path);
    } else {
        player.OpenSlot(NextSlot(player), path);
    }
}

void DrawVideo(const DualPlayerController::SlotView& view, float maxWidth) {
    if (!view.loaded || !view.texture || !view.texture->IsValid() || view.width == 0) {
        ImGui::Dummy(ImVec2(maxWidth, maxWidth * 9.0f / 16.0f));
        return;
    }
    const float aspect  = static_cast<float>(view.height) / static_cast<float>(view.width);
    const float displayW = maxWidth;
    const float displayH = maxWidth * aspect;
    ImGui::Image(reinterpret_cast<ImTextureID>(view.texture->GetGpuHandle().ptr), ImVec2(displayW, displayH));
}

} // namespace

void DrawPlayerPanel(DualPlayerController& player, WindowFileDrop& drop, HWND owner) {
    // OS ドロップされたファイルを取り込む。
    for (const auto& path : drop.PollDropped()) {
        HandleIncomingFile(player, path);
    }

    if (!ImGui::Begin("Media Player")) {
        ImGui::End();
        return;
    }

    // --- ファイルを開く ---
    if (ImGui::Button("Open A...")) {
        std::string p = OpenFileDialog(owner, kMp4Filter);
        if (!p.empty()) {
            player.OpenSlot(0, p);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open B...")) {
        std::string p = OpenFileDialog(owner, kMp4Filter);
        if (!p.empty()) {
            player.OpenSlot(1, p);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open session.json...")) {
        std::string p = OpenFileDialog(owner, kSessionFilter);
        if (!p.empty()) {
            player.OpenSession(p);
        }
    }
    ImGui::TextDisabled("(mp4/session.json をウィンドウにドラッグ&ドロップでも開けます)");

    if (!player.GetLastError().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", player.GetLastError().c_str());
    }

    // --- 映像表示(2 本を横並び) ---
    const float avail = ImGui::GetContentRegionAvail().x;
    const float halfW = (avail - 8.0f) * 0.5f;

    const auto viewA = player.GetSlotView(0);
    const auto viewB = player.GetSlotView(1);

    auto drawDiag = [](const char* tag, const DualPlayerController::SlotView& v) {
        if (!v.loaded) {
            ImGui::TextDisabled("%s: (empty)", tag);
            return;
        }
        ImGui::Text("%s: %ux%u audio=%d tex=%d play=%d frames=%llu pos=%.2f",
                    tag, v.width, v.height, v.hasAudio ? 1 : 0,
                    v.textureValid ? 1 : 0, v.playing ? 1 : 0,
                    static_cast<unsigned long long>(v.framesReceived), v.positionSeconds);
        ImGui::Text("    vPushed=%llu aSubmit=%llu readFail=%llu lastStream=%d (v=%u a=%u)",
                    static_cast<unsigned long long>(v.videoPushed),
                    static_cast<unsigned long long>(v.audioSubmitted),
                    static_cast<unsigned long long>(v.readFailed),
                    static_cast<int>(v.lastStreamIndex),
                    v.videoStreamIndex, v.audioStreamIndex);
    };

    ImGui::BeginGroup();
    ImGui::TextUnformatted(viewA.loaded ? (viewA.label.empty() ? "A" : viewA.label.c_str()) : "A (empty)");
    DrawVideo(viewA, halfW);
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::TextUnformatted(viewB.loaded ? (viewB.label.empty() ? "B" : viewB.label.c_str()) : "B (empty)");
    DrawVideo(viewB, halfW);
    ImGui::EndGroup();

    // --- 診断表示(映像が出ない原因切り分け用) ---
    drawDiag("A", viewA);
    drawDiag("B", viewB);

    // --- トランスポート ---
    ImGui::SeparatorText("Timeline");

    const double duration = player.GetMasterDuration();
    const double position = player.GetMasterPosition();

    if (ImGui::Button(player.IsPlaying() ? "Pause" : "Play", ImVec2(90, 0))) {
        player.TogglePlay();
    }
    ImGui::SameLine();
    ImGui::Text("%s / %s", FormatTime(position).c_str(), FormatTime(duration).c_str());

    // 1 本のシークバーで 2 動画の時間を同期させる。
    float t = static_cast<float>(position);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##timeline", &t, 0.0f, static_cast<float>(duration > 0.0 ? duration : 1.0), "%.2f s")) {
        player.SeekMaster(static_cast<double>(t));
    }

    ImGui::End();
}

} // namespace LogGuide

#else

namespace LogGuide {
class DualPlayerController;
class WindowFileDrop;
void DrawPlayerPanel(DualPlayerController&, WindowFileDrop&, HWND) {}
} // namespace LogGuide

#endif // _DEBUG

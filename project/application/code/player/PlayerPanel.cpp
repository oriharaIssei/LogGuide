#include "player/PlayerPanel.h"

#ifdef _DEBUG

/// module
#include "analysis/TimelineJsonlReader.h"
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

// イベント種別/タグごとの色。シークバーのマーカーとリストで共通に使う。
ImU32 EventColor(const TimelineEntry& e) {
    if (e.type == "speech") {
        if (e.tag == "confusion")   return IM_COL32(255, 200,  40, 255); // 黄: 迷い
        if (e.tag == "frustration") return IM_COL32(235,  70,  60, 255); // 赤: 不満
        if (e.tag == "delight")     return IM_COL32( 70, 200, 120, 255); // 緑: 喜び
        if (e.tag == "surprise")    return IM_COL32( 90, 160, 255, 255); // 青: 驚き
        return IM_COL32(180, 180, 180, 255);                             // 灰: 分類なし発話
    }
    if (e.type == "silence_start" || e.type == "silence_end")
        return IM_COL32(120, 120, 140, 255); // 無発話区間
    if (e.type == "volume_spike")
        return IM_COL32(230, 130, 230, 255); // 紫: 音量スパイク
    if (e.type == "speech_density")
        return IM_COL32(255, 150,  60, 255); // 橙: 発話密度
    if (e.type == "degradation")
        return IM_COL32(120,  80,  40, 255); // 茶: 縮退
    return IM_COL32(200, 200, 200, 255);
}

// イベント 1 件の 1 行ラベル（リスト表示用）。
std::string EventLabel(const TimelineEntry& e) {
    const int totalSec = static_cast<int>(e.timeMs / 1000);
    char ts[16] = {};
    std::snprintf(ts, sizeof(ts), "%d:%02d", totalSec / 60, totalSec % 60);

    std::string body;
    if (e.type == "speech") {
        body = (e.tag.empty() ? "" : "[" + e.tag + "] ") + e.text;
    } else if (e.type == "silence_start") {
        body = "(無発話 開始)";
    } else if (e.type == "silence_end") {
        body = "(無発話 終了 " + std::to_string(e.durationMs / 1000) + "s)";
    } else if (e.type == "volume_spike") {
        body = "♪ " + (e.label.empty() ? std::string("音量スパイク") : e.label);
    } else if (e.type == "speech_density") {
        body = "(発話密度 急増)";
    } else if (e.type == "degradation") {
        body = "⚠ 解析縮退";
    } else if (e.type == "summary") {
        body = "[要約] " + e.text;
    } else {
        body = e.type;
    }
    return std::string(ts) + "  " + body;
}

// シークバー矩形の上にイベントマーカー（tag 別色分け）を重ねて描く。
void DrawTimelineMarkers(const ImVec2& barMin, const ImVec2& barMax,
                         const TimelineData& timeline, double durationSec) {
    if (timeline.Empty() || durationSec <= 0.0) {
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float width = barMax.x - barMin.x;
    for (const auto& e : timeline.entries) {
        if (e.type == "meta" || e.type == "summary") {
            continue;
        }
        const double t = static_cast<double>(e.timeMs) / 1000.0;
        const float frac = static_cast<float>(t / durationSec);
        if (frac < 0.0f || frac > 1.0f) {
            continue;
        }
        const float x = barMin.x + frac * width;
        dl->AddLine(ImVec2(x, barMin.y), ImVec2(x, barMax.y), EventColor(e), 2.0f);
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
    // シークバー矩形の上に AI 解析イベントマーカーを重ねる。
    const ImVec2 barMin = ImGui::GetItemRectMin();
    const ImVec2 barMax = ImGui::GetItemRectMax();
    if (player.HasTimeline()) {
        DrawTimelineMarkers(barMin, barMax, player.GetTimeline(), duration);
    }

    // --- AI 解析タイムライン（イベントリスト / 要約） ---
    if (player.HasTimeline()) {
        const TimelineData& tl = player.GetTimeline();
        ImGui::SeparatorText("AI Analysis Timeline");

        if (!tl.summary.empty()) {
            if (ImGui::CollapsingHeader("Session Summary", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("%s", tl.summary.c_str());
            }
        }

        ImGui::Text("%zu events", tl.entries.size());
        if (ImGui::BeginChild("##events", ImVec2(0, 200), true)) {
            for (size_t i = 0; i < tl.entries.size(); ++i) {
                const TimelineEntry& e = tl.entries[i];
                if (e.type == "meta") {
                    continue;
                }
                ImGui::PushID(static_cast<int>(i));
                ImGui::PushStyleColor(ImGuiCol_Text, EventColor(e));
                const std::string label = EventLabel(e);
                // クリックで該当時刻へジャンプ。
                if (ImGui::Selectable(label.c_str())) {
                    player.SeekMaster(static_cast<double>(e.timeMs) / 1000.0);
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
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

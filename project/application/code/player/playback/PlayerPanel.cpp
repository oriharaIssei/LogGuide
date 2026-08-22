#include "playback/PlayerPanel.h"

#ifdef _DEBUG

/// module
#include "analysis/SummaryService.h"
#include "analysis/TimelineJsonlReader.h"
#include "playback/DualPlayerController.h"
#include "playback/FileDialog.h"
#include "playback/VideoTexture.h"

/// externals
#include <imgui/imgui.h>

/// stl
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <iterator>
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

// イベントの出自。マーカーの描き分け（上半分=映像 / 下半分=音声 / 全高=合成）に使う。
enum class EventGroup { Audio, Video, Composite, None };

// フィルタ 1 項目 = 関連する type をまとめた表示単位。
struct FilterItem {
    const char* key;   // 代表 type（start/end 対はまとめる）
    const char* label; // チェックボックスの表示名
    EventGroup  group;
};

constexpr FilterItem kFilterItems[] = {
    {"speech",              "発話",         EventGroup::Audio},
    {"silence",             "無発話",       EventGroup::Audio},
    {"volume_spike",        "音量スパイク", EventGroup::Audio},
    {"speech_density",      "発話密度",     EventGroup::Audio},
    {"degradation",         "解析縮退",     EventGroup::Audio},
    {"screen_static",       "画面停滞",     EventGroup::Video},
    {"screen_blank",        "暗転/ロード",  EventGroup::Video},
    {"screen_transition",   "画面遷移",     EventGroup::Video},
    {"screen_thrash",       "遷移頻発",     EventGroup::Video},
    {"stuck_candidate",     "詰まり候補",   EventGroup::Composite},
    {"unexpected_reaction", "想定外の反応", EventGroup::Composite},
    {"focus_likely",        "集中状態",     EventGroup::Composite},
};
constexpr int kFilterCount = static_cast<int>(std::size(kFilterItems));

// type -> kFilterItems のインデックス。マーカーにしない type（meta/summary）は -1。
int FilterIndexOf(const std::string& type) {
    if (type == "speech")               return 0;
    if (type == "silence_start" || type == "silence_end") return 1;
    if (type == "volume_spike")         return 2;
    if (type == "speech_density")       return 3;
    if (type == "degradation")          return 4;
    if (type == "screen_static_start" || type == "screen_static_end") return 5;
    if (type == "screen_blank_start"  || type == "screen_blank_end")  return 6;
    if (type == "screen_transition")    return 7;
    if (type == "screen_thrash")        return 8;
    if (type == "stuck_candidate")      return 9;
    if (type == "unexpected_reaction")  return 10;
    if (type == "focus_likely")         return 11;
    return -1;
}

// マーカー表示のフィルタ状態。ビューアのセッション内で保持する。
struct TimelineFilter {
    bool visible[kFilterCount];
    bool showSuppressed = false; // focus_likely に抑制された無発話イベントを表示するか

    TimelineFilter() {
        for (bool& v : visible) {
            v = true;
        }
    }

    bool Accepts(const TimelineEntry& e) const {
        const int idx = FilterIndexOf(e.type);
        if (idx < 0) {
            return false;
        }
        if (e.suppressed && !showSuppressed) {
            return false;
        }
        return visible[idx];
    }
};

EventGroup GroupOf(const std::string& type) {
    const int idx = FilterIndexOf(type);
    return idx < 0 ? EventGroup::None : kFilterItems[idx].group;
}

// イベント種別/タグごとの色。シークバーのマーカーとリストで共通に使う。
ImU32 EventColor(const TimelineEntry& e) {
    // 合成イベント: 単独シグナルより確度が高いので彩度を上げて手前に置く。
    if (e.type == "stuck_candidate")     return IM_COL32(255,  40,  40, 255); // 強い赤: 詰まり候補
    if (e.type == "unexpected_reaction") return IM_COL32(255, 170,   0, 255); // 橙: 想定外の反応
    if (e.type == "focus_likely")        return IM_COL32(110, 210, 130, 255); // 緑: 集中状態

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

    if (e.type == "screen_static_start" || e.type == "screen_static_end")
        return IM_COL32(100, 145, 195, 255); // 青灰: 画面停滞
    if (e.type == "screen_blank_start" || e.type == "screen_blank_end")
        return IM_COL32( 90,  90, 120, 255); // 暗い青: 暗転/ロード
    if (e.type == "screen_transition")
        return IM_COL32( 80, 200, 200, 255); // シアン: 画面遷移
    if (e.type == "screen_thrash")
        return IM_COL32(255, 110, 180, 255); // ピンク: 遷移頻発
    return IM_COL32(200, 200, 200, 255);
}

std::string FormatStamp(int64_t timeMs) {
    const int totalSec = static_cast<int>(timeMs / 1000);
    char ts[16] = {};
    std::snprintf(ts, sizeof(ts), "%d:%02d", totalSec / 60, totalSec % 60);
    return ts;
}

// イベント 1 件の 1 行ラベル（リスト表示用）。
std::string EventLabel(const TimelineEntry& e) {
    const std::string secs = std::to_string(e.durationMs / 1000);

    std::string body;
    if (e.type == "speech") {
        body = (e.tag.empty() ? "" : "[" + e.tag + "] ") + e.text;
    } else if (e.type == "silence_start") {
        body = "(無発話 開始)";
    } else if (e.type == "silence_end") {
        body = "(無発話 終了 " + secs + "s)";
    } else if (e.type == "volume_spike") {
        body = "♪ " + (e.label.empty() ? std::string("音量スパイク") : e.label);
    } else if (e.type == "speech_density") {
        body = "(発話密度 急増)";
    } else if (e.type == "degradation") {
        body = "⚠ 解析縮退";
    } else if (e.type == "summary") {
        body = "[要約] " + e.text;
    } else if (e.type == "screen_static_start") {
        body = "(画面停滞 開始)";
    } else if (e.type == "screen_static_end") {
        body = "(画面停滞 終了 " + secs + "s)";
    } else if (e.type == "screen_blank_start") {
        body = "(暗転 開始" + (e.label.empty() ? std::string() : " " + e.label) + ")";
    } else if (e.type == "screen_blank_end") {
        body = "(暗転 終了 " + secs + "s)";
    } else if (e.type == "screen_transition") {
        body = "→ 画面遷移";
    } else if (e.type == "screen_thrash") {
        body = "⇄ 画面遷移が頻発 (" + std::to_string(e.count) + "回)";
    } else if (e.type == "stuck_candidate") {
        body = "★ 詰まり候補 (無発話×画面停滞 " + std::to_string(e.overlapMs / 1000) + "s)";
    } else if (e.type == "unexpected_reaction") {
        body = "! 想定外の反応 (画面遷移の " + std::to_string(e.gapMs) + "ms 後)";
    } else if (e.type == "focus_likely") {
        body = "◎ 集中状態 (無発話 " + secs + "s だが画面は動作)";
    } else {
        body = e.type;
    }
    return FormatStamp(e.timeMs) + "  " + body;
}

// シークバー矩形の上にイベントマーカーを重ねて描く。
//   音声イベント = バー下半分 / 映像イベント = バー上半分 / 合成イベント = 全高（太線）
// 合成は最後に描いて手前に出す。詰まり候補はさらに三角の目印を上に付ける。
void DrawTimelineMarkers(const ImVec2& barMin, const ImVec2& barMax,
                         const TimelineData& timeline, double durationSec,
                         const TimelineFilter& filter) {
    if (timeline.Empty() || durationSec <= 0.0) {
        return;
    }
    ImDrawList* dl    = ImGui::GetWindowDrawList();
    const float width = barMax.x - barMin.x;
    const float midY  = (barMin.y + barMax.y) * 0.5f;

    auto drawPass = [&](bool composite) {
        for (const auto& e : timeline.entries) {
            const EventGroup group = GroupOf(e.type);
            if ((group == EventGroup::Composite) != composite || !filter.Accepts(e)) {
                continue;
            }
            const float frac = static_cast<float>((e.timeMs / 1000.0) / durationSec);
            if (frac < 0.0f || frac > 1.0f) {
                continue;
            }
            const float  x   = barMin.x + frac * width;
            const ImU32  col = EventColor(e);

            if (group == EventGroup::Audio) {
                dl->AddLine(ImVec2(x, midY), ImVec2(x, barMax.y), col, 2.0f);
            } else if (group == EventGroup::Video) {
                dl->AddLine(ImVec2(x, barMin.y), ImVec2(x, midY), col, 2.0f);
            } else {
                dl->AddLine(ImVec2(x, barMin.y), ImVec2(x, barMax.y), col, 3.0f);
            }

            if (e.type == "stuck_candidate") {
                // 最重要イベント。バー上に下向き三角を出して一目で見つかるようにする。
                const float s = 5.0f;
                dl->AddTriangleFilled(ImVec2(x - s, barMin.y - s * 1.6f),
                                      ImVec2(x + s, barMin.y - s * 1.6f),
                                      ImVec2(x, barMin.y), col);
            }
        }
    };
    drawPass(false); // 音声・映像
    drawPass(true);  // 合成（手前）
}

// type 別のマーカー表示 ON/OFF。グループごとに列を分けて並べる。
void DrawFilterControls(TimelineFilter& filter) {
    if (!ImGui::CollapsingHeader("マーカー表示")) {
        return;
    }
    auto column = [&](const char* title, EventGroup group) {
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", title);
        for (int i = 0; i < kFilterCount; ++i) {
            if (kFilterItems[i].group == group) {
                ImGui::Checkbox(kFilterItems[i].label, &filter.visible[i]);
            }
        }
        ImGui::EndGroup();
    };
    column("音声", EventGroup::Audio);
    ImGui::SameLine(0.0f, 24.0f);
    column("映像", EventGroup::Video);
    ImGui::SameLine(0.0f, 24.0f);
    column("合成", EventGroup::Composite);

    ImGui::Checkbox("抑制されたイベントも表示 (集中状態と判定された無発話)", &filter.showSuppressed);
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

void DrawPlayerPanel(DualPlayerController& player, WindowFileDrop& drop, HWND owner,
                     SummaryService* summary) {
    // マーカーのフィルタ状態はパネルをまたいで保持する（ビューアのセッション設定）。
    static TimelineFilter filter;

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
        DrawTimelineMarkers(barMin, barMax, player.GetTimeline(), duration, filter);
    }

    // --- AI 解析タイムライン（イベントリスト / 要約） ---
    if (player.HasTimeline()) {
        const TimelineData& tl = player.GetTimeline();
        ImGui::SeparatorText("AI Analysis Timeline");

        // 要約を生成ボタン: 開いているタイムライン(.jsonl)に対して on-demand で要約を実行する。
        if (summary != nullptr && !player.GetTimelinePath().empty()) {
            const bool summarizing = summary->IsSummarizing();
            if (summarizing) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "要約生成中...");
                // 生成完了を検知したらタイムラインを読み直して summary を反映する。
                // （IsSummarizing が false に落ちた次フレームで ReloadTimeline される）
            } else {
                ImGui::BeginDisabled(!summary->CanSummarize());
                if (ImGui::Button(tl.summary.empty() ? "要約を生成" : "要約を再生成")) {
                    summary->SummarizeExisting(player.GetTimelinePath());
                }
                ImGui::EndDisabled();
                if (!summary->CanSummarize()) {
                    ImGui::SameLine();
                    // 再生アプリに録画は無いので、無効化の理由は summarize モデルのみ。
                    ImGui::TextDisabled("(summarize モデル無効)");
                }
            }
            // 直前フレームが要約中で、今フレーム完了していたらリロード。
            static bool wasSummarizing = false;
            if (wasSummarizing && !summarizing) {
                player.ReloadTimeline();
            }
            wasSummarizing = summarizing;
        }

        if (!tl.summary.empty()) {
            if (ImGui::CollapsingHeader("Session Summary", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("%s", tl.summary.c_str());
            }
        }

        DrawFilterControls(filter);

        // meta / summary はマーカーにならないので母数から外す。
        size_t shown = 0;
        size_t total = 0;
        for (const auto& e : tl.entries) {
            if (FilterIndexOf(e.type) < 0) {
                continue;
            }
            ++total;
            if (filter.Accepts(e)) {
                ++shown;
            }
        }
        ImGui::Text("%zu / %zu events", shown, total);

        if (ImGui::BeginChild("##events", ImVec2(0, 200), true)) {
            for (size_t i = 0; i < tl.entries.size(); ++i) {
                const TimelineEntry& e = tl.entries[i];
                if (!filter.Accepts(e)) {
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

                // 合成イベントは、根拠になった元イベントをホバーで確認できるようにする。
                if (!e.sources.empty() && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextDisabled("根拠となったイベント");
                    for (const auto& s : e.sources) {
                        ImGui::Text("%s  %s", FormatStamp(s.timeMs).c_str(), s.type.c_str());
                    }
                    ImGui::EndTooltip();
                }
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
class SummaryService;
void DrawPlayerPanel(DualPlayerController&, WindowFileDrop&, HWND, SummaryService*) {}
} // namespace LogGuide

#endif // _DEBUG

#include "TerminalPanel.h"

#ifdef _DEBUG

/// module
#include "AppLauncher.h"
#include "SessionCatalog.h"

/// externals
#include <imgui/imgui.h>

/// stl
#include <cstdio>
#include <string>
#include <vector>

namespace LogGuide {

namespace {

// セッション尺の表示用フォーマット（m:ss）。
// shared/analysis/SessionSummarizer.cpp の FormatStamp と同じ整形だが、
// ここでは int64_t(ミリ秒) ではなく double(秒) を受け取る。
std::string FormatDuration(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int totalSec = static_cast<int>(seconds + 0.5); // 四捨五入
    char buf[16]        = {};
    std::snprintf(buf, sizeof(buf), "%d:%02d", totalSec / 60, totalSec % 60);
    return buf;
}

} // namespace

bool DrawTerminalPanel(SessionCatalog& catalog, std::string* outError) {
    // 選択中のセッションはパネルをまたいで保持する。
    static int selected = -1;

    bool launched = false;

    if (!ImGui::Begin("LogGuide")) {
        ImGui::End();
        return false;
    }

    // --- レコーダーを起動 ---
    ImGui::SeparatorText("アプリを選択");
    if (ImGui::Button("レコーダーを起動")) {
        std::string err;
        if (LaunchRecorder(&err)) {
            launched = true;
        } else if (outError != nullptr) {
            *outError = err;
        }
    }

    // --- セッションを選んで再生 ---
    ImGui::SeparatorText("セッションを選択して再生");
    if (ImGui::Button("更新")) {
        catalog.Refresh();
    }

    const std::vector<SessionEntry>& entries = catalog.Entries();

    // Refresh でリストが変わり、選択インデックスが範囲外になっていたら戻す。
    if (selected >= static_cast<int>(entries.size())) {
        selected = -1;
    }

    if (entries.empty()) {
        ImGui::TextDisabled("録画セッションがありません。先にレコーダーで録画してください。");
    } else if (ImGui::BeginTable("##sessions", 4,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                  ImVec2(0.0f, 200.0f))) {
        ImGui::TableSetupColumn("セッション");
        ImGui::TableSetupColumn("開始時刻");
        ImGui::TableSetupColumn("尺");
        ImGui::TableSetupColumn("トラック数");
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const SessionEntry& e = entries[static_cast<size_t>(i)];

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::PushID(i);
            // session.json を読めない行は選択不可にする（BeginDisabled で操作も見た目も無効化）。
            ImGui::BeginDisabled(!e.manifestValid);
            const bool isSelected = (selected == i);
            const std::string label =
                e.manifestValid ? e.sessionId : (e.sessionId + " (session.json を読めません)");
            if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                selected = i;
            }
            ImGui::EndDisabled();
            ImGui::PopID();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.startedAtIso.empty() ? "-" : e.startedAtIso.c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatDuration(e.durationSeconds).c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%d", e.trackCount);
        }

        ImGui::EndTable();
    }

    const bool hasValidSelection = (selected >= 0) &&
                                    (selected < static_cast<int>(entries.size())) &&
                                    entries[static_cast<size_t>(selected)].manifestValid;

    ImGui::BeginDisabled(entries.empty() || !hasValidSelection);
    if (ImGui::Button("選択したセッションを再生")) {
        std::string err;
        if (LaunchPlayer(entries[static_cast<size_t>(selected)].manifestPath, &err)) {
            launched = true;
        } else if (outError != nullptr) {
            *outError = err;
        }
    }
    ImGui::EndDisabled();

    // セッションを指定せずにプレイヤーを起動（プレイヤー側の UI から開く運用のため）。
    if (ImGui::Button("セッションを選ばずにプレイヤーを起動")) {
        std::string err;
        if (LaunchPlayer("", &err)) {
            launched = true;
        } else if (outError != nullptr) {
            *outError = err;
        }
    }

    // --- エラー表示 ---
    if (!catalog.LastError().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", catalog.LastError().c_str());
    }
    if (outError != nullptr && !outError->empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", outError->c_str());
    }

    ImGui::End();
    return launched;
}

} // namespace LogGuide

#else

namespace LogGuide {
class SessionCatalog;
bool DrawTerminalPanel(SessionCatalog&, std::string*) { return false; }
} // namespace LogGuide

#endif // _DEBUG

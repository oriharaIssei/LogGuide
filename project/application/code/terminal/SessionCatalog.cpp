#include "SessionCatalog.h"

/// module
#include "recording/RecordingComponents.h"
#include "recording/SessionManifest.h"

/// stl
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace LogGuide {

void SessionCatalog::Refresh(const std::string& root) {
    entries_.clear();
    lastError_.clear();

    try {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) {
            // recordings/ がまだ存在しない = 一度も録画していない正常な状態。
            // エラー扱いにせず空リストのまま返す（lastError_ も空のまま）。
            return;
        }

        for (const auto& dirEntry : fs::directory_iterator(root)) {
            if (!dirEntry.is_directory()) {
                continue; // ディレクトリでなければ黙って飛ばす
            }

            const fs::path manifestPath = dirEntry.path() / "session.json";
            if (!fs::is_regular_file(manifestPath)) {
                continue; // session.json の無いディレクトリは黙って飛ばす
            }

            SessionEntry sessionEntry;
            sessionEntry.sessionId    = dirEntry.path().filename().string();
            sessionEntry.manifestPath = fs::absolute(manifestPath).string(); // 再生アプリへ渡すため絶対パスに正規化

            SessionInfo info;
            if (ReadSessionManifest(sessionEntry.manifestPath, info, nullptr)) {
                sessionEntry.manifestValid   = true;
                sessionEntry.startedAtIso    = info.startedAtIso;
                sessionEntry.durationSeconds = info.durationSeconds;
                sessionEntry.trackCount      = static_cast<int>(info.tracks.size());
            }
            // 読めなかった場合も manifestValid=false のままエントリ自体は残す。
            // UI 側で「壊れたセッション」として表示できるようにするため。

            entries_.push_back(std::move(sessionEntry));
        }
    } catch (const std::exception& e) {
        // ディレクトリ走査中の予期しない例外（アクセス権限など）で落とさない。
        lastError_ = std::string("セッション一覧の走査に失敗しました: ") + e.what();
    }

    // sessionId 降順（session_YYYYMMDD_HHMMSS 形式なので辞書順降順 = 新しい順）。
    std::sort(entries_.begin(), entries_.end(), [](const SessionEntry& a, const SessionEntry& b) {
        return a.sessionId > b.sessionId;
    });
}

} // namespace LogGuide

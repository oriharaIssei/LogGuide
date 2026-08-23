#pragma once
#include <string>
#include <vector>

// =============================================================================
// SessionCatalog
//
// recordings/ 配下の録画セッション（session_YYYYMMDD_HHMMSS/ ディレクトリ）を
// 列挙し、再生アプリに渡せる形（SessionEntry）へまとめる。
// ImGui / EditorController には一切依存しない（全構成でコンパイルできること）。
// =============================================================================

namespace LogGuide {

// recordings/ 配下の 1 セッション（= 再生アプリに渡す単位）。
struct SessionEntry {
    std::string sessionId;              // ディレクトリ名 "session_20260822_143012"
    std::string manifestPath;           // session.json の絶対パス（再生アプリへ渡す）
    std::string startedAtIso;           // 録画開始時刻。manifest が読めなければ空
    double      durationSeconds = 0.0;  // 録画尺
    int         trackCount      = 0;    // mp4 の本数
    bool        manifestValid   = false;// session.json を読めたか
};

class SessionCatalog {
public:
    // root 以下を走査し、session.json を持つディレクトリを列挙する。新しい順に並べる。
    void Refresh(const std::string& root = "recordings");

    const std::vector<SessionEntry>& Entries() const { return entries_; }
    const std::string&               LastError() const { return lastError_; }

private:
    std::vector<SessionEntry> entries_;
    std::string               lastError_;
};

} // namespace LogGuide

#pragma once

/// stl
#include <cstdio>
#include <mutex>
#include <string>

/// externals
#include <nlohmann/json.hpp>

namespace LogGuide {

// =============================================================================
// TimelineJsonlWriter
//
// タイムラインイベントを JSON Lines（1 イベント 1 行）で追記出力するライタ。
// クラッシュ耐性を最優先とし、以下を守る:
//   - 追記のみ。ファイル全体の書き直しは行わない。
//   - 1 行書くたびに即時フラッシュ（fflush + OS バッファのフラッシュ）。
//     解析中にアプリが強制終了しても、直前の行まで無傷で読める。
//
// 数百イベント/セッション程度なので I/O 性能は問題にならない。
// 複数スレッド（解析ワーカ / 信号検出）から呼ばれるため mutex で直列化する。
// =============================================================================
class TimelineJsonlWriter {
public:
    TimelineJsonlWriter() = default;
    ~TimelineJsonlWriter();

    TimelineJsonlWriter(const TimelineJsonlWriter&)            = delete;
    TimelineJsonlWriter& operator=(const TimelineJsonlWriter&) = delete;

    // path を追記モードで開く。既存内容は保持する。失敗時 false。
    bool Open(const std::string& path);
    void Close();
    bool IsOpen() const;

    // 1 イベントを 1 行として追記し、即時フラッシュする。失敗時 false。
    bool Write(const nlohmann::json& event);

    const std::string& GetPath() const { return path_; }

private:
    std::FILE*         file_ = nullptr;
    std::string        path_;
    mutable std::mutex mutex_;
};

} // namespace LogGuide

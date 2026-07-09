#include "analysis/TimelineJsonlWriter.h"

/// windows (即時フラッシュ用)
#include <io.h>
#include <windows.h>

namespace LogGuide {

TimelineJsonlWriter::~TimelineJsonlWriter() {
    Close();
}

bool TimelineJsonlWriter::Open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    // 追記モード。ファイルが無ければ作成する。
    if (fopen_s(&file_, path.c_str(), "ab") != 0 || !file_) {
        file_ = nullptr;
        return false;
    }
    path_ = path;
    return true;
}

void TimelineJsonlWriter::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

bool TimelineJsonlWriter::IsOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_ != nullptr;
}

bool TimelineJsonlWriter::Write(const nlohmann::json& event) {
    // dump は UTF-8。ensure_ascii=false で日本語をそのまま出す（人が読める JSONL にする）。
    std::string line = event.dump(-1, ' ', false);
    line.push_back('\n');

    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_) {
        return false;
    }
    if (std::fwrite(line.data(), 1, line.size(), file_) != line.size()) {
        return false;
    }
    // C ランタイムバッファ → OS へ。さらに OS バッファ → ディスクまで落とし、
    // クラッシュしても直前の行まで確実に残るようにする。
    std::fflush(file_);
    int fd = _fileno(file_);
    if (fd >= 0) {
        HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
        if (h != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(h);
        }
    }
    return true;
}

} // namespace LogGuide

#include "AppLauncher.h"

/// windows (exe 起動 / UTF-8 変換用)
#include <windows.h>

/// stl
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace LogGuide {

namespace {

// std::wstring -> UTF-8。RecordingSystem.cpp / FileDialog.cpp の WideToUtf8 と同じやり方。
std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

// UTF-8 -> std::wstring。上の WideToUtf8 と対になる向きの変換（同じ「まずサイズを問い合わせる」やり方）。
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

// 自分（呼び出し元 exe）のフルパスを取得する。
// GetModuleFileNameW はバッファ不足時に切り詰めて返す（戻り値 == バッファサイズ）ので、
// その場合はバッファを倍にして取り直す。
std::wstring GetSelfExePath() {
    DWORD size = MAX_PATH;
    std::vector<wchar_t> buffer(size);
    for (;;) {
        DWORD len = GetModuleFileNameW(nullptr, buffer.data(), size);
        if (len == 0) {
            return {}; // 取得失敗
        }
        if (len < size) {
            return std::wstring(buffer.data(), len);
        }
        // 切り詰められた可能性がある。バッファを倍にして再試行する。
        size *= 2;
        buffer.resize(size);
        if (size > (1u << 20)) {
            return {}; // 異常系のフェイルセーフ
        }
    }
}

// exePath / argument をダブルクォートで囲んだコマンドライン文字列を組み立てる。
// argument が空なら exe パスのみ。
std::wstring BuildCommandLine(const std::wstring& exePath, const std::wstring& argument) {
    std::wstring cmd;
    cmd += L'"';
    cmd += exePath;
    cmd += L'"';
    if (!argument.empty()) {
        cmd += L" \"";
        cmd += argument;
        cmd += L'"';
    }
    return cmd;
}

} // namespace

bool LaunchSiblingApp(const wchar_t* exeName, const std::string& argument, std::string* err) {
    const std::wstring selfPath = GetSelfExePath();
    if (selfPath.empty()) {
        if (err) {
            *err = "自分の実行ファイルパスの取得に失敗しました";
        }
        return false;
    }

    // cwd(project/) ではなく、自分の exe と同じディレクトリを基準にする。
    const fs::path exePath = fs::path(selfPath).parent_path() / exeName;

    std::error_code ec;
    if (!fs::exists(exePath, ec) || ec) {
        if (err) {
            *err = WideToUtf8(exeName) + " が見つかりません: " + WideToUtf8(exePath.wstring());
        }
        return false;
    }

    const std::wstring argumentWide = Utf8ToWide(argument);
    const std::wstring commandLine  = BuildCommandLine(exePath.wstring(), argumentWide);

    // CreateProcessW は lpCommandLine を書き換える可能性があるため、
    // 書き込み可能なバッファ（末尾 null 終端込み）にコピーして渡す。
    std::vector<wchar_t> commandLineBuffer(commandLine.begin(), commandLine.end());
    commandLineBuffer.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    const BOOL ok = CreateProcessW(
        nullptr,                  // lpApplicationName（lpCommandLine 側にパスを含めるので未使用）
        commandLineBuffer.data(), // lpCommandLine（書き込み可能バッファ）
        nullptr,                  // lpProcessAttributes
        nullptr,                  // lpThreadAttributes
        FALSE,                    // bInheritHandles
        0,                        // dwCreationFlags
        nullptr,                  // lpEnvironment（親の環境を継承）
        nullptr,                  // lpCurrentDirectory（親の cwd = project/ を引き継がせる）
        &startupInfo,
        &processInfo);

    if (!ok) {
        // GetLastError は後続の Win32 呼び出し（WideToUtf8 内の変換 API）で
        // 書き換わりうるので、失敗を検知した直後に控えておく。
        const DWORD lastError = GetLastError();
        if (err) {
            *err = WideToUtf8(exeName) + " の起動に失敗しました (GetLastError=" +
                   std::to_string(lastError) + ")";
        }
        return false;
    }

    // ターミナルは起動したプロセスを待たずに終了するので、ハンドルは即座に閉じる。
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

bool LaunchRecorder(std::string* err) {
    return LaunchSiblingApp(L"LogGuideRecorder.exe", std::string(), err);
}

bool LaunchPlayer(const std::string& sessionJsonPath, std::string* err) {
    return LaunchSiblingApp(L"LogGuidePlayer.exe", sessionJsonPath, err);
}

} // namespace LogGuide

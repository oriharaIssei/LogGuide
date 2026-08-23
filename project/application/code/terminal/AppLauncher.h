#pragma once
#include <string>

// =============================================================================
// AppLauncher
//
// 自分（LogGuideTerminal.exe）と同じディレクトリにある兄弟 exe
// （LogGuideRecorder.exe / LogGuidePlayer.exe）を起動する。
// ターミナルは起動したら自身を終了する純粋なランチャーなので、起動した
// プロセスを待つことはしない。ImGui / EditorController には依存しない
// （全構成でコンパイルできること）。
// =============================================================================

namespace LogGuide {

// 自分の exe と同じディレクトリにある exe を起動する。作業ディレクトリは引き継ぐ。
// argument が空でなければコマンドライン引数として 1 つ渡す。失敗時 false（err に理由）。
bool LaunchSiblingApp(const wchar_t* exeName, const std::string& argument,
                      std::string* err = nullptr);

// 録画アプリを起動する。
bool LaunchRecorder(std::string* err = nullptr);
// 再生アプリを起動し、session.json を開かせる（空なら何も開かずに起動）。
bool LaunchPlayer(const std::string& sessionJsonPath, std::string* err = nullptr);

} // namespace LogGuide

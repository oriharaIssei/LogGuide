#pragma once
#include <string>

// ImGui によるランチャー UI。ImGui は _DEBUG ビルドでのみ有効。
namespace LogGuide {
class SessionCatalog;

// ランチャー UI を 1 フレーム描画する。Engine::BeginFrame と EndFrame の間で呼ぶこと。
// アプリの起動を実行したら true を返す(呼び出し側はターミナルを終了させる)。
// 起動に失敗した場合は false を返し、outError に理由を格納する。
bool DrawTerminalPanel(SessionCatalog& catalog, std::string* outError);

} // namespace LogGuide

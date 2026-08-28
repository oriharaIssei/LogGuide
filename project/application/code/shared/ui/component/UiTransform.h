#pragma once

#include "component/IComponent.h"

// parent (EntityHandle) をメンバとして値で持つために定義が要る
// (component/IComponent.h 経由で間接的には入っているが、直接使うヘッダなので明示的に include する)。
#include "entity/EntityHandle.h"

#include <cstdint>
#include <string>
#include <Vector2.h>

namespace LogGuide {

/// UI 要素の矩形を決めるコンポーネント.
/// 画面座標系は左上原点・+X 右・+Y 下・単位ピクセル.
///
/// アンカー方式 (Unity の RectTransform と同じ考え方):
///   矩形 = 親矩形の (anchorMin, anchorMax) の位置に (offsetMin, offsetMax) を足したもの
///   anchorMin == anchorMax なら「点アンカー + サイズ指定」、
///   違えば親のサイズに追従してストレッチする.
/// v4 で親子関係に対応した。parent が無効なハンドルなら親矩形は画面全体になる
/// (v1〜v3 まではこれしか無かったので、結果は完全に互換).
class UiTransform final : public OriGine::IComponent {
public:
    UiTransform()           = default;
    ~UiTransform() override = default;

    void Initialize(OriGine::Scene* _scene, const OriGine::EntityHandle& _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, const OriGine::EntityHandle& _owner, const std::string& _parentLabel) override;

    // --- レイアウト入力 (JSON に保存する) ---
    OriGine::Vec2f anchorMin = {0.5f, 0.5f};
    OriGine::Vec2f anchorMax = {0.5f, 0.5f};
    OriGine::Vec2f offsetMin = {-160.0f, -60.0f};
    OriGine::Vec2f offsetMax = {160.0f, 60.0f};
    /// 描画とヒットテストの前後関係. 大きいほど手前.
    int32_t renderPriority   = 0;
    bool visible             = true;
    /// 親要素. 無効なハンドルなら画面全体を親矩形とする.
    /// 親子の解決順は UiLayoutSystem が再帰で面倒を見るので、登録順は問わない.
    OriGine::EntityHandle parent{};
    /// true なら、子孫をこの矩形で切る（ウィンドウの内容をはみ出させないために使う）.
    /// 自分自身が切られるかどうかは、親から受け継いだクリップ矩形で決まる.
    bool clipChildren = false;
    /// 描画先サーフェス (v10). 0 = メインウィンドウ、1 以上は NativeWindowManager が管理する
    /// 追加の OS ウィンドウ. 親がいる場合は親の resolvedSurfaceId を継承する（このフィールドは
    /// 無視される）ので、実際に指定するのはウィンドウのルートだけでよい.
    int32_t surfaceId = 0;

    // --- レイアウト結果 (UiLayoutSystem が毎フレーム書き込む。JSON に保存しない) ---
    OriGine::Vec2f resolvedMin = {0.0f, 0.0f};
    OriGine::Vec2f resolvedMax = {0.0f, 0.0f};
    /// 自分を描くときのクリップ矩形（親から受け継いだもの）. 画面ピクセル.
    OriGine::Vec2f clipMin = {0.0f, 0.0f};
    OriGine::Vec2f clipMax = {0.0f, 0.0f};
    /// このフレームで解決済みかを判定するための世代番号. 0 は未解決.
    uint32_t resolvedFrame = 0;
    /// 階層で加算した描画/ヒットテストの優先度. = 親の resolvedPriority + 自分の renderPriority.
    /// ウィンドウがクリックで前後するため、静的な renderPriority だけでは足りない.
    int32_t resolvedPriority = 0;
    /// 実際に描画される先のサーフェス ID (v10). 親がいれば親の値をそのまま継承する.
    int32_t resolvedSurfaceId = 0;
    /// サーフェスが既に無効 (ウィンドウが閉じられた等) な場合に visible 相当として扱うための
    /// 実効可視状態 (v10). UiRenderSystem / UiInteractionSystem / UiWindowSystem は
    /// visible ではなくこちらを見ること.
    bool resolvedVisible = true;
};

void to_json(nlohmann::json& _j, const UiTransform& _c);
void from_json(const nlohmann::json& _j, UiTransform& _c);

} // namespace LogGuide

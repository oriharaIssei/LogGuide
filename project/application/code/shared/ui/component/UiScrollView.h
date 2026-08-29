#pragma once

#include "component/IComponent.h"

// content / scrollBar / scrollThumb (EntityHandle) をメンバとして値で持つために定義が要る
// (component/IComponent.h 経由で間接的には入っているが、直接使うヘッダなので明示的に include する)。
#include "entity/EntityHandle.h"

#include <string>

namespace LogGuide {

/// 縦スクロールできるビューポート (v12).
/// UiTransform(clipChildren = true) + UiRect(背景) と組み合わせて使う。
/// 構成は次の 4 エンティティ (UiWidgetBuilder::CreateUiScrollView が組み立てる):
///   ビューポート (このコンポーネントが付くエンティティ) … UiTransform(clipChildren) + UiRect
///     content    … UiTransform (縦に伸びる。中身はこれを親にする)
///     scrollBar  … UiTransform + UiRect (バーの溝。ビューポートの右端に張り付く)
///       scrollThumb … UiTransform + UiRect + UiInteractable + UiHighlight (つまみ。溝の子)
/// 実際のスクロール処理 (ホイール/ドラッグ/クリップ) は UiScrollSystem が毎フレーム行う.
class UiScrollView final : public OriGine::IComponent {
public:
    UiScrollView()           = default;
    ~UiScrollView() override = default;

    void Initialize(OriGine::Scene* _scene, const OriGine::EntityHandle& _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, const OriGine::EntityHandle& _owner, const std::string& _parentLabel) override;

    // --- 設定 (JSON に保存する) ---
    /// ホイール 1 ノッチあたりのスクロール量 (px)
    float wheelStep = 48.0f;
    /// スクロールバーの幅 (px). 0 ならバーを出さない
    float scrollBarWidth = 10.0f;

    // --- 構成エンティティ ---
    OriGine::EntityHandle content{};     ///< 中身を足すときの親
    OriGine::EntityHandle scrollBar{};   ///< バーの溝
    OriGine::EntityHandle scrollThumb{}; ///< つまみ

    // --- 実行時の状態 (UiScrollSystem が書き込む。JSON には保存しない) ---
    /// 上端からのスクロール量 (px, 0 以上)
    float scrollOffset = 0.0f;
    /// 中身の高さ (px). アプリが設定する。子を走査した自動計算はしない.
    float contentHeight = 0.0f;
    /// ビューポートの高さ (px). UiScrollSystem が書く.
    float viewHeight = 0.0f;
    bool isDraggingThumb = false;
    float thumbGrabOffset = 0.0f;
};

void to_json(nlohmann::json& _j, const UiScrollView& _c);
void from_json(const nlohmann::json& _j, UiScrollView& _c);

} // namespace LogGuide

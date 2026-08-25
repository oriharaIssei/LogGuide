#pragma once

#include "component/IComponent.h"

// titleBar / contentArea (EntityHandle) をメンバとして値で持つために定義が要る
// (component/IComponent.h 経由で間接的には入っているが、直接使うヘッダなので明示的に include する)。
#include "entity/EntityHandle.h"

#include <Vector2.h>
#include <cstdint>
#include <string>

namespace LogGuide {

/// 移動できるウィンドウ.
/// ウィンドウは以下の 3 つのエンティティで構成する（UiWindowBuilder が組み立てる）:
///   ルート     … UiTransform + UiRect(枠) + UiWindow + clipChildren
///     タイトルバー … UiTransform + UiRect + UiText + TextComponent + UiInteractable + UiHighlight
///     内容領域   … UiTransform + clipChildren （中身はこの子にする）
///
/// ルートの UiTransform は点アンカー (anchorMin == anchorMax == {0,0}) 前提で、
/// offsetMin / offsetMax を画面左上からの絶対ピクセルとして扱う.
class UiWindow final : public OriGine::IComponent {
public:
    UiWindow()           = default;
    ~UiWindow() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    /// ドラッグで移動できるか
    bool movable = true;
    /// 重なり順. 大きいほど手前. UiWindowSystem が付け替える.
    int32_t order = 0;

    /// タイトルバーのエンティティ. ドラッグの判定に使う.
    OriGine::EntityHandle titleBar{};
    /// 内容領域のエンティティ. 中身を足すときの親.
    OriGine::EntityHandle contentArea{};

    // --- 実行時の状態 (UiWindowSystem が書き込む。JSON には保存しない) ---
    bool isDragging = false;
    /// ドラッグ開始時の「カーソル位置 - ウィンドウ左上」. これを保つように動かす.
    OriGine::Vec2f dragGrabOffset{};
};

void to_json(nlohmann::json& _j, const UiWindow& _c);
void from_json(const nlohmann::json& _j, UiWindow& _c);

} // namespace LogGuide

#pragma once

// UiWidgetDesc::name の既定引数 (OriGine::TextAlign) に完全な定義が要るため include する。
#include "component/text/TextComponent.h"
#include "entity/EntityHandle.h"

#include <cstdint>
#include <string>
#include <Vector2.h>
#include <Vector4.h>

namespace OriGine {
class Scene;
class SystemRunner;
} // namespace OriGine

namespace LogGuide {

/// 部品を作るときの共通指定. 親と矩形はどの部品でも要るのでまとめる.
struct UiWidgetDesc {
    OriGine::EntityHandle parent{};
    OriGine::Vec2f anchorMin = {0.0f, 0.0f};
    OriGine::Vec2f anchorMax = {0.0f, 0.0f};
    OriGine::Vec2f offsetMin = {0.0f, 0.0f};
    OriGine::Vec2f offsetMax = {0.0f, 0.0f};
    int32_t renderPriority   = 0;
    std::string name         = "UiWidget"; ///< エンティティ名 (デバッグ用)
};

/// 押せるボタン (UiRect + UiText + TextComponent + UiInteractable + UiHighlight).
/// クリックされたかは UiInteractable::wasClicked を見る.
OriGine::EntityHandle CreateUiButton(OriGine::Scene* _scene, OriGine::SystemRunner* _runner,
                                     const UiWidgetDesc& _desc, const std::string& _label,
                                     float _fontSize = 18.0f);

/// 押せない文字列 (UiText + TextComponent のみ。背景も当たり判定も無い).
OriGine::EntityHandle CreateUiLabel(OriGine::Scene* _scene, OriGine::SystemRunner* _runner,
                                    const UiWidgetDesc& _desc, const std::string& _text,
                                    float _fontSize = 16.0f,
                                    OriGine::TextAlign _align = OriGine::TextAlign::Left);

/// 背景の板 (UiRect のみ). 区切りや、まとまりの背景に使う.
OriGine::EntityHandle CreateUiPanel(OriGine::Scene* _scene, OriGine::SystemRunner* _runner,
                                    const UiWidgetDesc& _desc,
                                    const OriGine::Vec4f& _fillColor);

/// スクロールビューを組み立てた結果.
struct UiScrollViewHandles {
    OriGine::EntityHandle viewport{}; ///< UiScrollView が付いている本体
    OriGine::EntityHandle content{};  ///< 中身を足すときの親
};

/// 縦スクロールできるビューを 1 つ作る (ビューポート/コンテンツ/バーの溝/つまみの 4 エンティティ)。
/// 中身を足すときは、戻り値の content を UiTransform::parent に指定すること.
/// contentHeight (中身の高さ) は子を走査した自動計算をしないので、
/// scene->GetComponent<UiScrollView>(viewport) 経由でアプリが設定すること.
UiScrollViewHandles CreateUiScrollView(OriGine::Scene* _scene, OriGine::SystemRunner* _runner,
                                        const UiWidgetDesc& _desc);

} // namespace LogGuide

#include "ui/UiWidgetBuilder.h"

/// engine
#include "component/text/TextComponent.h"
#include "scene/Scene.h"
#include "system/SystemRunner.h"

/// application
#include "ui/component/UiHighlight.h"
#include "ui/component/UiInteractable.h"
#include "ui/component/UiRect.h"
#include "ui/component/UiScrollView.h"
#include "ui/component/UiText.h"
#include "ui/component/UiTransform.h"
#include "ui/system/UiHighlightSystem.h"
#include "ui/system/UiInteractionSystem.h"
#include "ui/system/UiLayoutSystem.h"
#include "ui/system/UiRenderSystem.h"
#include "ui/system/UiScrollSystem.h"

using namespace OriGine;

namespace LogGuide {

namespace {
/// UiWidgetDesc の内容を UiTransform へ書き写す. どの部品でも同じなので共通化する.
void ApplyDescToTransform(const UiWidgetDesc& _desc, UiTransform& _transform) {
    _transform.parent         = _desc.parent;
    _transform.anchorMin      = _desc.anchorMin;
    _transform.anchorMax      = _desc.anchorMax;
    _transform.offsetMin      = _desc.offsetMin;
    _transform.offsetMax      = _desc.offsetMax;
    _transform.renderPriority = _desc.renderPriority;
}
} // namespace

EntityHandle CreateUiButton(Scene* _scene, SystemRunner* _runner, const UiWidgetDesc& _desc,
                             const std::string& _label, float _fontSize) {
    EntityHandle entity = _scene->CreateEntity(_desc.name);

    _scene->AddComponent<UiTransform>(entity);
    _scene->AddComponent<UiRect>(entity);
    _scene->AddComponent<UiText>(entity);
    _scene->AddComponent<TextComponent>(entity);
    _scene->AddComponent<UiInteractable>(entity);
    _scene->AddComponent<UiHighlight>(entity);

    if (UiTransform* transform = _scene->GetComponent<UiTransform>(entity)) {
        ApplyDescToTransform(_desc, *transform);
    }
    if (UiText* uiText = _scene->GetComponent<UiText>(entity)) {
        uiText->verticalAlign = UiTextVerticalAlign::Middle;
    }
    if (TextComponent* text = _scene->GetComponent<TextComponent>(entity)) {
        text->text     = _label;
        text->fontSize = _fontSize;
        text->align    = TextAlign::Center;
        text->color    = {0.92f, 0.94f, 0.98f, 1.0f};
        text->dirty    = true;
    }
    // UiRect / UiHighlight は既定色のまま (呼び出し側が必要なら後で上書きする)。

    _runner->RegisterEntity<UiLayoutSystem, UiRenderSystem, UiInteractionSystem, UiHighlightSystem>(entity);

    return entity;
}

EntityHandle CreateUiLabel(Scene* _scene, SystemRunner* _runner, const UiWidgetDesc& _desc,
                            const std::string& _text, float _fontSize, TextAlign _align) {
    EntityHandle entity = _scene->CreateEntity(_desc.name);

    _scene->AddComponent<UiTransform>(entity);
    _scene->AddComponent<UiText>(entity);
    _scene->AddComponent<TextComponent>(entity);

    if (UiTransform* transform = _scene->GetComponent<UiTransform>(entity)) {
        ApplyDescToTransform(_desc, *transform);
    }
    if (TextComponent* label = _scene->GetComponent<TextComponent>(entity)) {
        label->text     = _text;
        label->fontSize = _fontSize;
        label->align    = _align;
        label->color    = {0.88f, 0.90f, 0.95f, 1.0f};
        label->dirty    = true;
    }

    // 押せない文字列なので UiInteractionSystem / UiHighlightSystem には登録しない (当たり判定を持たない)。
    _runner->RegisterEntity<UiLayoutSystem, UiRenderSystem>(entity);

    return entity;
}

EntityHandle CreateUiPanel(Scene* _scene, SystemRunner* _runner, const UiWidgetDesc& _desc,
                            const Vec4f& _fillColor) {
    EntityHandle entity = _scene->CreateEntity(_desc.name);

    _scene->AddComponent<UiTransform>(entity);
    _scene->AddComponent<UiRect>(entity);

    if (UiTransform* transform = _scene->GetComponent<UiTransform>(entity)) {
        ApplyDescToTransform(_desc, *transform);
    }
    if (UiRect* rect = _scene->GetComponent<UiRect>(entity)) {
        rect->fillColor = _fillColor;
    }

    // 見た目だけの板なので UiInteractionSystem / UiHighlightSystem には登録しない。
    _runner->RegisterEntity<UiLayoutSystem, UiRenderSystem>(entity);

    return entity;
}

UiScrollViewHandles CreateUiScrollView(Scene* _scene, SystemRunner* _runner, const UiWidgetDesc& _desc) {
    UiScrollViewHandles handles{};

    // --- ビューポート: このコンポーネントが付くエンティティ本体。子孫をここで切る ---
    handles.viewport = _scene->CreateEntity(_desc.name);
    _scene->AddComponent<UiTransform>(handles.viewport);
    _scene->AddComponent<UiRect>(handles.viewport);
    _scene->AddComponent<UiScrollView>(handles.viewport);

    if (UiTransform* viewportTransform = _scene->GetComponent<UiTransform>(handles.viewport)) {
        ApplyDescToTransform(_desc, *viewportTransform);
        viewportTransform->clipChildren = true;
    }
    if (UiRect* viewportRect = _scene->GetComponent<UiRect>(handles.viewport)) {
        // ボタン等と衝突しない、落ち着いた背景色にしておく。
        viewportRect->fillColor   = {0.11f, 0.12f, 0.14f, 1.0f};
        viewportRect->borderWidth = 0.0f;
    }

    UiScrollView* scrollView = _scene->GetComponent<UiScrollView>(handles.viewport);

    // --- content: ビューポートの子。横は全幅ストレッチ、縦は UiScrollSystem が毎フレーム決める ---
    handles.content = _scene->CreateEntity("UiScrollContent");
    _scene->AddComponent<UiTransform>(handles.content);
    if (UiTransform* contentTransform = _scene->GetComponent<UiTransform>(handles.content)) {
        contentTransform->parent    = handles.viewport;
        contentTransform->anchorMin = {0.0f, 0.0f};
        contentTransform->anchorMax = {1.0f, 0.0f};
        contentTransform->offsetMin = {0.0f, 0.0f};
        contentTransform->offsetMax = {0.0f, 0.0f};
    }

    // --- scrollBar: バーの溝。ビューポートの右端に張り付き、縦いっぱいにストレッチする ---
    EntityHandle scrollBar = _scene->CreateEntity("UiScrollBar");
    _scene->AddComponent<UiTransform>(scrollBar);
    _scene->AddComponent<UiRect>(scrollBar);
    if (UiTransform* barTransform = _scene->GetComponent<UiTransform>(scrollBar)) {
        barTransform->parent         = handles.viewport;
        barTransform->anchorMin      = {1.0f, 0.0f};
        barTransform->anchorMax      = {1.0f, 1.0f};
        const float barWidth         = scrollView ? scrollView->scrollBarWidth : 10.0f;
        barTransform->offsetMin      = {-barWidth, 0.0f};
        barTransform->offsetMax      = {0.0f, 0.0f};
        barTransform->renderPriority = 1; // content より手前に描く。
    }
    if (UiRect* barRect = _scene->GetComponent<UiRect>(scrollBar)) {
        barRect->fillColor    = {0.08f, 0.09f, 0.10f, 1.0f};
        barRect->borderWidth  = 0.0f;
        barRect->cornerRadius = {4.0f, 4.0f, 4.0f, 4.0f};
    }

    // --- scrollThumb: 溝の子。ドラッグ判定を持つ ---
    EntityHandle scrollThumb = _scene->CreateEntity("UiScrollThumb");
    _scene->AddComponent<UiTransform>(scrollThumb);
    _scene->AddComponent<UiRect>(scrollThumb);
    _scene->AddComponent<UiInteractable>(scrollThumb);
    _scene->AddComponent<UiHighlight>(scrollThumb);
    if (UiTransform* thumbTransform = _scene->GetComponent<UiTransform>(scrollThumb)) {
        thumbTransform->parent         = scrollBar;
        thumbTransform->anchorMin      = {0.0f, 0.0f};
        thumbTransform->anchorMax      = {1.0f, 0.0f};
        thumbTransform->offsetMin      = {0.0f, 0.0f};
        thumbTransform->offsetMax      = {0.0f, 24.0f}; // UiScrollSystem が毎フレーム書き換える。
        thumbTransform->renderPriority = 1;              // 溝より手前に描く。
    }
    if (UiRect* thumbRect = _scene->GetComponent<UiRect>(scrollThumb)) {
        thumbRect->cornerRadius = {4.0f, 4.0f, 4.0f, 4.0f};
        thumbRect->borderWidth  = 0.0f;
    }
    if (UiHighlight* thumbHighlight = _scene->GetComponent<UiHighlight>(scrollThumb)) {
        thumbHighlight->normalColor  = {0.35f, 0.38f, 0.45f, 1.0f};
        thumbHighlight->hoverColor   = {0.45f, 0.48f, 0.58f, 1.0f};
        thumbHighlight->pressedColor = {0.28f, 0.30f, 0.36f, 1.0f};
    }

    if (scrollView) {
        scrollView->content     = handles.content;
        scrollView->scrollBar   = scrollBar;
        scrollView->scrollThumb = scrollThumb;
    }

    _runner->RegisterEntity<UiLayoutSystem, UiRenderSystem, UiScrollSystem>(handles.viewport);
    _runner->RegisterEntity<UiLayoutSystem>(handles.content);
    _runner->RegisterEntity<UiLayoutSystem, UiRenderSystem>(scrollBar);
    _runner->RegisterEntity<UiLayoutSystem, UiRenderSystem, UiInteractionSystem, UiHighlightSystem>(scrollThumb);

    return handles;
}

} // namespace LogGuide

#include "ui/system/UiHighlightSystem.h"

/// application
#include "ui/component/UiHighlight.h"
#include "ui/component/UiInteractable.h"
#include "ui/component/UiRect.h"

using namespace OriGine;

namespace LogGuide {

void UiHighlightSystem::Initialize() {}

void UiHighlightSystem::Finalize() {}

void UiHighlightSystem::UpdateEntity(const EntityHandle& _entity) {
    UiHighlight* highlight       = GetComponent<UiHighlight>(_entity);
    UiInteractable* interactable = GetComponent<UiInteractable>(_entity);
    UiRect* rect                 = GetComponent<UiRect>(_entity);
    if (!highlight || !interactable || !rect) {
        return;
    }

    // v12: 優先順位は disabled > pressed > hover > selected > normal。
    // 「選択中の行にホバーしたらホバー色」という、選択より上に一時的な状態を優先する自然な挙動になる。
    if (!interactable->enabled) {
        rect->fillColor = highlight->disabledColor;
    } else if (interactable->isPressed && interactable->isHovered) {
        rect->fillColor = highlight->pressedColor;
    } else if (interactable->isHovered) {
        rect->fillColor = highlight->hoverColor;
    } else if (interactable->isSelected) {
        rect->fillColor = highlight->selectedColor;
    } else {
        rect->fillColor = highlight->normalColor;
    }
}

} // namespace LogGuide

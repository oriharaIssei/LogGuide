#include "ui/system/UiHighlightSystem.h"

/// application
#include "ui/component/UiHighlight.h"
#include "ui/component/UiInteractable.h"
#include "ui/component/UiRect.h"

using namespace OriGine;

namespace LogGuide {

void UiHighlightSystem::Initialize() {}

void UiHighlightSystem::Finalize() {}

void UiHighlightSystem::UpdateEntity(EntityHandle _entity) {
    UiHighlight* highlight       = GetComponent<UiHighlight>(_entity);
    UiInteractable* interactable = GetComponent<UiInteractable>(_entity);
    UiRect* rect                 = GetComponent<UiRect>(_entity);
    if (!highlight || !interactable || !rect) {
        return;
    }

    if (!interactable->enabled) {
        rect->fillColor = highlight->disabledColor;
    } else if (interactable->isPressed && interactable->isHovered) {
        rect->fillColor = highlight->pressedColor;
    } else if (interactable->isHovered) {
        rect->fillColor = highlight->hoverColor;
    } else {
        rect->fillColor = highlight->normalColor;
    }
}

} // namespace LogGuide

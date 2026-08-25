#include "ui/UiRegistration.h"

/// ECS
#include "component/ComponentRegistry.h"
#include "system/SystemRegistry.h"

/// engine (v2: テキスト描画は engine の TextComponent / TextRenderSystem をそのまま使う)
#include "component/text/TextComponent.h"
#include "system/text/TextRenderSystem.h"

/// application
#include "ui/component/UiHighlight.h"
#include "ui/component/UiInteractable.h"
#include "ui/component/UiRect.h"
#include "ui/component/UiText.h"
#include "ui/component/UiTransform.h"
#include "ui/system/UiHighlightSystem.h"
#include "ui/system/UiInteractionSystem.h"
#include "ui/system/UiLayoutSystem.h"
#include "ui/system/UiRectRenderSystem.h"

using namespace OriGine;

namespace LogGuide {

void RegisterUiComponents() {
    ComponentRegistry* componentRegistry = ComponentRegistry::GetInstance();
    componentRegistry->RegisterComponent<UiTransform>();
    componentRegistry->RegisterComponent<UiRect>();
    componentRegistry->RegisterComponent<UiText>();
    componentRegistry->RegisterComponent<UiInteractable>();
    componentRegistry->RegisterComponent<UiHighlight>();
    // engine のコンポーネント。登録しないと AddComponent がヌル参照で落ちる。
    componentRegistry->RegisterComponent<OriGine::TextComponent>();
}

void RegisterUiSystems() {
    SystemRegistry* systemRegistry = SystemRegistry::GetInstance();
    systemRegistry->RegisterSystem<UiLayoutSystem>();
    systemRegistry->RegisterSystem<UiRectRenderSystem>();
    systemRegistry->RegisterSystem<UiInteractionSystem>();
    systemRegistry->RegisterSystem<UiHighlightSystem>();
    // engine のシステムをアプリ側から登録して使う。
    systemRegistry->RegisterSystem<OriGine::TextRenderSystem>();
}

} // namespace LogGuide

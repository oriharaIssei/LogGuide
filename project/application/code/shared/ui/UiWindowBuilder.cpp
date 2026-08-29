#include "ui/UiWindowBuilder.h"

/// engine
#include "component/text/TextComponent.h"
#include "scene/Scene.h"
#include "system/SystemRunner.h"

/// application
#include "ui/component/UiHighlight.h"
#include "ui/component/UiInteractable.h"
#include "ui/component/UiRect.h"
#include "ui/component/UiText.h"
#include "ui/component/UiTransform.h"
#include "ui/component/UiWindow.h"
#include "ui/system/UiHighlightSystem.h"
#include "ui/system/UiInteractionSystem.h"
#include "ui/system/UiLayoutSystem.h"
#include "ui/system/UiRenderSystem.h"
#include "ui/system/UiWindowSystem.h"

/// stl
#include <cstdint>

using namespace OriGine;

namespace LogGuide {

namespace {
/// ウィンドウ 1 枚が占める優先度の幅. UiWindowSystem::kWindowPriorityBand と同じ値にすること
/// (前面化のたびに UiWindowSystem 側で振り直されるが、生成直後の初期値もここで揃えておく)。
constexpr int32_t kInitialPriorityBand = 1000;

/// タイトルバーのボタン (閉じる/切り離し) の一辺の長さ (px). タイトルバー高さ - 8px の正方形。
constexpr float kTitleButtonSize = kUiWindowTitleBarHeight - 8.0f;
/// ボタンとタイトルバー右端、ボタン同士の間隔 (px).
constexpr float kTitleButtonMargin = 4.0f;
/// ボタンをタイトルバー内で縦方向に中央寄せするための上端オフセット (px).
constexpr float kTitleButtonOffsetY = (kUiWindowTitleBarHeight - kTitleButtonSize) * 0.5f;

/// タイトルバーの子として、右寄せの正方形ボタンを 1 つ作る.
/// anchorMin == anchorMax == {1, 0} (タイトルバーの右上を点アンカーにする) にしておくことで、
/// ウィンドウをリサイズしてタイトルバーの幅が変わってもボタンは右端に追従する.
/// _rightOffset はタイトルバー右端からボタン右端までの距離 (px). 複数のボタンを並べるときに使う.
EntityHandle CreateTitleBarButton(
    Scene* _scene,
    SystemRunner* _runner,
    const EntityHandle& _titleBar,
    const std::string& _label,
    float _rightOffset) {
    EntityHandle button = _scene->CreateEntity("UiWindowTitleBarButton");

    _scene->AddComponent<UiTransform>(button);
    _scene->AddComponent<UiRect>(button);
    _scene->AddComponent<UiText>(button);
    _scene->AddComponent<TextComponent>(button);
    _scene->AddComponent<UiInteractable>(button);
    _scene->AddComponent<UiHighlight>(button);

    if (UiTransform* transform = _scene->GetComponent<UiTransform>(button)) {
        transform->parent    = _titleBar;
        transform->anchorMin = {1.0f, 0.0f};
        transform->anchorMax = {1.0f, 0.0f};
        transform->offsetMin = {-(_rightOffset + kTitleButtonSize), kTitleButtonOffsetY};
        transform->offsetMax = {-_rightOffset, kTitleButtonOffsetY + kTitleButtonSize};
        // タイトルバー (renderPriority 1) より必ず手前に来るよう、タイトルバーからの加算分を積む。
        transform->renderPriority = 1;
    }
    if (UiText* uiText = _scene->GetComponent<UiText>(button)) {
        uiText->padding       = {0.0f, 0.0f, 0.0f, 0.0f};
        uiText->verticalAlign = UiTextVerticalAlign::Middle;
    }
    if (TextComponent* label = _scene->GetComponent<TextComponent>(button)) {
        label->text     = _label;
        label->fontSize = 14.0f;
        label->align    = TextAlign::Center;
        label->color    = {0.92f, 0.94f, 0.98f, 1.0f};
        label->dirty    = true;
    }
    // UiRect / UiHighlight は既定色のまま (counterButton_ と同じく、フォーカスを引きすぎない普通のボタン扱い)。

    _runner->RegisterEntity<UiLayoutSystem, UiRenderSystem,
        UiInteractionSystem, UiHighlightSystem>(button);

    return button;
}
} // namespace

UiWindowHandles CreateUiWindow(
    Scene* _scene,
    SystemRunner* _runner,
    const std::string& _title,
    const Vec2f& _position,
    const Vec2f& _size,
    int32_t _order) {
    UiWindowHandles handles{};

    // --- エンティティを先に全部作る ---
    handles.root        = _scene->CreateEntity("UiWindowRoot");
    handles.titleBar    = _scene->CreateEntity("UiWindowTitleBar");
    handles.contentArea = _scene->CreateEntity("UiWindowContentArea");

    // --- ルート: ウィンドウ本体の枠。点アンカーで、offsetMin/Max を絶対ピクセルとして扱う ---
    _scene->AddComponent<UiTransform>(handles.root);
    _scene->AddComponent<UiRect>(handles.root);
    _scene->AddComponent<UiWindow>(handles.root);
    // ウィンドウ全体を覆うヒット領域。これが無いと、手前のウィンドウの上でクリックしたつもりが
    // 奥のウィンドウの中身に当たってしまう（クリック抜け）。
    // ルートの resolvedPriority は自分の子より小さく、奥のウィンドウの要素より大きいので、
    // 「手前のウィンドウが奥を覆う」「自分の子はルートより優先される」の両方が成り立つ。
    _scene->AddComponent<UiInteractable>(handles.root);

    if (UiTransform* rootTransform = _scene->GetComponent<UiTransform>(handles.root)) {
        rootTransform->anchorMin      = {0.0f, 0.0f};
        rootTransform->anchorMax      = {0.0f, 0.0f};
        rootTransform->offsetMin      = _position;
        rootTransform->offsetMax      = {_position[X] + _size[X], _position[Y] + _size[Y]};
        rootTransform->clipChildren   = true;
        rootTransform->renderPriority = _order * kInitialPriorityBand;
    }
    if (UiRect* rootRect = _scene->GetComponent<UiRect>(handles.root)) {
        rootRect->fillColor    = {0.13f, 0.15f, 0.18f, 1.0f};
        rootRect->borderColor  = {0.45f, 0.48f, 0.55f, 1.0f};
        rootRect->cornerRadius = {6.0f, 6.0f, 6.0f, 6.0f};
        rootRect->borderWidth  = 1.5f;
    }
    // --- タイトルバー: ルートの子。上端に横いっぱい。ドラッグの判定に UiInteractable を使う ---
    _scene->AddComponent<UiTransform>(handles.titleBar);
    _scene->AddComponent<UiRect>(handles.titleBar);
    _scene->AddComponent<UiText>(handles.titleBar);
    _scene->AddComponent<TextComponent>(handles.titleBar);
    _scene->AddComponent<UiInteractable>(handles.titleBar);
    _scene->AddComponent<UiHighlight>(handles.titleBar);

    if (UiTransform* titleTransform = _scene->GetComponent<UiTransform>(handles.titleBar)) {
        titleTransform->parent         = handles.root;
        titleTransform->anchorMin      = {0.0f, 0.0f};
        titleTransform->anchorMax      = {1.0f, 0.0f};
        titleTransform->offsetMin      = {0.0f, 0.0f};
        titleTransform->offsetMax      = {0.0f, kUiWindowTitleBarHeight};
        titleTransform->renderPriority = 1;
    }
    if (UiRect* titleRect = _scene->GetComponent<UiRect>(handles.titleBar)) {
        // fillColor は UiHighlightSystem が毎フレーム上書きするので、ここでは形だけ整える
        // (ルートの丸みに合わせて上側だけ丸め、枠線は持たせない)。
        titleRect->cornerRadius = {6.0f, 6.0f, 0.0f, 0.0f};
        titleRect->borderWidth  = 0.0f;
    }
    if (UiText* titleUiText = _scene->GetComponent<UiText>(handles.titleBar)) {
        titleUiText->padding       = {10.0f, 0.0f, 10.0f, 0.0f};
        titleUiText->verticalAlign = UiTextVerticalAlign::Middle;
    }
    if (TextComponent* titleLabel = _scene->GetComponent<TextComponent>(handles.titleBar)) {
        titleLabel->text     = _title;
        titleLabel->fontSize = 15.0f;
        titleLabel->align    = TextAlign::Left;
        titleLabel->color    = {0.92f, 0.94f, 0.98f, 1.0f};
        titleLabel->dirty    = true;
    }
    if (UiHighlight* titleHighlight = _scene->GetComponent<UiHighlight>(handles.titleBar)) {
        // 掴めることが分かるよう、ウィンドウ本体よりはっきりした色にしておく。
        titleHighlight->normalColor   = {0.22f, 0.24f, 0.30f, 1.0f};
        titleHighlight->hoverColor    = {0.30f, 0.33f, 0.41f, 1.0f};
        titleHighlight->pressedColor  = {0.17f, 0.19f, 0.24f, 1.0f};
        titleHighlight->disabledColor = {0.20f, 0.20f, 0.22f, 1.0f};
    }

    // --- タイトルバーのボタン: 右端から [閉じる][切り離し] の順に並べる ---
    // 表示/有効の出し分け (closable / resizable) は毎フレーム UiWindowSystem が行うので、
    // ここでは closable == false でも常に両方作る。
    handles.closeButton = CreateTitleBarButton(
        _scene, _runner, handles.titleBar, "×", kTitleButtonMargin);
    handles.detachButton = CreateTitleBarButton(
        _scene, _runner, handles.titleBar, "⧉",
        kTitleButtonMargin * 2.0f + kTitleButtonSize);

    if (UiWindow* window = _scene->GetComponent<UiWindow>(handles.root)) {
        window->order        = _order;
        window->titleBar     = handles.titleBar;
        window->contentArea  = handles.contentArea;
        window->closeButton  = handles.closeButton;
        window->detachButton = handles.detachButton;
    }

    // --- 内容領域: ルートの子。タイトルバーの下の残り全部。枠は付けない (枠はルートが描く) ---
    _scene->AddComponent<UiTransform>(handles.contentArea);

    if (UiTransform* contentTransform = _scene->GetComponent<UiTransform>(handles.contentArea)) {
        contentTransform->parent         = handles.root;
        contentTransform->anchorMin      = {0.0f, 0.0f};
        contentTransform->anchorMax      = {1.0f, 1.0f};
        contentTransform->offsetMin      = {0.0f, kUiWindowTitleBarHeight};
        contentTransform->offsetMax      = {0.0f, 0.0f};
        contentTransform->clipChildren   = true;
        contentTransform->renderPriority = 1;
    }

    // --- システムへの登録 ---
    _runner->RegisterEntity<UiLayoutSystem, UiRenderSystem, UiInteractionSystem,
        UiWindowSystem>(handles.root);
    _runner->RegisterEntity<UiLayoutSystem, UiRenderSystem,
        UiInteractionSystem, UiHighlightSystem>(handles.titleBar);
    _runner->RegisterEntity<UiLayoutSystem>(handles.contentArea);

    return handles;
}

void HideUiWindowChrome(Scene* _scene, const UiWindow& _window) {
    // v10 の DetachWindow() と v14 の DockUiWindow() で共通して使う (タイトルバーの役割を
    // 他の仕組み (OS の枠 / タブ) が肩代わりするので、自作タイトルバーとその子ボタンを隠し、
    // 内容領域をウィンドウ上端まで広げる)。
    if (UiTransform* titleTransform = _scene->GetComponent<UiTransform>(_window.titleBar)) {
        titleTransform->visible = false;
    }
    if (UiTransform* closeTransform = _scene->GetComponent<UiTransform>(_window.closeButton)) {
        closeTransform->visible = false;
    }
    if (UiTransform* detachTransform = _scene->GetComponent<UiTransform>(_window.detachButton)) {
        detachTransform->visible = false;
    }
    if (UiTransform* contentTransform = _scene->GetComponent<UiTransform>(_window.contentArea)) {
        contentTransform->offsetMin = {0.0f, 0.0f};
    }
}

void ShowUiWindowChrome(Scene* _scene, const UiWindow& _window) {
    if (UiTransform* titleTransform = _scene->GetComponent<UiTransform>(_window.titleBar)) {
        titleTransform->visible = true;
    }
    if (UiTransform* detachTransform = _scene->GetComponent<UiTransform>(_window.detachButton)) {
        detachTransform->visible = true;
    }
    // closeButton の visible は closable に応じて UiWindowSystem が毎フレーム設定し直す
    // (タイトルバーが見えているときだけ) ので、ここでは明示的に戻さない。
    if (UiTransform* contentTransform = _scene->GetComponent<UiTransform>(_window.contentArea)) {
        contentTransform->offsetMin = {0.0f, kUiWindowTitleBarHeight};
    }
}

} // namespace LogGuide

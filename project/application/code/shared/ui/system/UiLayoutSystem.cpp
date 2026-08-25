#include "ui/system/UiLayoutSystem.h"

/// engine
#include "Engine.h"
#include "text/BitmapFont.h"
#include "text/FontManager.h"
#include "winApp/WinApp.h"

/// application
#include "ui/component/UiText.h"
#include "ui/component/UiTransform.h"

/// stl
#include <algorithm>

using namespace OriGine;

namespace LogGuide {

void UiLayoutSystem::Initialize() {}

void UiLayoutSystem::Finalize() {}

void UiLayoutSystem::Update() {
    if (entities_.empty()) {
        return;
    }
    EraseDeadEntity();

    // 0 は「未解決」を表すので 1 から始める
    ++frameCounter_;

    // 先に全部の矩形を確定させる。親子関係があるので順序は再帰側が面倒を見る。
    for (const auto& entity : entities_) {
        ResolveTransform(entity, 0);
    }
    // 矩形が確定してからテキストを置く。
    for (const auto& entity : entities_) {
        LayoutText(entity);
    }
}

UiTransform* UiLayoutSystem::ResolveTransform(EntityHandle _entity, int32_t _depth) {
    UiTransform* transform = GetComponent<UiTransform>(_entity);
    if (!transform) {
        return nullptr;
    }
    // このフレームで解決済みなら何もしない
    if (transform->resolvedFrame == frameCounter_) {
        return transform;
    }
    // 親をたどる前に印を付ける。こうしておくと、親子が循環していても
    // 「解決済み」として打ち切られるので無限再帰にならない（値は 1 フレーム古くなる）。
    transform->resolvedFrame = frameCounter_;

    Vec2f parentMin{};
    Vec2f parentMax{};
    Vec2f clipMin{};
    Vec2f clipMax{};

    UiTransform* parent = nullptr;
    if (_depth < kMaxHierarchyDepth && transform->parent.IsValid()) {
        parent = ResolveTransform(transform->parent, _depth + 1);
    }

    if (parent != nullptr) {
        parentMin = parent->resolvedMin;
        parentMax = parent->resolvedMax;

        if (parent->clipChildren) {
            // 親の矩形と、親自身が受け継いだクリップの共通部分で子を切る
            clipMin = {std::max(parent->clipMin[X], parent->resolvedMin[X]),
                       std::max(parent->clipMin[Y], parent->resolvedMin[Y])};
            clipMax = {std::min(parent->clipMax[X], parent->resolvedMax[X]),
                       std::min(parent->clipMax[Y], parent->resolvedMax[Y])};
        } else {
            clipMin = parent->clipMin;
            clipMax = parent->clipMax;
        }
    } else {
        // 親が無ければ画面全体が親矩形。クリップも画面全体。
        WinApp* window = Engine::GetInstance()->GetWinApp();
        parentMin      = {0.0f, 0.0f};
        parentMax      = {static_cast<float>(window->GetWidth()),
                          static_cast<float>(window->GetHeight())};
        clipMin        = parentMin;
        clipMax        = parentMax;
    }

    const Vec2f parentSize = {parentMax[X] - parentMin[X], parentMax[Y] - parentMin[Y]};

    transform->resolvedMin = {
        parentMin[X] + parentSize[X] * transform->anchorMin[X] + transform->offsetMin[X],
        parentMin[Y] + parentSize[Y] * transform->anchorMin[Y] + transform->offsetMin[Y]};
    transform->resolvedMax = {
        parentMin[X] + parentSize[X] * transform->anchorMax[X] + transform->offsetMax[X],
        parentMin[Y] + parentSize[Y] * transform->anchorMax[Y] + transform->offsetMax[Y]};

    transform->clipMin = clipMin;
    transform->clipMax = clipMax;

    return transform;
}

void UiLayoutSystem::LayoutText(EntityHandle _entity) {
    UiTransform* transform = GetComponent<UiTransform>(_entity);
    if (!transform) {
        return;
    }

    // --- v2: UiText + TextComponent があれば矩形内の配置を計算する ---
    UiText* uiText      = GetComponent<UiText>(_entity);
    TextComponent* text = GetComponent<TextComponent>(_entity);
    if (!uiText || !text) {
        return;
    }

    // UiTransform の可視状態をテキストにも伝える
    text->visible = transform->visible;
    // 矩形との前後関係もテキストに揃える。TextComponent::renderPriority はテキスト同士の
    // 順序を決めるためのもので、矩形との前後関係はシステムの priority (Render カテゴリ内の
    // UiRectRenderSystem/TextRenderSystem の登録順) で既に決まっている。
    text->renderPriority = transform->renderPriority;

    // 内容領域 = 矩形から padding を引いたもの
    const float contentMinX = transform->resolvedMin[X] + uiText->padding[X];
    const float contentMinY = transform->resolvedMin[Y] + uiText->padding[Y];
    const float contentMaxX = transform->resolvedMax[X] - uiText->padding[Z];
    const float contentMaxY = transform->resolvedMax[Y] - uiText->padding[W];
    const float contentW    = contentMaxX - contentMinX;
    const float contentH    = contentMaxY - contentMinY;

    // 幅が変わると折り返しと中央寄せの結果が変わるので、レイアウトをやり直させる。
    // TextComponent::dirty は自動では立たないので、書き換えたらこちらで立てる。
    if (contentW != uiText->lastContentSize[X] || contentH != uiText->lastContentSize[Y]) {
        text->maxWidth          = (contentW > 0.0f) ? contentW : 0.0f;
        text->dirty             = true;
        uiText->lastContentSize = {contentW, contentH};
    }

    // 縦位置を出すにはテキストの高さが要る。engine 側に縦方向の中央寄せは無いので自前で計測する。
    //
    // 毎フレーム計測する。TextComponent::dirty は Render カテゴリの TextRenderSystem が
    // 消費してしまうため、Movement カテゴリのここでは「前回から変わったか」の判定に使えない
    // (文字列の書き換えが Render より後に入ると、次にここへ来たときには既に落とされている)。
    // 計測はラベル程度の長さなら数十グリフ分の演算で済み、scratchLayout_ の内部バッファも
    // 使い回されるので、変化検出の状態を別途持つより毎回測る方が単純で確実。
    // TextRenderSystem 側にも dirty を消費させたいので、_consumeDirty は false にする。
    BitmapFont* font = FontManager::GetInstance()->GetFont(text->fontHandle);
    if (font) {
        // scratchLayout_ は使い回しなので、前のエンティティの結果で
        // UpdateLayout が早期 return しないよう valid を落としてから呼ぶ。
        scratchLayout_.valid = false;
        layout_.UpdateLayout(*font, *text, scratchLayout_, false);
        uiText->measuredHeight = scratchLayout_.boundingSize[Y];
    }

    float posY = contentMinY;
    switch (uiText->verticalAlign) {
    case UiTextVerticalAlign::Middle:
        posY = contentMinY + (contentH - uiText->measuredHeight) * 0.5f;
        break;
    case UiTextVerticalAlign::Bottom:
        posY = contentMaxY - uiText->measuredHeight;
        break;
    case UiTextVerticalAlign::Top:
    default:
        break;
    }

    // 位置が動いたときも TextRenderSystem のキャッシュを作り直させる必要がある。
    // 変化していないときに dirty を立てるとキャッシュが無意味になるので、比較してから立てる。
    if (text->position[X] != contentMinX || text->position[Y] != posY) {
        text->position = {contentMinX, posY};
        text->dirty    = true;
    }
}

} // namespace LogGuide

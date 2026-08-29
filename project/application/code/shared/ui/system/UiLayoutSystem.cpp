#include "ui/system/UiLayoutSystem.h"

/// engine
#include "Engine.h"
#include "text/BitmapFont.h"
#include "text/FontManager.h"
#include "winApp/WinApp.h"

/// application
#include "ui/component/UiText.h"
#include "ui/component/UiTransform.h"
#include "ui/native/NativeWindowManager.h"

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

UiTransform* UiLayoutSystem::ResolveTransform(const EntityHandle& _entity, int32_t _depth) {
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

    // 「親を設定していない (ルート)」と「親を設定しているのに解決できない (親が既に破棄された
    // 孤児)」を区別する。後者をこのまま素通りさせると、下の else 節で「親矩形 = サーフェス全体」
    // として再配置されてしまい、巨大な部品として描画・ヒットテストされてしまう
    // (原因B。v14 で発覚したドックタブの孤児化バグ)。孤児は非表示・矩形ゼロにして、
    // 描画/ヒットテスト (どちらも resolvedVisible を見る) から確実に外す。
    // ルート (ウィンドウのルートやドックスペースのルートなど parent 未設定の要素) は
    // これまで通り画面全体を親矩形として扱われ続ける。
    if (transform->parent.IsValid() && parent == nullptr) {
        transform->resolvedVisible  = false;
        transform->resolvedMin      = {0.0f, 0.0f};
        transform->resolvedMax      = {0.0f, 0.0f};
        transform->clipMin          = {0.0f, 0.0f};
        transform->clipMax          = {0.0f, 0.0f};
        transform->resolvedPriority = 0;
        return transform;
    }

    // v10: 描画先サーフェス。親がいれば親の値を継承する (子が親と違うサーフェスに出ることは無い)。
    // 親が無ければウィンドウのルートなので、自分の surfaceId をそのまま使う。
    int32_t surfaceId = 0;
    if (parent != nullptr) {
        surfaceId = parent->resolvedSurfaceId;
    } else {
        surfaceId = transform->surfaceId;
    }
    transform->resolvedSurfaceId = surfaceId;

    // サーフェスが無効 (追加ウィンドウが閉じられた等) なら、UiRenderSystem / UiInteractionSystem /
    // UiWindowSystem 側で visible 相当として除外できるようにしておく。
    const bool surfaceValid      = surfaceProvider_ ? surfaceProvider_->IsSurfaceValid(surfaceId) : true;
    // 親が隠れているなら子孫もまとめて隠す。
    // UiTransform::visible は「自分を描くか」の指定でしかなく、子へは伝播しない。
    // そのため、器 (ウィンドウのルートやタイトルバー) だけを隠しても中身が描かれ続け、
    // ヒットテストにも引っかかり続ける。v14 で「非アクティブなタブのウィンドウの
    // ルートを隠しても中のラベルが消えず、タブを切り替えても見た目が変わらない」
    // という形で表面化した。visible を落とすたびに子を 1 つずつ手で隠して回るのは
    // 破綻するので、階層で解決するこの場所で伝播させる。
    // 親は再帰で先に解決済み (resolvedFrame でメモ化されている) なので、
    // ここで parent->resolvedVisible を見てよい。
    transform->resolvedVisible = transform->visible && surfaceValid
                              && (parent == nullptr || parent->resolvedVisible);

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
        // 親が無ければ自分のサーフェス全体が親矩形。クリップもサーフェス全体。
        // NativeWindowManager が未注入なら、従来通り WinApp (メインウィンドウ) のサイズを使う。
        Vec2f surfaceSize;
        if (surfaceProvider_ != nullptr) {
            surfaceSize = surfaceProvider_->GetSurfaceSize(surfaceId);
        } else {
            WinApp* window = Engine::GetInstance()->GetWinApp();
            surfaceSize     = {static_cast<float>(window->GetWidth()),
                               static_cast<float>(window->GetHeight())};
        }
        parentMin = {0.0f, 0.0f};
        parentMax = surfaceSize;
        clipMin   = parentMin;
        clipMax   = parentMax;
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

    // ウィンドウがクリックで前後するため、静的な renderPriority だけでは前後関係を表せない。
    // 親の解決済み優先度に自分の renderPriority を足した値を、描画/ヒットテストの前後判定に使う。
    transform->resolvedPriority =
        (parent != nullptr ? parent->resolvedPriority : 0) + transform->renderPriority;

    return transform;
}

void UiLayoutSystem::LayoutText(const EntityHandle& _entity) {
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

    // UiTransform の実効可視状態 (サーフェス無効化を含む) をテキストにも伝える
    text->visible = transform->resolvedVisible;
    // 前後関係をテキストにも揃える。
    // UiRenderSystem は矩形とグリフをこの値で一緒に並べ替えてから描くので、
    // ここに入れた値がそのまま「他の要素との前後関係」になる。
    // 同じ値なら矩形が先に描かれるため、自分の背景の上に自分の文字が乗る。
    text->renderPriority = transform->resolvedPriority;

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

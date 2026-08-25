#pragma once

#include "system/ISystem.h"

/// engine (TextLayoutSystem は ISystem ではないステートレスなヘルパなので自前で持つ.
/// TextLayoutResult は component/text/TextComponent.h の中で定義されている)
#include "component/text/TextComponent.h"
#include "system/text/TextLayoutSystem.h"

/// application (ResolveTransform() が UiTransform* を返す. ポインタの宣言だけなら前方宣言でも
/// 足りるが、過去に前方宣言止まりでビルドが落ちた例があるため、定義のあるヘッダを直接 include して
/// 自己完結させておく)
#include "ui/component/UiTransform.h"

/// stl
#include <cstdint>

namespace LogGuide {

/// UiTransform のアンカー指定から、実際の矩形 (resolvedMin/resolvedMax) を計算する.
/// v4 で親子関係とクリップ矩形の伝播に対応した。UiTransform::parent が無効なハンドルなら
/// 親矩形は画面全体になる (v1〜v3 の結果と完全に互換).
/// 親を子より先に解決する必要があるため、UpdateEntity() ではなく Update() をオーバーライドし、
/// 自前で階層を再帰して解決する。
/// 同じエンティティが UiText + TextComponent も持っていれば、矩形が確定したあとに
/// その中でのテキストの位置 (TextComponent::position/maxWidth) も併せて計算する.
class UiLayoutSystem final : public OriGine::ISystem {
public:
    UiLayoutSystem() : OriGine::ISystem(OriGine::SystemCategory::Movement) {}
    ~UiLayoutSystem() override = default;

    void Initialize() override;
    void Finalize() override;
    /// 親を先に解決する必要があるため、UpdateEntity() ではなく Update() をオーバーライドする.
    void Update() override;

private:
    /// 階層をたどって矩形とクリップ矩形を確定させる. 解決済みなら何もしない.
    /// 戻り値は解決後の UiTransform（対象または親に UiTransform が無ければ nullptr）.
    LogGuide::UiTransform* ResolveTransform(OriGine::EntityHandle _entity, int32_t _depth);
    /// 矩形が確定したあとに TextComponent の位置を決める.
    void LayoutText(OriGine::EntityHandle _entity);

    /// 循環参照や過剰なネストで無限再帰にならないよう深さを制限する.
    static constexpr int32_t kMaxHierarchyDepth = 32;

    /// 「このフレームで解決済み」を表す世代番号. 0 は未解決を意味するので 1 から始める.
    uint32_t frameCounter_ = 0;

    /// 縦位置を決めるための事前計測に使う. TextLayoutSystem は ISystem ではなく
    /// ステートレスなヘルパなので、自分でインスタンスを持ってよい.
    OriGine::TextLayoutSystem layout_;
    /// 計測結果の使い回し先. エンティティをまたいで再利用する.
    OriGine::TextLayoutResult scratchLayout_;
};

} // namespace LogGuide

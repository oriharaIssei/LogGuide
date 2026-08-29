#pragma once

#include "component/IComponent.h"

// childA / childB / splitter / tabBar / contentArea / windows (EntityHandle) をメンバとして
// 値で持つために定義が要る (component/IComponent.h 経由で間接的には入っているが、
// 直接使うヘッダなので明示的に include する)。
#include "entity/EntityHandle.h"

#include <cstdint>
#include <string>
#include <vector>

namespace LogGuide {

/// ドックノードの分割方向.
enum class UiDockSplit : uint8_t {
    None = 0,   ///< 葉ノード. ウィンドウ (タブ) を持つ
    Horizontal, ///< 左右に分割 (境界は縦線)
    Vertical,   ///< 上下に分割 (境界は横線)
};

/// ImGui のドッキングに相当するツリーの 1 ノード.
/// ツリー構造そのものは UiTransform::parent で表す (childA/childB/splitter は
/// いずれもこのノードの UiTransform を親にする)。矩形の計算とクリップは UiLayoutSystem に
/// 任せ、このコンポーネント自身はレイアウト計算を持たない (UiDockSystem が毎フレーム
/// splitRatio からアンカーを決めるだけ)。
///
/// 分割ノードのとき: childA / childB / splitter の 3 エンティティを子に持つ。
/// 葉ノードのとき: tabBar / contentArea の 2 エンティティを子に持ち、windows に入っている
/// ウィンドウのルートを contentArea の子として表示する (タブで切り替える)。
class UiDockNode final : public OriGine::IComponent {
public:
    UiDockNode()           = default;
    ~UiDockNode() override = default;

    void Initialize(OriGine::Scene* _scene, const OriGine::EntityHandle& _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, const OriGine::EntityHandle& _owner, const std::string& _parentLabel) override;

    // --- 設定 (JSON に保存する。Stage 5 のレイアウト保存がここに効く) ---
    UiDockSplit split = UiDockSplit::None;
    /// 分割ノードのとき、最初の子 (childA) が占める割合 (0.05〜0.95)
    float splitRatio = 0.5f;
    /// 葉ノードのとき、今表示しているタブの添字
    int32_t activeTab = 0;

    // --- 構成エンティティ ---
    /// 分割ノードのとき: 子ノード 2 つとスプリッター
    OriGine::EntityHandle childA{};
    OriGine::EntityHandle childB{};
    OriGine::EntityHandle splitter{};
    /// 葉ノードのとき: タブバーと内容領域
    OriGine::EntityHandle tabBar{};
    OriGine::EntityHandle contentArea{};

    // --- 実行時の状態 (JSON に保存しない) ---
    /// この葉ノードに入っているウィンドウのルート (タブの並び順)
    std::vector<OriGine::EntityHandle> windows;
    /// このフレームに作ったタブのボタン (毎フレーム作り直さない。windows が変わったときだけ)
    std::vector<OriGine::EntityHandle> tabButtons;
    bool tabsDirty = true; ///< windows を書き換えたら立てる
    bool isDraggingSplitter = false;
    float splitterGrabOffset = 0.0f;
};

void to_json(nlohmann::json& _j, const UiDockNode& _c);
void from_json(const nlohmann::json& _j, UiDockNode& _c);

} // namespace LogGuide

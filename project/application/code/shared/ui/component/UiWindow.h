#pragma once

#include "component/IComponent.h"

// titleBar / contentArea (EntityHandle) をメンバとして値で持つために定義が要る
// (component/IComponent.h 経由で間接的には入っているが、直接使うヘッダなので明示的に include する)。
#include "entity/EntityHandle.h"

#include <Vector2.h>
#include <cstdint>
#include <string>

namespace LogGuide {

/// 移動できるウィンドウ.
/// ウィンドウは以下の 3 つのエンティティで構成する（UiWindowBuilder が組み立てる）:
///   ルート     … UiTransform + UiRect(枠) + UiWindow + clipChildren
///     タイトルバー … UiTransform + UiRect + UiText + TextComponent + UiInteractable + UiHighlight
///       閉じるボタン … UiTransform + UiRect + UiText + TextComponent + UiInteractable + UiHighlight
///       切り離しボタン … 同上
///     内容領域   … UiTransform + clipChildren （中身はこの子にする）
///
/// ルートの UiTransform は点アンカー (anchorMin == anchorMax == {0,0}) 前提で、
/// offsetMin / offsetMax を画面左上からの絶対ピクセルとして扱う.
///
/// 閉じる/切り離しボタンが押されたときの後始末はこのコンポーネント（や UiWindowSystem）の責務ではない.
/// contentArea を親にしてアプリが足した中身は UiWindowSystem からは辿れないため、
/// closeRequested / detachRequested を立てるところまでで止め、実際にエンティティを破棄する（あるいは
/// v9/v10 で別ウィンドウへ移す）のはアプリ側が毎フレーム見て行う.
class UiWindow final : public OriGine::IComponent {
public:
    UiWindow()           = default;
    ~UiWindow() override = default;

    void Initialize(OriGine::Scene* _scene, const OriGine::EntityHandle& _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, const OriGine::EntityHandle& _owner, const std::string& _parentLabel) override;

    /// リサイズの縁判定に使うビット. 角は 2 ビット立つ.
    static constexpr uint32_t kEdgeLeft   = 1u;
    static constexpr uint32_t kEdgeRight  = 2u;
    static constexpr uint32_t kEdgeTop    = 4u;
    static constexpr uint32_t kEdgeBottom = 8u;

    // --- 設定 (JSON に保存する) ---
    /// ドラッグで移動できるか
    bool movable = true;
    /// 重なり順. 大きいほど手前. UiWindowSystem が付け替える.
    int32_t order = 0;
    /// 閉じるボタンを出すか
    bool closable = true;
    /// 縁のドラッグでリサイズできるか
    bool resizable = true;
    /// リサイズの下限サイズ
    OriGine::Vec2f minSize = {200.0f, 120.0f};

    // --- 構成エンティティ ---
    /// タイトルバーのエンティティ. ドラッグの判定に使う.
    OriGine::EntityHandle titleBar{};
    /// 内容領域のエンティティ. 中身を足すときの親.
    OriGine::EntityHandle contentArea{};
    /// 閉じるボタンのエンティティ. タイトルバーの子.
    OriGine::EntityHandle closeButton{};
    /// 切り離しボタンのエンティティ (v9/v10 で実際の切り離しに使う。今はボタンだけ). タイトルバーの子.
    OriGine::EntityHandle detachButton{};

    // --- 実行時の状態 (UiWindowSystem が書き込む。JSON には保存しない) ---
    bool isDragging = false;
    /// ドラッグ開始時の「カーソル位置 - ウィンドウ左上」. これを保つように動かす.
    OriGine::Vec2f dragGrabOffset{};

    /// 縁を掴んでリサイズ中か
    bool isResizing = false;
    /// リサイズ中に掴んでいる縁. kEdgeLeft 等の OR
    uint32_t resizeEdges = 0;
    /// リサイズ開始時のカーソル位置
    OriGine::Vec2f resizeStartCursor{};
    /// リサイズ開始時の offsetMin
    OriGine::Vec2f resizeStartMin{};
    /// リサイズ開始時の offsetMax
    OriGine::Vec2f resizeStartMax{};

    /// 閉じるボタンが押された. 1 フレームだけでなく、アプリが拾って破棄するまで立ったままにする.
    bool closeRequested = false;
    /// 切り離しボタンが押された. v9 でアプリが拾う（今のところ拾い先が無いので立ちっぱなしになる）.
    bool detachRequested = false;

    /// v14: 今このウィンドウが入っている葉ノード (UiDockNode)。無効ならフローティング.
    /// ドック/アンドックのたびに UiDockBuilder (DockUiWindow/UndockUiWindow) が書き換える。
    /// 実行時の状態なので JSON には保存しない.
    OriGine::EntityHandle dockNode{};
    /// ドックされているかどうか.
    bool IsDocked() const { return dockNode.IsValid(); }
};

void to_json(nlohmann::json& _j, const UiWindow& _c);
void from_json(const nlohmann::json& _j, UiWindow& _c);

} // namespace LogGuide

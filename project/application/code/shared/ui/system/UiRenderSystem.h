#pragma once

/// parent
#include "system/render/base/BaseRenderSystem.h"

/// engine
#include "component/text/TextComponent.h" // TextLayoutResult
#include "directX12/DxDescriptor.h" // DxSrvDescriptor
#include "directX12/DxResource.h"
#include "directX12/PipelineStateObj.h"
#include "directX12/buffer/IStructuredBuffer.h"
#include "entity/EntityHandle.h"
#include "system/text/TextLayoutSystem.h"

/// application
#include "ui/UiGlyphInstanceData.h"
#include "ui/UiRectInstanceData.h"

/// stl
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace OriGine {
class BitmapFont;
}

namespace LogGuide {

class NativeWindowManager;

/// UiRectRenderSystem と UiTextRenderSystem を統合した描画システム.
/// v6 まで矩形とテキストが別システム (Render priority 0 / 1) だったため、
/// SystemRunner::UpdateCategory(Render) が「全部の矩形 → 全部のテキスト」の
/// 2 パスで実行してしまい、resolvedPriority で矩形同士・テキスト同士は正しく
/// 並んでいてもテキストが常に全矩形より後に描かれ、奥のウィンドウのテキストが
/// 手前のウィンドウの矩形の上に出てしまっていた。
/// ここでは矩形とグリフを同じ順序付きリストに集め、resolvedPriority で
/// 一緒に並べ替えてから PSO を切り替えつつ順に描く (ImGui のコマンドリストと同じ考え方)。
/// 頂点バッファは使わず、StructuredBuffer + SV_VertexID / SV_InstanceID で組み立てる点、
/// レイアウト計算・グリフのラスタライズ・アトラス管理を engine の
/// TextLayoutSystem / BitmapFont / FontManager にそのまま委譲する点は
/// 統合前の 2 システムから変えていない。
class UiRenderSystem final : public OriGine::BaseRenderSystem {
public:
    UiRenderSystem();
    ~UiRenderSystem() override;

    void Initialize() override;
    void Finalize() override;

    void CreatePSO() override;
    void StartRender() override;
    void Rendering() override;
    void DispatchRenderer(const OriGine::EntityHandle& _entity) override;
    bool ShouldSkipRender() const override;

    /// サーフェス (追加の OS ウィンドウ) の問い合わせ先を注入する (v10)。
    /// 未注入 (nullptr) のときは従来通りメインウィンドウ 1 枚だけに描く。
    void SetSurfaceProvider(NativeWindowManager* _provider) { surfaceProvider_ = _provider; }

private:
    /// フォント 1 つ分の GPU アトラステクスチャ (UiTextRenderSystem からそのまま移した).
    struct FontAtlasGpu {
        OriGine::DxResource resource;
        OriGine::DxSrvDescriptor srv;
        bool created       = false;
        int uploadedWidth  = 0;
        int uploadedHeight = 0;
    };

    FontAtlasGpu& EnsureAtlas(OriGine::BitmapFont* _font);
    void CreateAtlasTexture(OriGine::BitmapFont& _font, FontAtlasGpu& _gpu);
    void ReuploadAtlasTexture(OriGine::BitmapFont& _font, FontAtlasGpu& _gpu);

    // ---- このフレームに集めたもの ----

    /// このフレームに描く矩形 1 つ分.
    struct RectItem {
        int32_t priority    = 0;
        int32_t surfaceId   = 0; ///< 描画先サーフェス (v10). UiLayoutSystem が解決した resolvedSurfaceId.
        UiRectInstanceData data{};
    };
    /// このフレームに描くテキスト 1 件分.
    /// アトラスが作り直された場合にレイアウトを計算し直せるよう、
    /// TextComponent とレイアウト結果は非 const で持つ。
    struct TextItem {
        int32_t priority                  = 0;
        int32_t surfaceId                 = 0; ///< 描画先サーフェス (v10).
        OriGine::BitmapFont* font         = nullptr;
        OriGine::TextComponent* text      = nullptr;
        OriGine::TextLayoutResult* layout = nullptr;
        /// 親から受け継いだクリップ矩形（画面ピクセル）. UiLayoutSystem が解決する.
        OriGine::Vec2f clipMin{};
        OriGine::Vec2f clipMax{};
    };

    /// 矩形とテキストを同じ順序で並べるための指し手.
    enum class DrawKind : uint8_t {
        Rect = 0, ///< 同じ優先度なら矩形が先（自分の背景の上に自分の文字が乗る）
        Text = 1,
    };
    /// サーフェス → resolvedPriority の順で並べ替えるための 1 要素分 (実データは持たず添字だけ持つ).
    struct DrawCommand {
        int32_t surfaceId  = 0; ///< v10: まずサーフェスでまとめる
        int32_t priority   = 0;
        DrawKind kind      = DrawKind::Rect;
        uint32_t itemIndex = 0; ///< rectItems_ / textItems_ の添字
    };
    /// 並べ替え後、連続する同種のコマンドをまとめた、実際のドローコール 1 回分.
    struct DrawRun {
        int32_t surfaceId          = 0; ///< v10: このランの描画先サーフェス
        DrawKind kind              = DrawKind::Rect;
        OriGine::BitmapFont* font  = nullptr; ///< Text のときだけ使う
        uint32_t firstInstance     = 0;
        uint32_t instanceCount     = 0;
    };

    /// このフレームに集めた矩形 (Rendering() の先頭で毎フレーム積まれ、末尾でクリアする).
    std::vector<RectItem> rectItems_;
    /// このフレームに集めたテキスト.
    std::vector<TextItem> textItems_;
    /// rectItems_ / textItems_ を resolvedPriority でまとめて並べ替えるための作業領域.
    std::vector<DrawCommand> commands_;
    /// 並べ替え後、連続する同種をまとめたドローラン列.
    std::vector<DrawRun> runs_;

    // ---- GPU ----
    static constexpr uint32_t kInitialRectCapacity  = 256;
    static constexpr uint32_t kInitialGlyphCapacity = 2048;

    OriGine::PipelineStateObj* rectPso_ = nullptr;
    OriGine::PipelineStateObj* textPso_ = nullptr;
    OriGine::IStructuredBuffer<UiRectInstanceData> rectBuffer_;
    OriGine::IStructuredBuffer<UiGlyphInstanceData> glyphBuffer_;

    /// フォント (BitmapFont*) ごとの GPU アトラス.
    std::unordered_map<OriGine::BitmapFont*, FontAtlasGpu> atlases_;
    /// エンティティごとのレイアウト結果。UpdateLayout の早期 return を効かせるために保持する.
    std::unordered_map<OriGine::EntityHandle, OriGine::TextLayoutResult> layoutCache_;
    OriGine::TextLayoutSystem layout_;

    /// サーフェスのサイズ/取得先 (v10). 未注入ならメインウィンドウ 1 枚だけに描く.
    NativeWindowManager* surfaceProvider_ = nullptr;
    /// サーフェスごとに実行するランの範囲を切り出すための作業領域 (Rendering() 内で使い回す).
    std::vector<int32_t> surfacesToProcess_;
};

} // namespace LogGuide

#pragma once

/// api
#include <Windows.h>

/// Microsoft
#include <wrl.h>

#include <d3d12.h>
#include <dxgi1_6.h>

/// stl
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// engine
#include "directX12/DxDescriptor.h"
#include "directX12/DxResource.h"

/// math
#include <Vector2.h>
#include <Vector4.h>

namespace LogGuide {

/// 追加の OS ウィンドウ 1 枚。自前のウィンドウクラスと自前のスワップチェーンを持つ。
/// engine の WinApp / DxSwapChain は「メインウィンドウ 1 枚」専用なので、
/// それらを流用せずに同じことをこのクラスで行う (engine は submodule なので変更しない)。
///
/// v9 で DirectX まわりの罠 (バックバッファのリサイズ、RTV/DSV のフォーマット、
/// バリアの張り方) を先に潰し、v10 で UiRenderSystem がこのクラスの
/// BindAndClear() / EndRender() を使って自作 UI を実際にここへ描くようになった。
class NativeWindow {
public:
    /// ウィンドウ生成時のパラメータ.
    struct Desc {
        std::wstring title = L"LogGuide";
        int32_t x = CW_USEDEFAULT, y = CW_USEDEFAULT; ///< スクリーン座標 (枠を含む)
        int32_t clientWidth = 480, clientHeight = 320;
        int32_t minClientWidth = 200, minClientHeight = 120;
    };

    NativeWindow()  = default;
    ~NativeWindow();

    /// ウィンドウの生成と、スワップチェーン/RTV/DSV の初期化を行う.
    bool Create(const Desc& _desc);
    /// GPU の完了を待ち、DirectX リソースを解放してからウィンドウを破棄する.
    /// 呼んだ後 GetHwnd() は nullptr を返す.
    void Destroy();

    /// フレームの先頭で呼ぶ. 溜まっていたリサイズ要求を処理する (GPU 待ちを含む).
    void BeginFrame();
    /// バックバッファを RENDER_TARGET に遷移させ、RTV/DSV をバインドし、クリアする.
    /// Engine::ScreenPreDraw() と ScreenPostDraw() の「間」で呼ぶこと.
    void BindAndClear();
    /// バックバッファを PRESENT に戻す. BindAndClear と対で、同じくコマンド記録中に呼ぶ.
    void EndRender();
    /// Engine::ScreenPostDraw() の「後」に呼ぶ.
    void Present();

    HWND GetHwnd() const { return hwnd_; }
    OriGine::Vec2f GetClientSize() const;
    /// 閉じるボタンが押されたか (WM_CLOSE を受けても即破棄はしない).
    bool IsCloseRequested() const { return closeRequested_; }
    /// スクリーン座標のカーソルをこのウィンドウのクライアント座標に変換する.
    OriGine::Vec2f ScreenToClientPos(POINT _screen) const;
    /// このウィンドウのクライアント座標をスクリーン座標に変換する (ScreenToClientPos の逆).
    /// v10: 切り離し/再結合で UI ウィンドウの矩形とスクリーン座標を相互変換するために使う.
    OriGine::Vec2f ClientToScreenPos(POINT _client) const;
    /// カーソルが今このウィンドウの上にあるか (他のウィンドウに隠れていない).
    bool IsCursorOver() const;
    /// GetForegroundWindow() == hwnd_ かどうか.
    bool IsActive() const;

    /// サイズ変更/移動のモーダルループ中に 1 フレーム分回すためのコールバック.
    /// engine の WinApp::SetSizeMoveFrameCallback と同じ狙い.
    void SetSizeMoveFrameCallback(const std::function<void()>& _cb) { sizeMoveFrameCallback_ = _cb; }

    /// クリア色を設定する.
    /// v9 の確認用: サブウィンドウごとに違う色にして、個別に描けていることを目で確認するために使う.
    void SetClearColor(const OriGine::Vec4f& _color) { clearColor_ = _color; }

private:
    /// ウィンドウクラスを (プロセス内で) 1 回だけ登録する.
    static void EnsureWindowClassRegistered();
    /// WM_NCCREATE で GWLP_USERDATA に this を仕込む. CreateWindowEx から戻った後に
    /// セットすると、生成中に飛んでくるメッセージで pThis == nullptr になるため.
    static LRESULT CALLBACK WndProc(HWND _hwnd, UINT _msg, WPARAM _wparam, LPARAM _lparam);

    /// スワップチェーンを生成する (Create() から 1 回だけ呼ぶ).
    void CreateSwapChain();
    /// スワップチェーンのバックバッファを取得し、RTV を作る.
    void CreateBackBufferViews();
    /// RTV を返し、バックバッファのリソース参照を解放する (ResizeBuffers の前に必須).
    void ReleaseBackBufferViews();
    /// 深度バッファと DSV を作る (Engine::CreateDsv() と同じ手順).
    void CreateDepthBuffer(UINT _width, UINT _height);
    /// DSV と深度バッファを解放する.
    void ReleaseDepthBuffer();

    HWND hwnd_ = nullptr;

    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    std::vector<OriGine::DxResource> backBufferResources_;
    std::vector<OriGine::DxRtvDescriptor> backBufferRtvs_;
    UINT bufferCount_ = 0;

    OriGine::DxResource dsvResource_;
    OriGine::DxDsvDescriptor dsv_;

    UINT width_  = 0; ///< 現在のクライアント領域の幅 (バックバッファの幅と一致させる)
    UINT height_ = 0;
    int32_t minClientWidth_  = 0;
    int32_t minClientHeight_ = 0;

    bool closeRequested_ = false;

    /// WM_SIZE で記録するだけにして、実際のリサイズは BeginFrame() で行う
    /// (描画の途中でスワップチェーンをリサイズすると危険なため).
    bool pendingResize_ = false;
    UINT pendingWidth_  = 0;
    UINT pendingHeight_ = 0;

    bool inSizeMove_      = false; ///< サイズ変更/移動のモーダルループ中か
    bool inSizeMoveFrame_ = false; ///< sizeMoveFrameCallback_ の実行中か (再入防止)
    std::function<void()> sizeMoveFrameCallback_;

    OriGine::Vec4f clearColor_ = {0.05f, 0.05f, 0.08f, 1.0f};
};

} // namespace LogGuide

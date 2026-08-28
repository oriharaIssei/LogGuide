#include "ui/native/NativeWindow.h"

/// stl
#include <cassert>

/// engine
#include "Engine.h"
#include "directX12/DxCommand.h"
#include "directX12/DxDevice.h"
#include "directX12/DxFence.h"
#include "EngineConfig.h"
#include "logger/Logger.h"
#include "winApp/WinApp.h" // AddOwnedWindow / RemoveOwnedWindow の呼び出しに WinApp の完全な定義が要る

using namespace OriGine;

namespace LogGuide {

namespace {
/// ウィンドウクラス名. プロセス内のすべての NativeWindow で共有する.
constexpr wchar_t kClassName[] = L"LogGuideNativeWindow";
} // namespace

NativeWindow::~NativeWindow() {
    Destroy();
}

void NativeWindow::EnsureWindowClassRegistered() {
    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASSEX wc{};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW); // 入れないとカーソルが化ける
    wc.hbrBackground = nullptr; // GDI に塗らせない (毎フレーム DirectX 側でクリアするため)
    wc.lpszClassName = kClassName;

    RegisterClassEx(&wc);
    registered = true;
}

LRESULT CALLBACK NativeWindow::WndProc(HWND _hwnd, UINT _msg, WPARAM _wparam, LPARAM _lparam) {
    // this ポインタは CreateWindowEx の最後の引数 (lpParam) で渡ってくる。
    // CreateWindowEx から戻った後に SetWindowLongPtr するのでは、生成中に飛んでくる
    // メッセージ (WM_GETMINMAXINFO 等) で pThis == nullptr になってしまうため、
    // 最初に届く WM_NCCREATE の中で仕込む。
    if (_msg == WM_NCCREATE) {
        const CREATESTRUCT* createStruct = reinterpret_cast<const CREATESTRUCT*>(_lparam);
        SetWindowLongPtr(_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
    }

    NativeWindow* self = reinterpret_cast<NativeWindow*>(GetWindowLongPtr(_hwnd, GWLP_USERDATA));

    switch (_msg) {
        case WM_CLOSE:
            // DestroyWindow はここで呼ばない。破棄はアプリのフレーム処理 (NativeWindowManager::BeginFrame)
            // から行う。描画の途中で HWND が消えると厄介なため。
            if (self != nullptr) {
                self->closeRequested_ = true;
            }
            return 0;

        case WM_DESTROY:
            // PostQuitMessage は絶対に呼ばない。呼ぶとこのサブウィンドウを閉じただけで
            // アプリ全体が終了してしまう (メインウィンドウのウィンドウクラスにだけ結びつく処理)。
            if (self != nullptr) {
                self->hwnd_ = nullptr;
            }
            return 0;

        case WM_ERASEBKGND:
            // GDI に消させない。リサイズ中のちらつきが減る。
            return 1;

        case WM_GETMINMAXINFO:
            if (self != nullptr) {
                MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(_lparam);
                RECT frame{0, 0, self->minClientWidth_, self->minClientHeight_};
                AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
                mmi->ptMinTrackSize.x = frame.right - frame.left;
                mmi->ptMinTrackSize.y = frame.bottom - frame.top;
            }
            return 0;

        case WM_ENTERSIZEMOVE:
            // ここから DefWindowProc がモーダルループに入り、アプリのメインループが止まる。
            if (self != nullptr) {
                self->inSizeMove_ = true;
            }
            break;

        case WM_EXITSIZEMOVE:
            if (self != nullptr) {
                self->inSizeMove_ = false;
            }
            break;

        case WM_SIZE:
            if (self != nullptr) {
                // 最小化 (SIZE_MINIMIZED や幅/高さ 0) のときはリサイズ要求を立てない。
                const UINT newWidth  = LOWORD(_lparam);
                const UINT newHeight = HIWORD(_lparam);
                if (_wparam != SIZE_MINIMIZED && newWidth > 0 && newHeight > 0) {
                    self->pendingWidth_  = newWidth;
                    self->pendingHeight_ = newHeight;
                    self->pendingResize_ = true;

                    // ドラッグ中のモーダルループでアプリのメインループが止まるため、
                    // ここから 1 フレーム分の更新と描画を行う。再入防止のフラグを必ず見ること
                    // (コールバックの中からさらに WM_SIZE が飛ぶ経路があるため)。
                    if (self->inSizeMove_ && self->sizeMoveFrameCallback_ && !self->inSizeMoveFrame_) {
                        self->inSizeMoveFrame_ = true;
                        self->sizeMoveFrameCallback_();
                        self->inSizeMoveFrame_ = false;
                    }
                }
            }
            break;

        default:
            break;
    }

    return DefWindowProc(_hwnd, _msg, _wparam, _lparam);
}

bool NativeWindow::Create(const Desc& _desc) {
    EnsureWindowClassRegistered();

    minClientWidth_  = _desc.minClientWidth;
    minClientHeight_ = _desc.minClientHeight;

    RECT rect{0, 0, _desc.clientWidth, _desc.clientHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowEx(
        0,
        kClassName,
        _desc.title.c_str(),
        WS_OVERLAPPEDWINDOW,
        _desc.x, _desc.y,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr, // 親ウィンドウなし (独立した OS ウィンドウにする)
        nullptr,
        GetModuleHandle(nullptr),
        this); // WM_NCCREATE の lpCreateParams としてここから渡る

    if (hwnd_ == nullptr) {
        LOG_ERROR("Failed to create native window.");
        return false;
    }

    // AdjustWindowRect は近似値なので、実際のクライアント領域を取り直す。
    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    width_  = static_cast<UINT>(clientRect.right - clientRect.left);
    height_ = static_cast<UINT>(clientRect.bottom - clientRect.top);
    if (width_ == 0) {
        width_ = 1;
    }
    if (height_ == 0) {
        height_ = 1;
    }

    CreateSwapChain();
    CreateBackBufferViews();
    CreateDepthBuffer(width_, height_);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    // このウィンドウが前面のときも engine 側 (WinApp::UpdateActivity) がアプリを
    // アクティブ扱いにできるよう、追加ウィンドウとして登録しておく。
    Engine::GetInstance()->GetWinApp()->AddOwnedWindow(hwnd_);

    return true;
}

void NativeWindow::CreateSwapChain() {
    DxDevice* device = Engine::GetInstance()->GetDxDevice();
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue = Engine::GetInstance()->GetDxCommand()->GetCommandQueue();

    bufferCount_ = Config::Rendering::kSwapChainBufferCount;

    // キューは engine のメインキューをそのまま使う。別キューにすると同期が要るので分けないこと。
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width            = width_;
    swapChainDesc.Height           = height_;
    swapChainDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount      = bufferCount_;
    swapChainDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT result = device->dxgiFactory_->CreateSwapChainForHwnd(
        queue.Get(),
        hwnd_,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1);
    if (FAILED(result)) {
        LOG_CRITICAL("Failed to create swap chain for native window.");
        assert(false);
        return;
    }

    result = swapChain1->QueryInterface(IID_PPV_ARGS(&swapChain_));
    if (FAILED(result)) {
        LOG_CRITICAL("Failed to query swap chain interface for native window.");
        assert(false);
    }
}

void NativeWindow::CreateBackBufferViews() {
    backBufferResources_.resize(bufferCount_);
    backBufferRtvs_.resize(bufferCount_);

    auto* rtvHeap = Engine::GetInstance()->GetRtvHeap();

    // RTV の Format は R8G8B8A8_UNORM_SRGB (バッファ自体は UNORM)。
    // ShaderManager::CreatePso が RTV フォーマットを SRGB 決め打ちにしているため、
    // ここを合わせないと後の段階 (v10) で PSO とレンダーターゲットが不一致になる。
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format        = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    for (UINT i = 0; i < bufferCount_; ++i) {
        HRESULT result = swapChain_->GetBuffer(
            i, IID_PPV_ARGS(backBufferResources_[i].GetResourceRef().GetAddressOf()));
        if (FAILED(result)) {
            LOG_CRITICAL("Failed to get back buffer for native window.");
            assert(false);
            continue;
        }

        RTVEntry rtvEntry{&backBufferResources_[i], rtvDesc};
        backBufferRtvs_[i] = rtvHeap->CreateDescriptor(&rtvEntry);
    }
}

void NativeWindow::ReleaseBackBufferViews() {
    auto* rtvHeap = Engine::GetInstance()->GetRtvHeap();

    for (UINT i = 0; i < bufferCount_; ++i) {
        if (i < backBufferRtvs_.size()) {
            rtvHeap->ReleaseDescriptor(backBufferRtvs_[i]);
        }
        if (i < backBufferResources_.size()) {
            // ResizeBuffers はバックバッファへの参照が 1 つでも残っていると失敗するため、
            // ComPtr を必ず全部 Reset する。
            backBufferResources_[i].Finalize();
        }
    }
}

void NativeWindow::CreateDepthBuffer(UINT _width, UINT _height) {
    // ShaderManager::CreatePso は DSV フォーマットを D24_UNORM_S8_UINT 決め打ちにしている。
    // 深度を使わない UI でも、PSO が DSV フォーマットを持っている以上、深度バッファを
    // バインドせずに描くと D3D12 のデバッグレイヤーが警告を出すため、安全側に倒して持つ
    // (Engine::CreateDsv() と同じ手順)。
    dsvResource_.CreateDSVBuffer(Engine::GetInstance()->GetDxDevice()->device_, static_cast<UINT64>(_width), _height);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format        = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    DSVEntry dsvEntry{&dsvResource_, dsvDesc};
    dsv_ = Engine::GetInstance()->GetDsvHeap()->CreateDescriptor(&dsvEntry);
}

void NativeWindow::ReleaseDepthBuffer() {
    Engine::GetInstance()->GetDsvHeap()->ReleaseDescriptor(dsv_);
    dsvResource_.Finalize();
}

void NativeWindow::Destroy() {
    if (hwnd_ == nullptr && !swapChain_) {
        return; // 既に破棄済み
    }

    if (swapChain_) {
        // GPU 待ち → RTV/DSV を返す → スワップチェーンを解放、の順を必ず守る。
        // HWND を先に壊すとスワップチェーンの解放でトラブルになる。
        DxFence* fence = Engine::GetInstance()->GetDxFence();
        fence->WaitForFence(fence->Signal(Engine::GetInstance()->GetDxCommand()->GetCommandQueue()));

        ReleaseBackBufferViews();
        ReleaseDepthBuffer();
        swapChain_.Reset();
    }

    if (hwnd_ != nullptr) {
        // 登録した順番は問わないので、実際に破棄する前に解除しておく。
        Engine::GetInstance()->GetWinApp()->RemoveOwnedWindow(hwnd_);

        // DestroyWindow は同期的に WM_DESTROY を送るので、その中で hwnd_ が nullptr になる。
        DestroyWindow(hwnd_);
        hwnd_ = nullptr; // 念のため (WM_DESTROY が届かない異常系への保険)
    }
}

void NativeWindow::BeginFrame() {
    if (hwnd_ == nullptr || !pendingResize_) {
        return;
    }
    pendingResize_ = false;

    const UINT newWidth  = pendingWidth_;
    const UINT newHeight = pendingHeight_;
    if (newWidth == width_ && newHeight == height_) {
        return;
    }

    // 1. GPU の完了を待つ
    DxFence* fence = Engine::GetInstance()->GetDxFence();
    fence->WaitForFence(fence->Signal(Engine::GetInstance()->GetDxCommand()->GetCommandQueue()));

    // 2. RTV を返し、バックバッファの ComPtr を全部 Reset する。深度バッファも作り直す。
    ReleaseBackBufferViews();
    ReleaseDepthBuffer();

    // 3. バックバッファのリサイズ
    HRESULT result = swapChain_->ResizeBuffers(bufferCount_, newWidth, newHeight, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (FAILED(result)) {
        LOG_CRITICAL("Failed to resize swap chain buffers for native window.");
        assert(false);
        return;
    }

    width_  = newWidth;
    height_ = newHeight;

    // 4. バッファを取り直して RTV を作り直す
    CreateBackBufferViews();
    CreateDepthBuffer(width_, height_);
}

void NativeWindow::BindAndClear() {
    if (hwnd_ == nullptr || !swapChain_) {
        return;
    }

    DxCommand* command      = Engine::GetInstance()->GetDxCommand();
    const UINT backBufferIdx = swapChain_->GetCurrentBackBufferIndex();

    // バリアは明示的に before/after を指定する (ResourceDirectBarrier)。ResourceBarrier
    // (トラッカー任せ) は使わない。ResizeBuffers でバックバッファのリソースポインタが
    // 変わるため、トラッカーが持っている状態が古くなる恐れがあるため。
    // フレーム開始時のバックバッファは必ず PRESENT 状態。
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = backBufferResources_[backBufferIdx].GetResource().Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command->ResourceDirectBarrier(backBufferResources_[backBufferIdx].GetResource(), barrier);

    // OMSetRenderTargets / ClearRenderTargetView / ClearDepthStencilView は
    // DxCommand::ClearTarget が engine と同じ手順でまとめてやってくれる。
    command->ClearTarget(backBufferRtvs_[backBufferIdx], dsv_, clearColor_);

    D3D12_VIEWPORT viewport{
        0.0f, 0.0f,
        static_cast<float>(width_), static_cast<float>(height_),
        Config::Rendering::kMinDepth, Config::Rendering::kMaxDepth};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    command->GetCommandList()->RSSetViewports(1, &viewport);
    command->GetCommandList()->RSSetScissorRects(1, &scissor);
}

void NativeWindow::EndRender() {
    if (hwnd_ == nullptr || !swapChain_) {
        return;
    }

    DxCommand* command      = Engine::GetInstance()->GetDxCommand();
    const UINT backBufferIdx = swapChain_->GetCurrentBackBufferIndex();

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = backBufferResources_[backBufferIdx].GetResource().Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command->ResourceDirectBarrier(backBufferResources_[backBufferIdx].GetResource(), barrier);
}

void NativeWindow::Present() {
    if (hwnd_ == nullptr || !swapChain_) {
        return;
    }
    swapChain_->Present(1, 0);
}

OriGine::Vec2f NativeWindow::GetClientSize() const {
    return OriGine::Vec2f(static_cast<float>(width_), static_cast<float>(height_));
}

OriGine::Vec2f NativeWindow::ScreenToClientPos(POINT _screen) const {
    POINT pt = _screen;
    if (hwnd_ != nullptr) {
        ScreenToClient(hwnd_, &pt);
    }
    return OriGine::Vec2f(static_cast<float>(pt.x), static_cast<float>(pt.y));
}

OriGine::Vec2f NativeWindow::ClientToScreenPos(POINT _client) const {
    POINT pt = _client;
    if (hwnd_ != nullptr) {
        ClientToScreen(hwnd_, &pt);
    }
    return OriGine::Vec2f(static_cast<float>(pt.x), static_cast<float>(pt.y));
}

bool NativeWindow::IsCursorOver() const {
    if (hwnd_ == nullptr) {
        return false;
    }
    POINT pt{};
    GetCursorPos(&pt);
    HWND hit = WindowFromPoint(pt);
    return hit == hwnd_ || IsChild(hwnd_, hit) != FALSE;
}

bool NativeWindow::IsActive() const {
    return hwnd_ != nullptr && GetForegroundWindow() == hwnd_;
}

} // namespace LogGuide

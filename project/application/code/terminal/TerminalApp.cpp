#include "TerminalApp.h"

#include <algorithm> // std::max / std::clamp (v10: 再結合時のクランプ処理)
#include <string> // std::to_string (クリック回数の文字列化)

#define ENGINE_INCLUDE
#define RESOURCE_DIRECTORY
#include <EngineInclude.h>

#include "globalVariables/GlobalVariables.h"
#include "scene/SceneManager.h"

/// engine (window handle)
#include "winApp/WinApp.h"
#include "logger/Logger.h"
/// engine (v9/v10: 追加ウィンドウの描画後にメインのバックバッファへ戻すために必要。Engine.h は前方宣言のみ)
#include "directX12/DxCommand.h"
#include "directX12/DxSwapChain.h"
/// engine (v10: 切り離した OS ウィンドウのタイトルに UTF-8 のウィンドウ名を出すための変換)
#include "util/StringUtil.h"

/// engine (ECS シーンを直接立ち上げて自作 UI を描画するため)
#include "input/InputManager.h"
#include "scene/Scene.h"
#include "system/SystemRunner.h"

/// module（全構成で有効）
#include "SessionCatalog.h"

/// UI（v9 で追加の OS ウィンドウ基盤を用意し、v10 で自作 UI のウィンドウをそこに載せて
/// 切り離し/再結合できるようにした）
#include "ui/native/NativeWindowManager.h"

/// UI（ImGui に依存しない自作 UI。ECS 上のコンポーネント/システムとして実装している）
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
/// UI（v6: ウィンドウの移動/前面化。UiWindowBuilder がウィンドウ 1 枚分の 3 エンティティを組み立てる）
#include "ui/UiWindowBuilder.h"
#include "ui/system/UiWindowSystem.h"

/// UI（v2: テキストのコンポーネントは engine の TextComponent をそのまま使う。
/// v5: 描画はクリップ矩形付きで描ける自作の UiTextRenderSystem に乗り換えた。
/// v7: UiTextRenderSystem は UiRectRenderSystem と統合されて UiRenderSystem になった）
#include "component/text/TextComponent.h"

#ifdef _DEBUG
/// externals
#include <imgui/imgui.h>

/// terminal（ランチャー UI。ImGui 同様 Debug 構成のみ存在する）
#include "TerminalPanel.h"
#endif // _DEBUG

using namespace OriGine;

TerminalApp::TerminalApp()  = default;
TerminalApp::~TerminalApp() = default;

void TerminalApp::Initialize(const std::vector<std::string>& _commandLines) {
    variables_    = GlobalVariables::GetInstance();
    engine_       = Engine::GetInstance();
    sceneManager_ = std::make_unique<SceneManager>();

    variables_->LoadAllFile();
    engine_->Initialize();

    (void)_commandLines; // ターミナルは引数を取らない

    RegisterUsingComponents();
    RegisterUsingSystems();

    ApplyWindowSettings();

#ifdef _DEBUG
    // 録画/再生アプリと作業ディレクトリを共有するため、ウィンドウ配置の ini を分ける。
    // ランチャーにシーンエディタは不要なので、EditorController は初期化しない。
    ImGui::GetIO().IniFilename = "imgui_terminal.ini";
#endif // _DEBUG

    // 他アプリと見分けられるようウィンドウタイトルを設定する。
    engine_->GetWinApp()->SetWindowTitle(L"LogGuide");

    // リサイズは自由にさせる。
    // WinApp の既定は FIXED_ASPECT (Settings/WindowState/ResizeMode の既定値 2) で、
    // 起動時のアスペクト比を保つようウィンドウ矩形が補正されてしまう。
    // 複数のパネルを自由に配置する UI にはアスペクト比の固定は邪魔になるうえ、
    // WinApp の FIXED_ASPECT はクライアント領域の比をウィンドウ矩形（タイトルバーと枠を含む）に
    // 適用しているため、実際のクライアント比が意図した値からずれる。
    // WinApp::Initialize() 内の RestoreWindowState() より後に呼ぶ必要があるので、
    // engine_->Initialize() のあとで設定する。
    engine_->GetWinApp()->SetWindowResizeMode(WindowResizeMode::FREE);

    // 録画セッション一覧を起動時に一度読み込んでおく。
    catalog_ = std::make_unique<LogGuide::SessionCatalog>();
    catalog_->Refresh();

    // v9: 追加の OS ウィンドウ基盤。所有者はアプリなのでシングルトンにはしない。
    nativeWindows_ = std::make_unique<LogGuide::NativeWindowManager>();

    // ECS シーンを立ち上げる。
    // Scene::Initialize() は SceneView / RaytracingScene / シーン JSON まで面倒を見るが、
    // ここでは ECS だけあればよいので InitializeECS() を直接呼ぶ。
    InputManager* input = InputManager::GetInstance();
    scene_              = std::make_unique<Scene>("Terminal");
    scene_->SetInputDevices(input->GetKeyboard(), input->GetMouse(), nullptr);
    scene_->InitializeECS();
    scene_->SetActive(true);

    SystemRunner* runner = scene_->GetSystemRunnerRef();
    runner->RegisterSystem<LogGuide::UiLayoutSystem>(0, true, true);
    // v7: 矩形とテキストの描画は 1 つの UiRenderSystem にまとめてある
    // (別々のパスに分けると「全矩形 → 全テキスト」の 2 パスになり、奥のウィンドウの
    // テキストが手前のウィンドウの矩形の上に出てしまうため)。
    runner->RegisterSystem<LogGuide::UiRenderSystem>(0, true, true);
    // v3: ヒットテストと色変化。Input / StateTransition カテゴリで、Render カテゴリの
    // 矩形描画系とは実行順が競合しないため priority はどちらも 0 でよい。
    runner->RegisterSystem<LogGuide::UiInteractionSystem>(0, true, true);
    runner->RegisterSystem<LogGuide::UiHighlightSystem>(0, true, true);
    // v6: ウィンドウの移動/前面化。StateTransition カテゴリだが他システムとは依存関係が
    // ないため、priority はどちらでもよい（他の登録同様 0 にしておく）。
    runner->RegisterSystem<LogGuide::UiWindowSystem>(0, true, true);

    // v10: サーフェス (追加の OS ウィンドウ) 対応のために各システムへ NativeWindowManager を注入する。
    // 未注入のときは各システムとも従来通りメインウィンドウ 1 枚だけの挙動になる。
    runner->GetSystem<LogGuide::UiLayoutSystem>()->SetSurfaceProvider(nativeWindows_.get());
    runner->GetSystem<LogGuide::UiRenderSystem>()->SetSurfaceProvider(nativeWindows_.get());
    runner->GetSystem<LogGuide::UiInteractionSystem>()->SetSurfaceProvider(nativeWindows_.get());
    runner->GetSystem<LogGuide::UiWindowSystem>()->SetSurfaceProvider(nativeWindows_.get());

    // --- ウィンドウ A: 中にボタンを 1 つ置く ---
    LogGuide::UiWindowHandles windowA = LogGuide::CreateUiWindow(
        scene_.get(), runner, "ウィンドウ A", {60.0f, 60.0f}, {320.0f, 200.0f}, 0);
    uiWindowRoots_.push_back(windowA.root); // v10: 切り離し/再結合の対象として覚えておく

    counterButton_ = scene_->CreateEntity("UiCounterButton");
    scene_->AddComponent<LogGuide::UiTransform>(counterButton_);
    scene_->AddComponent<LogGuide::UiRect>(counterButton_);
    scene_->AddComponent<LogGuide::UiText>(counterButton_);
    scene_->AddComponent<LogGuide::UiInteractable>(counterButton_);
    scene_->AddComponent<LogGuide::UiHighlight>(counterButton_);
    scene_->AddComponent<OriGine::TextComponent>(counterButton_);

    // ウィンドウ A の内容領域の上寄りに横いっぱい配置する。
    if (LogGuide::UiTransform* buttonTransform =
            scene_->GetComponent<LogGuide::UiTransform>(counterButton_)) {
        buttonTransform->parent         = windowA.contentArea;
        buttonTransform->anchorMin      = {0.0f, 0.0f};
        buttonTransform->anchorMax      = {1.0f, 0.0f};
        buttonTransform->offsetMin      = {16.0f, 20.0f};
        buttonTransform->offsetMax      = {-16.0f, 64.0f};
        buttonTransform->renderPriority = 10;
    }
    // 動作確認用の文字列。クリックできることを示すため、初期ラベルをクリック誘導文言にする
    // （日本語のままなので、グリフの動的追加とアトラス再アップロードの確認は引き続き兼ねる）。
    // fillColor は UiHighlightSystem が毎フレーム上書きするので、UiRect 側の初期値は指定しない。
    if (TextComponent* buttonLabel =
            scene_->GetComponent<OriGine::TextComponent>(counterButton_)) {
        buttonLabel->text     = "クリックしてください";
        buttonLabel->fontSize = 18.0f;
        buttonLabel->align    = OriGine::TextAlign::Center;
        buttonLabel->color    = {0.92f, 0.94f, 0.98f, 1.0f};
        buttonLabel->dirty    = true;
    }
    runner->RegisterEntity<LogGuide::UiLayoutSystem,
                            LogGuide::UiRenderSystem,
                            LogGuide::UiInteractionSystem,
                            LogGuide::UiHighlightSystem>(counterButton_);

    // --- ウィンドウ B: 中身がウィンドウで切られることを見せる ---
    windowB_ = LogGuide::CreateUiWindow(
        scene_.get(), runner, "ウィンドウ B", {200.0f, 140.0f}, {320.0f, 200.0f}, 1);
    uiWindowRoots_.push_back(windowB_.root); // v10: 切り離し/再結合の対象として覚えておく

    overflowTextEntity_ = scene_->CreateEntity("UiOverflowText");
    scene_->AddComponent<LogGuide::UiTransform>(overflowTextEntity_);
    scene_->AddComponent<LogGuide::UiText>(overflowTextEntity_);
    scene_->AddComponent<OriGine::TextComponent>(overflowTextEntity_);

    if (LogGuide::UiTransform* overflowTransform =
            scene_->GetComponent<LogGuide::UiTransform>(overflowTextEntity_)) {
        overflowTransform->parent    = windowB_.contentArea;
        // 内容領域より横に広くして、ウィンドウの縁で切られることを見せる。
        overflowTransform->anchorMin = {0.0f, 0.0f};
        overflowTransform->anchorMax = {1.0f, 1.0f};
        overflowTransform->offsetMin = {-40.0f, 12.0f};
        overflowTransform->offsetMax = {40.0f, -12.0f};
    }
    // テキストのクリップを目で見るための仕込み。ウィンドウの内容領域より横に広い要素に
    // 載せてあるので、クリップが効いていれば内容領域の左右の縁で文字が切られる。
    // ウィンドウを動かしても、切られる位置は内容領域に追従してついてくる。
    if (TextComponent* overflowLabel =
            scene_->GetComponent<OriGine::TextComponent>(overflowTextEntity_)) {
        overflowLabel->text     = "この文章はウィンドウの内容領域より横に広い要素に載せてあります。"
                                   "ウィンドウの縁で切られていれば、クリッピングが効いています。"
                                   "ウィンドウを動かしても切られる位置が付いてきます。";
        overflowLabel->fontSize = 14.0f;
        overflowLabel->align    = OriGine::TextAlign::Left;
        overflowLabel->color    = {0.85f, 0.88f, 0.94f, 1.0f};
        overflowLabel->dirty    = true;
    }
    runner->RegisterEntity<LogGuide::UiLayoutSystem,
                            LogGuide::UiRenderSystem>(overflowTextEntity_);

    // ウィンドウの縁をドラッグしている間、Win32 は DefWindowProc の中で独自のループを回すため
    // Run() のループが止まり、1 フレームも描画されなくなる。
    // エンジンに 1 フレーム分の処理を渡しておくと、ドラッグ中も追従して描画される。
    // シーンが完全に組み上がったあとに登録すること（コールバックは即座に呼ばれ得る）。
    engine_->GetWinApp()->SetSizeMoveFrameCallback([this]() { Frame(); });
}

void TerminalApp::Finalize() {
    // コールバックが this を握っているので、後始末より先に外す。
    if (engine_ != nullptr && engine_->GetWinApp() != nullptr) {
        engine_->GetWinApp()->ClearSizeMoveFrameCallback();
    }

    catalog_.reset();

    // v9: 追加の OS ウィンドウ。DirectX リソースを engine_->Finalize() より前に解放する。
    if (nativeWindows_) {
        nativeWindows_->CloseAll();
        nativeWindows_.reset();
    }

    if (scene_) {
        scene_->Finalize();
        scene_.reset();
    }

    // EditorController は Initialize していないため Finalize も呼ばない。
    sceneManager_.reset();
    engine_->Finalize();
}

void TerminalApp::Run() {
    while (!isEndRequest_) {
        if (engine_->ProcessMessage()) {
            isEndRequest_ = true;
            break;
        }

        Frame();
    }
}

/// <summary>
/// 1 フレーム分の更新と描画.
/// メインループからだけでなく、ウィンドウのサイズ変更/移動のモーダルループ中にも
/// WinApp から直接呼ばれる（そうしないとドラッグ中に 1 フレームも描画されない）。
/// </summary>
void TerminalApp::Frame() {
    engine_->BeginFrame();

    // v10: 前フレームで立った切り離し要求を処理する。scene_->Update() より前に行うことで、
    // このフレームの UiLayoutSystem が新しいサーフェス ID で矩形を解決できるようにする。
    HandlePendingDetachRequests();
    // v10: 切り離したウィンドウが実際に閉じられる直前のスクリーン矩形を控えておく
    // (nativeWindows_->BeginFrame() がこの直後に破棄してしまうため、それより前に行う必要がある)。
    CapturePendingReattachRects();

    // v9: 追加の OS ウィンドウのリサイズ処理と、閉じられたウィンドウの破棄。
    // engine_ の BeginFrame とスワップチェーンのリサイズ判定の位置を揃えるため、ここで呼ぶ。
    nativeWindows_->BeginFrame();

    // v10: このフレームで閉じられたサーフェスがあれば、対応する UI ウィンドウをメインへ戻す。
    HandleClosedSurfaces();

    // v11: UI 用のマウスボタン状態を更新する。engine の MouseInput は DirectInput を
    // DISCL_FOREGROUND でメインウィンドウに結び付けているため、切り離した OS ウィンドウに
    // フォーカスがあるとボタンが取れなくなる。scene_->Update() より前に 1 回だけ呼ぶこと。
    nativeWindows_->UpdateMouseState();

    scene_->Update(); // Input → StateTransition (ここで新たな detachRequested が立ちうる) → Movement → ...

    // UiInteractionSystem が Input カテゴリで立てた wasClicked を拾う。
    // wasClicked は 1 フレームだけ true になる。
    if (LogGuide::UiInteractable* interactable =
            scene_->GetComponent<LogGuide::UiInteractable>(counterButton_)) {
        if (interactable->wasClicked) {
            ++clickCount_;
            if (TextComponent* text = scene_->GetComponent<OriGine::TextComponent>(counterButton_)) {
                text->text  = "クリック: " + std::to_string(clickCount_);
                text->dirty = true; // 文字列を書き換えたら自分で立てる
            }
        }
    }

    // v8: ウィンドウ B の閉じるボタン。UiWindowSystem は中身まで辿れないので、
    // closeRequested を見たアプリ側が「ルート・タイトルバー・内容領域・両ボタン・中身」を
    // まとめて破棄する。一度破棄したらもう見に行かないよう windowBClosed_ で防ぐ。
    if (!windowBClosed_) {
        if (LogGuide::UiWindow* windowB = scene_->GetComponent<LogGuide::UiWindow>(windowB_.root)) {
            if (windowB->closeRequested) {
                scene_->AddDeleteEntity(windowB_.root);
                scene_->AddDeleteEntity(windowB_.titleBar);
                scene_->AddDeleteEntity(windowB_.contentArea);
                scene_->AddDeleteEntity(windowB_.closeButton);
                scene_->AddDeleteEntity(windowB_.detachButton);
                scene_->AddDeleteEntity(overflowTextEntity_); // アプリが contentArea の子として足した中身
                windowBClosed_ = true;
            }
        }
    }

#ifdef _DEBUG
    if (LogGuide::DrawTerminalPanel(*catalog_, &lastError_)) {
        isEndRequest_ = true; // アプリを起動したのでターミナルは終了する
    }
#endif // _DEBUG

    engine_->EndFrame();

    engine_->ScreenPreDraw();
    // Scene::Render() は SceneView(RenderTexture) に描いてコマンドリストを
    // Close/Execute してしまうため使わない。バックバッファが束ねられている
    // この位置で Render カテゴリだけを直接回す。
    // v10: 追加ウィンドウへの描画も UiRenderSystem がこの中で面倒を見る
    // (サーフェスごとに BindAndClear → 描画 → EndRender する)。
    // v9 時点の nativeWindows_->RenderAll() (単色クリアするだけの確認用) はここでは呼ばない
    // (UiRenderSystem が実際の内容を描いた直後に単色で上書きしてしまうため)。
    scene_->GetSystemRunnerRef()->UpdateCategory(SystemCategory::Render);

    // UiRenderSystem::Rendering() は最後に描いたサーフェスのバックバッファへ
    // レンダーターゲットをバインドしたままにする (EndRender() はバリアを PRESENT に戻すだけで、
    // バインド自体は戻さない)。ScreenPostDraw() は Close/Execute/Present/GPU 待ちしかしないと
    // 思いきや、Debug 構成では中で ImGuiManager::Draw() → ImGui_ImplDx12_RenderDrawData を
    // 呼んでおり、これは自分では OMSetRenderTargets せず「今バインドされている」
    // レンダーターゲットに直接描画コマンドを積む。
    // 戻さないと ImGui の描画がサブウィンドウ (しかも EndRender 後で PRESENT 状態に戻っている
    // バックバッファ) に飛んでしまい、デバッグレイヤーの警告にもなる。そのため、
    // ScreenPostDraw() の前に必ずメインのバックバッファへ戻す
    // (UiRenderSystem::Rendering() 側では戻さない。二重に書かないための一本化)。
    {
        DxSwapChain* mainSwapChain              = engine_->GetDxSwapChain();
        D3D12_CPU_DESCRIPTOR_HANDLE mainRtv      = mainSwapChain->GetCurrentBackBufferRtv();
        D3D12_CPU_DESCRIPTOR_HANDLE mainDsv      = engine_->GetDxDsv().GetCpuHandle();
        auto mainCommandList                     = engine_->GetDxCommand()->GetCommandList();
        mainCommandList->OMSetRenderTargets(1, &mainRtv, FALSE, &mainDsv);

        const int32_t mainWidth  = engine_->GetWinApp()->GetWidth();
        const int32_t mainHeight = engine_->GetWinApp()->GetHeight();
        D3D12_VIEWPORT mainViewport{
            0.0f, 0.0f,
            static_cast<float>(mainWidth), static_cast<float>(mainHeight),
            Config::Rendering::kMinDepth, Config::Rendering::kMaxDepth};
        D3D12_RECT mainScissor{0, 0, mainWidth, mainHeight};
        mainCommandList->RSSetViewports(1, &mainViewport);
        mainCommandList->RSSetScissorRects(1, &mainScissor);
    }

    engine_->ScreenPostDraw();
    // v9: 追加ウィンドウの Present。ScreenPostDraw() の中でメインの GPU 完了待ちまで済んでいる。
    nativeWindows_->PresentAll();
}

/// <summary>
/// 前フレームまでに立った UiWindow::detachRequested を処理する.
/// </summary>
void TerminalApp::HandlePendingDetachRequests() {
    for (const EntityHandle& root : uiWindowRoots_) {
        LogGuide::UiWindow* window = scene_->GetComponent<LogGuide::UiWindow>(root);
        if (!window || !window->detachRequested) {
            continue;
        }
        window->detachRequested = false;
        DetachWindow(root);
    }
}

/// <summary>
/// 指定した UI ウィンドウを、現在の矩形の位置に新しい OS ウィンドウとして切り離す.
/// </summary>
void TerminalApp::DetachWindow(const EntityHandle& _root) {
    LogGuide::UiWindow* window           = scene_->GetComponent<LogGuide::UiWindow>(_root);
    LogGuide::UiTransform* rootTransform = scene_->GetComponent<LogGuide::UiTransform>(_root);
    if (!window || !rootTransform) {
        return;
    }
    LogGuide::UiTransform* contentTransform = scene_->GetComponent<LogGuide::UiTransform>(window->contentArea);
    LogGuide::UiTransform* titleTransform   = scene_->GetComponent<LogGuide::UiTransform>(window->titleBar);
    if (!contentTransform || !titleTransform) {
        return;
    }

    // 現在のルート矩形 (今のサーフェスのクライアント座標。切り離しはメインウィンドウ上の
    // ウィンドウにしか起こらないので resolvedSurfaceId == 0 のはず) をスクリーン座標に変換する。
    const Vec2f origin    = nativeWindows_->GetSurfaceScreenOrigin(rootTransform->resolvedSurfaceId);
    const Vec2f screenPos = {origin[X] + rootTransform->resolvedMin[X],
                              origin[Y] + rootTransform->resolvedMin[Y]};

    // OS ウィンドウのクライアントサイズ = 内容領域のサイズ
    // (自作タイトルバー分は OS の枠のタイトルバーと重複するため引く)。
    const Vec2f contentSize = {contentTransform->resolvedMax[X] - contentTransform->resolvedMin[X],
                                contentTransform->resolvedMax[Y] - contentTransform->resolvedMin[Y]};

    LogGuide::NativeWindow::Desc desc{};
    if (TextComponent* titleLabel = scene_->GetComponent<OriGine::TextComponent>(window->titleBar)) {
        desc.title = ConvertString(titleLabel->text); // UTF-8 → UTF-16
    }
    desc.x               = static_cast<int32_t>(screenPos[X]);
    desc.y               = static_cast<int32_t>(screenPos[Y]);
    desc.clientWidth     = std::max(1, static_cast<int32_t>(contentSize[X]));
    desc.clientHeight    = std::max(1, static_cast<int32_t>(contentSize[Y]));
    desc.minClientWidth  = static_cast<int32_t>(window->minSize[X]);
    desc.minClientHeight = std::max(1, static_cast<int32_t>(window->minSize[Y] - LogGuide::kUiWindowTitleBarHeight));

    const int32_t newSurfaceId = nativeWindows_->Open(desc);
    if (newSurfaceId < 0) {
        LOG_ERROR("Failed to open native window for detached UI window.");
        return;
    }
    if (LogGuide::NativeWindow* opened = nativeWindows_->Get(newSurfaceId)) {
        // ウィンドウの縁をドラッグしている間もこの切り離し先ウィンドウの描画が止まらないように、
        // メインウィンドウと同じく Frame() を丸ごと回すコールバックを登録する。
        opened->SetSizeMoveFrameCallback([this]() { Frame(); });
    }

    // ルートを新しいサーフェスへ、全ストレッチのアンカーに切り替える
    // (こうすると OS ウィンドウのリサイズに中身が自動で追従する)。
    rootTransform->surfaceId = newSurfaceId;
    rootTransform->anchorMin = {0.0f, 0.0f};
    rootTransform->anchorMax = {1.0f, 1.0f};
    rootTransform->offsetMin = {0.0f, 0.0f};
    rootTransform->offsetMax = {0.0f, 0.0f};

    // 自作タイトルバーは OS のタイトルバーと重複するので隠し、内容領域からタイトルバー分の
    // オフセットを外す。閉じる/切り離しボタンはタイトルバーの子で、UiLayoutSystem は親の
    // visible を子へ自動では伝播しない (矩形の計算は親が見えなくても続く) ため、
    // 個別に隠さないとタイトルバーの無い場所にボタンだけ浮いてしまう。
    titleTransform->visible     = false;
    contentTransform->offsetMin = {0.0f, 0.0f};
    if (LogGuide::UiTransform* closeButtonTransform = scene_->GetComponent<LogGuide::UiTransform>(window->closeButton)) {
        closeButtonTransform->visible = false;
    }
    if (LogGuide::UiTransform* detachButtonTransform = scene_->GetComponent<LogGuide::UiTransform>(window->detachButton)) {
        detachButtonTransform->visible = false;
    }

    // OS の枠に任せる。
    window->movable   = false;
    window->resizable = false;

    detachedWindowsBySurface_[newSurfaceId] = _root;
}

/// <summary>
/// nativeWindows_->BeginFrame() が破棄してしまう前に、切り離し中のウィンドウが
/// 閉じられようとしていないかを見て、再結合に使うスクリーン矩形を控えておく.
/// </summary>
void TerminalApp::CapturePendingReattachRects() {
    for (const auto& entry : detachedWindowsBySurface_) {
        const int32_t surfaceId          = entry.first;
        LogGuide::NativeWindow* nativeWindow = nativeWindows_->Get(surfaceId);
        if (nativeWindow == nullptr || !nativeWindow->IsCloseRequested()) {
            continue;
        }
        PendingReattachRect rect{};
        rect.screenOrigin              = nativeWindow->ClientToScreenPos(POINT{0, 0});
        rect.clientSize                = nativeWindow->GetClientSize();
        pendingReattachRects_[surfaceId] = rect;
    }
}

/// <summary>
/// このフレームで閉じられたサーフェスに対応する UI ウィンドウを、メインウィンドウへ戻す.
/// (Visual Studio のフローティングウィンドウと同じ挙動。中身のエンティティは破棄しない。)
/// </summary>
void TerminalApp::HandleClosedSurfaces() {
    for (int32_t surfaceId : nativeWindows_->TakeClosedSurfaces()) {
        auto detachedItr = detachedWindowsBySurface_.find(surfaceId);
        if (detachedItr == detachedWindowsBySurface_.end()) {
            continue; // v10 で追跡していないサーフェス (通常は無いはずだが念のため)
        }
        const EntityHandle root = detachedItr->second;
        detachedWindowsBySurface_.erase(detachedItr);

        LogGuide::UiWindow* window           = scene_->GetComponent<LogGuide::UiWindow>(root);
        LogGuide::UiTransform* rootTransform = scene_->GetComponent<LogGuide::UiTransform>(root);
        if (!window || !rootTransform) {
            continue;
        }

        // 閉じる直前のスクリーン矩形 (CapturePendingReattachRects() が控えたもの)。
        // 万一取れていなければ (通常は無い経路)、ウィンドウの最小サイズで適当な位置に置く。
        Vec2f screenOrigin{100.0f, 100.0f};
        Vec2f clientSize = window->minSize;
        auto rectItr     = pendingReattachRects_.find(surfaceId);
        if (rectItr != pendingReattachRects_.end()) {
            screenOrigin = rectItr->second.screenOrigin;
            clientSize   = rectItr->second.clientSize;
            pendingReattachRects_.erase(rectItr);
        }

        const Vec2f mainOrigin = nativeWindows_->GetSurfaceScreenOrigin(0);
        const Vec2f mainSize   = nativeWindows_->GetSurfaceSize(0);

        // 元のウィンドウサイズ = OS ウィンドウのクライアントサイズ + 自作タイトルバーの高さ。
        const Vec2f windowSize = {clientSize[X], clientSize[Y] + LogGuide::kUiWindowTitleBarHeight};

        Vec2f pos = {screenOrigin[X] - mainOrigin[X], screenOrigin[Y] - mainOrigin[Y]};
        // メインウィンドウの外に出るならクランプする。
        pos[X] = std::clamp(pos[X], 0.0f, std::max(0.0f, mainSize[X] - windowSize[X]));
        pos[Y] = std::clamp(pos[Y], 0.0f, std::max(0.0f, mainSize[Y] - windowSize[Y]));

        // 点アンカー + 絶対ピクセルの矩形に戻す。
        rootTransform->surfaceId = 0;
        rootTransform->anchorMin = {0.0f, 0.0f};
        rootTransform->anchorMax = {0.0f, 0.0f};
        rootTransform->offsetMin = pos;
        rootTransform->offsetMax = {pos[X] + windowSize[X], pos[Y] + windowSize[Y]};

        // 自作タイトルバーと閉じる/切り離しボタンを再表示し、内容領域のオフセットを元に戻す。
        // closeButton の visible は closable の値に応じて UiWindowSystem が毎フレーム
        // 設定し直す (タイトルバーが見えていれば) ので、ここではタイトルバーと
        // detachButton (対応する設定項目が無いので常に表示) だけ明示的に戻す。
        if (LogGuide::UiTransform* titleTransform = scene_->GetComponent<LogGuide::UiTransform>(window->titleBar)) {
            titleTransform->visible = true;
        }
        if (LogGuide::UiTransform* detachButtonTransform = scene_->GetComponent<LogGuide::UiTransform>(window->detachButton)) {
            detachButtonTransform->visible = true;
        }
        if (LogGuide::UiTransform* contentTransform = scene_->GetComponent<LogGuide::UiTransform>(window->contentArea)) {
            contentTransform->offsetMin = {0.0f, LogGuide::kUiWindowTitleBarHeight};
        }

        window->movable   = true;
        window->resizable = true;

        // 再結合したウィンドウを前面へ。
        if (LogGuide::UiWindowSystem* windowSystem =
                scene_->GetSystemRunnerRef()->GetSystem<LogGuide::UiWindowSystem>()) {
            windowSystem->BringWindowToFront(root);
        }
    }
}

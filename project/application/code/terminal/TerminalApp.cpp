#include "TerminalApp.h"

#include <string> // std::to_string (クリック回数の文字列化)

#define ENGINE_INCLUDE
#define RESOURCE_DIRECTORY
#include <EngineInclude.h>

#include "globalVariables/GlobalVariables.h"
#include "scene/SceneManager.h"

/// engine (window handle)
#include "winApp/WinApp.h"
#include "logger/Logger.h"

/// engine (ECS シーンを直接立ち上げて自作 UI を描画するため)
#include "input/InputManager.h"
#include "scene/Scene.h"
#include "system/SystemRunner.h"

/// module（全構成で有効）
#include "SessionCatalog.h"

/// UI（ImGui に依存しない自作 UI。ECS 上のコンポーネント/システムとして実装している）
#include "ui/component/UiHighlight.h"
#include "ui/component/UiInteractable.h"
#include "ui/component/UiRect.h"
#include "ui/component/UiText.h"
#include "ui/component/UiTransform.h"
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

    // --- ウィンドウ A: 中にボタンを 1 つ置く ---
    LogGuide::UiWindowHandles windowA = LogGuide::CreateUiWindow(
        scene_.get(), runner, "ウィンドウ A", {60.0f, 60.0f}, {320.0f, 200.0f}, 0);

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
    LogGuide::UiWindowHandles windowB = LogGuide::CreateUiWindow(
        scene_.get(), runner, "ウィンドウ B", {200.0f, 140.0f}, {320.0f, 200.0f}, 1);

    OriGine::EntityHandle overflowText = scene_->CreateEntity("UiOverflowText");
    scene_->AddComponent<LogGuide::UiTransform>(overflowText);
    scene_->AddComponent<LogGuide::UiText>(overflowText);
    scene_->AddComponent<OriGine::TextComponent>(overflowText);

    if (LogGuide::UiTransform* overflowTransform =
            scene_->GetComponent<LogGuide::UiTransform>(overflowText)) {
        overflowTransform->parent    = windowB.contentArea;
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
            scene_->GetComponent<OriGine::TextComponent>(overflowText)) {
        overflowLabel->text     = "この文章はウィンドウの内容領域より横に広い要素に載せてあります。"
                                   "ウィンドウの縁で切られていれば、クリッピングが効いています。"
                                   "ウィンドウを動かしても切られる位置が付いてきます。";
        overflowLabel->fontSize = 14.0f;
        overflowLabel->align    = OriGine::TextAlign::Left;
        overflowLabel->color    = {0.85f, 0.88f, 0.94f, 1.0f};
        overflowLabel->dirty    = true;
    }
    runner->RegisterEntity<LogGuide::UiLayoutSystem,
                            LogGuide::UiRenderSystem>(overflowText);

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

    scene_->Update(); // Input → StateTransition → Movement → Collision → Effect

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
    scene_->GetSystemRunnerRef()->UpdateCategory(SystemCategory::Render);
    engine_->ScreenPostDraw();
}

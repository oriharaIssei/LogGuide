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
#include "ui/system/UiRectRenderSystem.h"

/// UI（v2: テキスト描画は自作せず engine の TextComponent / TextRenderSystem をそのまま使う）
#include "component/text/TextComponent.h"
#include "system/text/TextRenderSystem.h"

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
    runner->RegisterSystem<LogGuide::UiRectRenderSystem>(0, true, true);
    // SystemRunner::ActivateSystem の並べ替えは std::sort（非安定）なので、
    // 矩形の後にテキストを描くには priority を明示的に大きくする必要がある。
    runner->RegisterSystem<OriGine::TextRenderSystem>(1, true, true);
    // v3: ヒットテストと色変化。Input / StateTransition カテゴリで、Render カテゴリの
    // 矩形描画系とは実行順が競合しないため priority はどちらも 0 でよい。
    runner->RegisterSystem<LogGuide::UiInteractionSystem>(0, true, true);
    runner->RegisterSystem<LogGuide::UiHighlightSystem>(0, true, true);

    // 動作確認用のパネルを 1 枚だけ置く。クリック回数表示のため Run() からも参照するので
    // メンバ panel_ に持たせる。
    panel_ = scene_->CreateEntity("UiPanel");
    scene_->AddComponent<LogGuide::UiTransform>(panel_);
    scene_->AddComponent<LogGuide::UiRect>(panel_);
    scene_->AddComponent<LogGuide::UiText>(panel_);
    scene_->AddComponent<OriGine::TextComponent>(panel_);
    scene_->AddComponent<LogGuide::UiInteractable>(panel_);
    scene_->AddComponent<LogGuide::UiHighlight>(panel_);

    // 画面中央に 480x140。テスト文字列が折り返さない幅にしてある
    // (FiraMono 28px の送り幅は約 17px、CJK フォールバックは約 28px)。
    LogGuide::UiTransform* transform = scene_->GetComponent<LogGuide::UiTransform>(panel_);
    if (transform) {
        transform->anchorMin = {0.5f, 0.5f};
        transform->anchorMax = {0.5f, 0.5f};
        transform->offsetMin = {-240.0f, -70.0f};
        transform->offsetMax = {240.0f, 70.0f};
        // v4: 親をクリップ有効にする（子がはみ出した分が切られるようになる）
        transform->clipChildren = true;
    }

    // 動作確認用の文字列。v3 ではボタンとして押せることを示すため、初期ラベルをクリック
    // 誘導文言にする（日本語のままなので、グリフの動的追加とアトラス再アップロードの確認は引き続き兼ねる）。
    // fillColor は UiHighlightSystem が毎フレーム上書きするので、UiRect 側の初期値は指定しない。
    // fontHandle は既定の kInvalidFontHandle のままでよい（FontManager が既定フォントに落とす）。
    TextComponent* text = scene_->GetComponent<OriGine::TextComponent>(panel_);
    if (text) {
        text->text     = "クリックしてください";
        text->fontSize = 28.0f;
        text->align    = OriGine::TextAlign::Center; // 水平方向
        text->color    = {0.92f, 0.94f, 0.98f, 1.0f};
        text->dirty    = true;
    }

    runner->RegisterEntity<LogGuide::UiLayoutSystem,
                            LogGuide::UiRectRenderSystem,
                            LogGuide::UiInteractionSystem,
                            LogGuide::UiHighlightSystem,
                            OriGine::TextRenderSystem>(panel_);

    // v4: 動作確認用の子要素。親パネルの下側に、左右へわざとはみ出す帯を置く。
    // clipChildren が効いていれば、はみ出した分はパネルの縁でぴったり切られる。
    // 親に追従するので、ウィンドウをリサイズしてもパネルとの位置関係は保たれる。
    // UiInteractable は持たせない（ボタンのクリック判定には影響しない）。
    childBar_ = scene_->CreateEntity("UiChildBar");
    scene_->AddComponent<LogGuide::UiTransform>(childBar_);
    scene_->AddComponent<LogGuide::UiRect>(childBar_);

    if (LogGuide::UiTransform* bar = scene_->GetComponent<LogGuide::UiTransform>(childBar_)) {
        bar->parent = panel_;
        // 親矩形の下寄り。左右に 60px ずつはみ出させる。
        bar->anchorMin      = {0.0f, 1.0f};
        bar->anchorMax      = {1.0f, 1.0f};
        bar->offsetMin      = {-60.0f, -34.0f};
        bar->offsetMax      = {60.0f, -12.0f};
        bar->renderPriority = 10; // パネルより手前
    }
    if (LogGuide::UiRect* barRect = scene_->GetComponent<LogGuide::UiRect>(childBar_)) {
        barRect->fillColor    = {0.35f, 0.62f, 0.95f, 1.0f};
        barRect->borderWidth  = 0.0f;
        barRect->cornerRadius = {4.0f, 4.0f, 4.0f, 4.0f};
    }

    runner->RegisterEntity<LogGuide::UiLayoutSystem,
                            LogGuide::UiRectRenderSystem>(childBar_);

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
            scene_->GetComponent<LogGuide::UiInteractable>(panel_)) {
        if (interactable->wasClicked) {
            ++clickCount_;
            if (TextComponent* text = scene_->GetComponent<OriGine::TextComponent>(panel_)) {
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

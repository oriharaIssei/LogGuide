# Application-Template

OriGine Engine を使用したアプリケーションプロジェクトの雛形です。
Engine を submodule として取り込み、最小構成の FrameWork / main.cpp / premake 設定を提供します。

---

## ディレクトリ構成

```
Application-Template/
├── README.md
├── .gitignore
├── .gitmodules              # Engine submodule 用 (setup.ps1 が記述)
├── setup.ps1                # 初回セットアップ (名前置換・submodule追加・premake)
├── premake.ps1              # premake 実行ラッパ
└── project/
    ├── config/
    │   └── premake5.lua     # workspace + App project 定義 (Engine は include)
    ├── engine/              # submodule: Engine リポジトリ (setup.ps1 で追加)
    │   └── premake.lua.example   # Engine 側に用意すべき premake.lua の参考
    └── application/
        ├── code/
        │   ├── shared/          # LogGuideCore (StaticLib) — 全アプリが共有
        │   │   ├── FrameWork.{h,cpp}
        │   │   ├── analysis/    # AnalysisConfig, TimelineEvent, TimelineJsonl{Reader,Writer},
        │   │   │                #  LocalLLM, SessionSummarizer
        │   │   ├── recording/   # RecordingComponents (SessionInfo/TrackInfo),
        │   │   │                #  SessionManifest (session.json の読み書き)
        │   │   ├── component/ComponentTemplate.txt
        │   │   ├── system/SystemTemplate.txt
        │   │   └── manager/
        │   ├── terminal/        # LogGuideTerminal.exe — スタートアップのランチャー
        │   │   ├── main.cpp
        │   │   ├── TerminalApp.{h,cpp}
        │   │   ├── TerminalPanel.{h,cpp}   # 起動先を選ぶ ImGui UI
        │   │   ├── SessionCatalog.{h,cpp}  # recordings/ の走査
        │   │   └── AppLauncher.{h,cpp}     # 兄弟 exe の起動
        │   ├── recorder/        # LogGuideRecorder.exe — 録画アプリ
        │   │   ├── main.cpp
        │   │   ├── RecorderApp.{h,cpp}
        │   │   ├── recording/   # RecordingSystem, RecordingPanel
        │   │   └── analysis/    # AudioAnalysisPipeline, VideoAnalysisPipeline,
        │   │                    #  WhisperTranscriber, CrossModalCorrelator ほか
        │   └── player/          # LogGuidePlayer.exe — 再生アプリ
        │       ├── main.cpp
        │       ├── PlayerApp.{h,cpp}
        │       ├── playback/    # DualPlayerController, PlayerPanel, VideoTexture, FileDialog
        │       └── analysis/    # SummaryService
        ├── externals/           # whisper.cpp / llama.cpp (CUDA 付きで別途 CMake ビルド)
        └── resource/            # whisper / llm モデル, GlobalVariables
        # cookedResource/ は AssetCooker による成果物のためローカル生成 (gitignore)
```

---

## アプリケーション構成

LogGuide は **ターミナル**・**録画**・**再生** の 3 つの独立した実行ファイルに分かれています。

| プロジェクト       | 種別        | 役割                                                           | 外部依存                 |
| ------------------ | ----------- | -------------------------------------------------------------- | ------------------------ |
| `LogGuideCore`     | StaticLib   | 全アプリ共有 (FrameWork / タイムライン JSONL / session.json / LocalLLM) | llama.cpp        |
| `LogGuideTerminal` | WindowedApp | スタートアップのランチャー。録画/再生を選んで起動する           | なし                     |
| `LogGuideRecorder` | WindowedApp | 画面・カメラ・音声のキャプチャと録画中のリアルタイム AI 解析    | whisper.cpp + llama.cpp  |
| `LogGuidePlayer`   | WindowedApp | camera.mp4 / screen.mp4 の同期再生とタイムライン閲覧・要約生成  | llama.cpp                |

### 起動フロー

```
LogGuideTerminal.exe          ← スタートアッププロジェクト
   ├─ [レコーダーを起動]  → LogGuideRecorder.exe
   └─ [セッションを選択]  → LogGuidePlayer.exe "<recordings/session_*/session.json>"
                              ↑ 引数を渡すとそのセッションを開いた状態で起動する
```

ターミナルは起動したら自身を終了します。3 つの exe は同じ出力ディレクトリに並ぶため、
ターミナルは自分の exe と同じ場所から兄弟 exe を探して起動します。
作業ディレクトリ (`project/`) は子プロセスへ引き継がれます。

再生アプリは文字起こしを行わないため whisper.cpp をリンクしません。
「要約を生成」だけは `SummaryService` (LocalLLM) が担当します。
ターミナルは `LogGuideCore` をリンクしますが、参照するのは `SessionManifest` と
`FrameWork` だけなので llama.cpp は不要です。

両アプリの作業ディレクトリは `project/` で共通のため、ImGui のウィンドウ配置は
`imgui_recorder.ini` / `imgui_player.ini` に分けています。

> UI (ImGui / EditorController) はエンジン側が `_DEBUG` 限定のため、
> パネルが表示されるのは **Debug 構成のみ**です。Develop / Release でも
> ビルドとリンクは通ります。

---

## クイックスタート

### 1. GitHub の Template Repository 機能で新規作成

GitHub 上で本リポジトリを **Template** に設定し、`Use this template` から新規リポジトリを生成するか、
コマンドラインから:

```powershell
gh repo create MyGame --template <user>/Application-Template --private --clone
cd MyGame
```

### 2. セットアップ実行

```powershell
.\setup.ps1 -AppName "MyGame" -EngineRepo "https://github.com/<user>/Engine.git"
```

`setup.ps1` は次を行います:
1. `LogGuide` をファイル内容・ファイル名の両方で `MyGame` に置換
2. Engine を `project/engine` に git submodule として追加
3. `premake.ps1` を実行して Visual Studio ソリューションを生成

### 3. ビルド

`project/LogGuide.slnx` を Visual Studio で開き、`Debug` / `Develop` / `Release` のいずれかをビルド。

スタートアッププロジェクトは `LogGuideTerminal` です。ここから録画/再生を選んで起動します。
個別に起動したい場合はソリューションエクスプローラで該当プロジェクトを
スタートアップに設定してください。3 つの exe はすべて `generated/output/<構成>/` に出力されます。

---

## Engine リポジトリ側で必要な準備

本テンプレートが想定する Engine 構成:

```
Engine/
├── premake.lua              # defineEngineProjects / getEngineIncludeDirs / getEngineLinks を export
├── code/                    # Engine 本体ソース
├── math/ util/ editor/ tool/
├── externals/               # DirectXTex, imgui, assimp 等
└── ...
```

`premake.lua` の実装例は [OriGine Engine リポジトリ](https://github.com/oriharaIssei/OriGine) の
ルート `premake.lua` を参照してください。
Engine 側に `premake.lua` を配置すると、Application 側の `project/config/premake5.lua` から
`include "engine/premake.lua"` 経由で Engine/DirectXTex/imgui の project 定義を再利用できます。

---

## setup.ps1 のオプション

| オプション         | 説明                                                   |
| ------------------ | ------------------------------------------------------ |
| `-AppName`         | **必須** 識別子形式のアプリ名 (例: `MyGame`)           |
| `-EngineRepo`      | Engine リポジトリの URL                                 |
| `-EngineBranch`    | Engine の追跡ブランチ (既定: `main`)                   |
| `-SkipSubmodule`   | submodule 追加をスキップ                               |
| `-SkipPremake`     | premake 実行をスキップ                                 |

---

## よくあるフロー

### 既存の OriGine リポジトリから Engine 分離後、初めて使う

1. Engine リポジトリを作成し、`premake.lua` を `premake.lua.example` を参考に配置
2. 本テンプレートから新規リポジトリを生成
3. `setup.ps1` を実行

### Engine 側を最新化したい

```powershell
cd project\engine
git pull origin main
cd ..\..
git add project/engine
git commit -m "Update Engine submodule"
```

---

## TODO / 既知の制約

- [ ] Engine 側の `premake.lua` 実装 (テンプレート側では `.example` のみ提供)
- [ ] premake5.exe の配置方法 (Engine 側 externals に入れるか、setup で取得するか要検討)
- [ ] `resource/` の初期アセット一式 (`cookedResource/` は AssetCooker 生成物のため対象外)

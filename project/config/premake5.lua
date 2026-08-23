-- ==========================================================================
-- LogGuide Workspace Premake5 Script
-- ==========================================================================
-- LogGuide は 3 つの独立した実行ファイルで構成される。
--
--   LogGuideCore     (StaticLib)   全アプリが共有するコード
--                                  FrameWork / タイムライン JSONL / AnalysisConfig /
--                                  LocalLLM / SessionSummarizer / SessionManifest
--   LogGuideTerminal (WindowedApp) スタートアップのランチャー。録画/再生を選んで起動する
--   LogGuideRecorder (WindowedApp) 録画とリアルタイム解析。whisper.cpp + llama.cpp
--   LogGuidePlayer   (WindowedApp) 2 動画同期再生とタイムライン閲覧。llama.cpp のみ
--
-- Engine 側の project 定義 (OriGine / DirectXTex / imgui) は
-- engine リポジトリ (submodule) 内の premake から include する。
-- ==========================================================================

-- 先頭で作業ディレクトリを project に変更
os.chdir(_SCRIPT_DIR .. "/..")

-- Engine 側のヘルパを読み込み (Engine repo のルート直下 premake.lua が
-- defineEngineProjects() / getEngineIncludeDirs() / getEngineLinks() を export する)
include "engine/premake.lua"

workspace "LogGuide"
    architecture "x86_64"
    configurations { "Debug", "Develop", "Release" }
    startproject "LogGuideTerminal"

-- ==========================================================================
-- Engine Projects (Engine 側の premake.lua が定義する)
-- ==========================================================================
defineEngineProjects()

-- ==========================================================================
-- Application 共通ヘルパ
-- --------------------------------------------------------------------------
-- 3 つの App プロジェクトはすべて location "application" に置くため、
-- $(ProjectDir) はいずれも project/application/ を指す。
-- ==========================================================================

-- whisper.cpp / llama.cpp のヘッダ (CMake で別途 CUDA 付きビルド済み)
local whisperIncludeDirs = {
    "$(SolutionDir)application/externals/whisper.cpp/include",
    "$(SolutionDir)application/externals/whisper.cpp/ggml/include",
}
local llamaIncludeDirs = {
    "$(SolutionDir)application/externals/llama.cpp/include",
    "$(SolutionDir)application/externals/llama.cpp/ggml/include",
}

-- CUDA 付きビルド成果物 (.lib) の出力先。lib は "whisper.cpp" / "llama.cpp"、
-- cfg は "debug" / "release" (scripts/build_*_cuda.bat が作るディレクトリ名に対応)。
local function ggmlLibDirs(lib, cfg)
    local root = "application/externals/" .. lib .. "/build_cuda_" .. cfg
    return {
        root .. "/src",
        root .. "/ggml/src",
        root .. "/ggml/src/ggml-cuda",
    }
end

-- 3 プロジェクト共通のコンパイル設定。project スコープの中で呼ぶこと。
local function applyCommonBuildSettings()
    warnings "Extra"
    multiprocessorcompile "On"
    buildoptions { "/utf-8", "/bigobj" }

    filter "configurations:Debug"
        defines { "DEBUG", "_DEBUG" }
        symbols "On"
        runtime "Debug"
        staticruntime "On"

    filter "configurations:Develop"
        defines { "DEVELOP", "_DEVELOP" }
        symbols "On"
        runtime "Release"
        staticruntime "On"

    filter "configurations:Release"
        defines { "NDEBUG", "_RELEASE", "RELEASE" }
        optimize "Full"
        runtime "Release"
        staticruntime "On"

    filter "system:windows"
        cppdialect "C++20"
        systemversion "latest"

    filter {}
end

-- 実行ファイル側だけが必要とする assimp のリンク設定。
local function applyAssimpLinks()
    filter "configurations:Debug"
        libdirs { "engine/externals/assimp/lib/Debug" }
        links { "assimp-vc143-mtd" }

    filter "configurations:Develop or Release"
        libdirs { "engine/externals/assimp/lib/Release" }
        links { "assimp-vc143-mt" }

    filter {}
end

-- 両アプリが共通でリンクする GPU 系ライブラリ (llama.cpp を CUDA 付きでビルドしている)。
local commonGpuLinks = {
    "llama", "ggml", "ggml-base", "ggml-cpu", "ggml-cuda",
    "cudart_static", "cublas", "cublasLt", "cuda",
}

-- ==========================================================================
-- LogGuideCore : 両アプリが共有する StaticLib
-- --------------------------------------------------------------------------
-- ここには「録画にも再生にも要るもの」だけを置く。
-- 逆に、このライブラリから recorder/ や player/ を参照してはならない。
-- ==========================================================================
project "LogGuideCore"
    kind "StaticLib"
    language "C++"
    location "application"
    targetdir "../generated/output/%{cfg.buildcfg}/"
    objdir "../generated/obj/%{cfg.buildcfg}/LogGuideCore/"
    debugdir "%{wks.location}"

    files {
        "application/code/shared/**.h",
        "application/code/shared/**.cpp",
    }

    clangtidy "On"

    -- LocalLLM が llama.cpp のヘッダを要する。whisper.cpp は録画側専用なので含めない。
    includedirs(table.join(
        {
            "$(ProjectDir)code/shared",
            "$(ProjectDir)",
        },
        llamaIncludeDirs,
        getEngineIncludeDirs()
    ))

    applyCommonBuildSettings()

-- ==========================================================================
-- LogGuideTerminal : スタートアップのランチャー
-- --------------------------------------------------------------------------
-- 録画アプリを起動するか、録画セッションを選んで再生アプリを起動するかを選ばせ、
-- 起動したら自身は終了する。3 つの exe は同じ出力ディレクトリに並ぶため、
-- 自分の exe と同じ場所から兄弟 exe を探して CreateProcess する。
--
-- whisper.cpp / llama.cpp はリンクしない。LogGuideCore は静的ライブラリなので、
-- 参照していないオブジェクト (LocalLLM 等) はリンカに取り込まれず、
-- ランチャーが必要とするのは SessionManifest と FrameWork だけで済む。
-- ==========================================================================
project "LogGuideTerminal"
    kind "WindowedApp"
    language "C++"
    location "application"
    targetdir "../generated/output/%{cfg.buildcfg}/"
    objdir "../generated/obj/%{cfg.buildcfg}/LogGuideTerminal/"
    debugdir "%{wks.location}"

    files {
        "application/code/terminal/**.h",
        "application/code/terminal/**.cpp",
    }

    clangtidy "On"

    includedirs(table.join(
        {
            "$(ProjectDir)code/shared",
            "$(ProjectDir)code/terminal",
            "$(ProjectDir)",
        },
        getEngineIncludeDirs()
    ))

    dependson { "LogGuideCore", "OriGine", "DirectXTex", "imgui" }

    links(table.join(
        { "LogGuideCore", "OriGine" },
        getEngineLinks()
    ))

    applyCommonBuildSettings()
    applyAssimpLinks()

-- ==========================================================================
-- LogGuideRecorder : 録画アプリ
-- --------------------------------------------------------------------------
-- 画面/カメラ/音声のキャプチャと、録画中のリアルタイム AI 解析を担当する。
-- 文字起こしに whisper.cpp を使うため、両ライブラリをリンクする。
-- ==========================================================================
project "LogGuideRecorder"
    kind "WindowedApp"
    language "C++"
    location "application"
    targetdir "../generated/output/%{cfg.buildcfg}/"
    objdir "../generated/obj/%{cfg.buildcfg}/LogGuideRecorder/"
    debugdir "%{wks.location}"

    files {
        "application/code/recorder/**.h",
        "application/code/recorder/**.cpp",
    }

    clangtidy "On"

    includedirs(table.join(
        {
            "$(ProjectDir)code/shared",
            "$(ProjectDir)code/recorder",
            "$(ProjectDir)",
        },
        whisperIncludeDirs,
        llamaIncludeDirs,
        getEngineIncludeDirs()
    ))

    dependson { "LogGuideCore", "OriGine", "DirectXTex", "imgui" }

    -- CUDA ランタイム
    libdirs { "$(CUDA_PATH)/lib/x64" }

    links(table.join(
        { "LogGuideCore", "OriGine", "whisper" },
        commonGpuLinks,
        getEngineLinks()
    ))

    -- whisper.cpp / llama.cpp の CUDA 付きビルド成果物 (.lib)
    filter "configurations:Debug"
        libdirs(table.join(
            ggmlLibDirs("whisper.cpp", "debug"),
            ggmlLibDirs("llama.cpp", "debug")
        ))

    filter "configurations:Develop or Release"
        libdirs(table.join(
            ggmlLibDirs("whisper.cpp", "release"),
            ggmlLibDirs("llama.cpp", "release")
        ))

    filter {}

    applyCommonBuildSettings()
    applyAssimpLinks()

-- ==========================================================================
-- LogGuidePlayer : 再生アプリ
-- --------------------------------------------------------------------------
-- 録画済みセッション (camera.mp4 / screen.mp4 / session.json / *.jsonl) を
-- 同期再生し、AI 解析タイムラインを閲覧する。
-- 文字起こしは行わないため whisper.cpp はリンクしない。
-- 「要約を生成」だけは LocalLLM を使うので llama.cpp はリンクする。
-- ==========================================================================
project "LogGuidePlayer"
    kind "WindowedApp"
    language "C++"
    location "application"
    targetdir "../generated/output/%{cfg.buildcfg}/"
    objdir "../generated/obj/%{cfg.buildcfg}/LogGuidePlayer/"
    debugdir "%{wks.location}"

    files {
        "application/code/player/**.h",
        "application/code/player/**.cpp",
    }

    clangtidy "On"

    includedirs(table.join(
        {
            "$(ProjectDir)code/shared",
            "$(ProjectDir)code/player",
            "$(ProjectDir)",
        },
        llamaIncludeDirs,
        getEngineIncludeDirs()
    ))

    dependson { "LogGuideCore", "OriGine", "DirectXTex", "imgui" }

    -- CUDA ランタイム
    libdirs { "$(CUDA_PATH)/lib/x64" }

    links(table.join(
        { "LogGuideCore", "OriGine" },
        commonGpuLinks,
        getEngineLinks()
    ))

    -- llama.cpp の CUDA 付きビルド成果物 (.lib) のみ
    filter "configurations:Debug"
        libdirs(ggmlLibDirs("llama.cpp", "debug"))

    filter "configurations:Develop or Release"
        libdirs(ggmlLibDirs("llama.cpp", "release"))

    filter {}

    applyCommonBuildSettings()
    applyAssimpLinks()

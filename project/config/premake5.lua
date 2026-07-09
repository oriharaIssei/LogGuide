-- ==========================================================================
-- LogGuide Workspace Premake5 Script
-- ==========================================================================
-- このファイルはテンプレートから生成されています。
-- - Application 固有の premake 記述のみをここに書く
-- - Engine 側の project 定義 (OriGine / DirectXTex / imgui 等) は
--   engine リポジトリ (submodule) 内の premake から include する想定
-- ==========================================================================

-- 先頭で作業ディレクトリを project に変更
os.chdir(_SCRIPT_DIR .. "/..")

-- Engine 側のヘルパを読み込み (Engine 側で premake.lua を提供する)
-- 例: Engine repo のルート直下に premake.lua を置き、
--     defineEngineProjects() / getEngineIncludeDirs() / getEngineLinks() を export する
include "engine/premake.lua"

workspace "LogGuide"
    architecture "x86_64"
    configurations { "Debug", "Develop", "Release" }
    startproject "LogGuide"

-- ==========================================================================
-- Engine Projects (Engine 側の premake.lua が定義する)
-- ==========================================================================
defineEngineProjects()

-- ==========================================================================
-- Application Project
-- ==========================================================================
project "LogGuide"
    kind "WindowedApp"
    language "C++"
    location "application"
    targetdir "../generated/output/%{cfg.buildcfg}/"
    objdir "../generated/obj/%{cfg.buildcfg}/LogGuide/"
    debugdir "%{wks.location}"
    files { "application/**.h", "application/**.cpp" }
    -- whisper.cpp / llama.cpp のソースはアプリのビルド対象に含めない
    -- (CMake で別途 CUDA 付きビルド済みの .lib をリンクする)
    removefiles { "application/externals/**" }

    clangtidy "On"

    includedirs(table.join(
        {
            "$(ProjectDir)code",
            "$(ProjectDir)",
            "$(SolutionDir)application/externals/whisper.cpp/include",
            "$(SolutionDir)application/externals/whisper.cpp/ggml/include",
            "$(SolutionDir)application/externals/llama.cpp/include",
            "$(SolutionDir)application/externals/llama.cpp/ggml/include",
        },
        getEngineIncludeDirs()
    ))

    dependson { "DirectXTex", "imgui" }
    -- CUDA ランタイム (whisper.cpp / llama.cpp を CUDA 付きでビルドしているため)
    libdirs { "$(CUDA_PATH)/lib/x64" }
    links(table.join(
        { "OriGine",
          "whisper", "llama", "ggml", "ggml-base", "ggml-cpu", "ggml-cuda",
          "cudart_static", "cublas", "cublasLt", "cuda" },
        getEngineLinks()
    ))

    -- whisper.cpp / llama.cpp の CUDA 付きビルド成果物 (.lib)
    filter "configurations:Debug"
        libdirs {
            "application/externals/whisper.cpp/build_cuda_debug/src",
            "application/externals/whisper.cpp/build_cuda_debug/ggml/src",
            "application/externals/whisper.cpp/build_cuda_debug/ggml/src/ggml-cuda",
            "application/externals/llama.cpp/build_cuda_debug/src",
            "application/externals/llama.cpp/build_cuda_debug/ggml/src",
            "application/externals/llama.cpp/build_cuda_debug/ggml/src/ggml-cuda",
        }
    filter "configurations:Develop or Release"
        libdirs {
            "application/externals/whisper.cpp/build_cuda_release/src",
            "application/externals/whisper.cpp/build_cuda_release/ggml/src",
            "application/externals/whisper.cpp/build_cuda_release/ggml/src/ggml-cuda",
            "application/externals/llama.cpp/build_cuda_release/src",
            "application/externals/llama.cpp/build_cuda_release/ggml/src",
            "application/externals/llama.cpp/build_cuda_release/ggml/src/ggml-cuda",
        }
    filter {}

    warnings "Extra"
    multiprocessorcompile "On"
    buildoptions { "/utf-8", "/bigobj" }

    filter "configurations:Debug"
        defines { "DEBUG", "_DEBUG" }
        symbols "On"
        runtime "Debug"
        libdirs { "engine/externals/assimp/lib/Debug" }
        links { "assimp-vc143-mtd" }
        staticruntime "On"

    filter "configurations:Develop"
        defines { "DEVELOP", "_DEVELOP" }
        symbols "On"
        runtime "Release"
        libdirs { "engine/externals/assimp/lib/Release" }
        links { "assimp-vc143-mt" }
        staticruntime "On"

    filter "configurations:Release"
        defines { "NDEBUG", "_RELEASE", "RELEASE" }
        optimize "Full"
        runtime "Release"
        libdirs { "engine/externals/assimp/lib/Release" }
        links { "assimp-vc143-mt" }
        staticruntime "On"

    filter "system:windows"
        cppdialect "C++20"
        systemversion "latest"

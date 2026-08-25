#include "FrameWork.h"

/// engine include
#define ENGINE_INCLUDE
#define ENGINE_ECS
#define ENGINE_COMPONENTS
#define ENGINE_SYSTEMS
#include <EngineInclude.h>

/// engine
#include "Engine.h"
#include "directX12/DxSwapChain.h"
#include "globalVariables/GlobalVariables.h"

/// app
#include "ui/UiRegistration.h"

using namespace OriGine;

FrameWork::FrameWork() = default;
FrameWork::~FrameWork() = default;

void FrameWork::ApplyWindowSettings()
{
#ifdef _DEBUG
    return;
#endif

    GlobalVariables *gv = GlobalVariables::GetInstance();
    auto *scene = gv->GetScene("Settings");
    if (!scene)
    {
        return;
    }
    auto groupItr = scene->find("WindowState");
    if (groupItr == scene->end())
    {
        return;
    }

    float r = *gv->AddValue<float>("Settings", "WindowState", "ClearColorR", 0.0f);
    float g = *gv->AddValue<float>("Settings", "WindowState", "ClearColorG", 0.0f);
    float b = *gv->AddValue<float>("Settings", "WindowState", "ClearColorB", 0.0f);
    float a = *gv->AddValue<float>("Settings", "WindowState", "ClearColorA", 0.0f);
    Engine::GetInstance()->GetDxSwapChain()->SetClearColor(Vec4f(r, g, b, a));
}

void RegisterUsingComponents()
{
    ComponentRegistry *componentRegistry = ComponentRegistry::GetInstance();
    LogGuide::RegisterUiComponents();
}

void RegisterUsingSystems()
{
    SystemRegistry *systemRegistry = SystemRegistry::GetInstance();

    LogGuide::RegisterUiSystems();
}

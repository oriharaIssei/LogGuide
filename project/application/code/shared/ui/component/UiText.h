#pragma once

#include "component/IComponent.h"

#include <Vector2.h>
#include <Vector4.h>
#include <cstdint>
#include <string>

namespace LogGuide {

/// 垂直方向の寄せ. engine の TextAlign は水平方向専用なので、こちらで補う.
enum class UiTextVerticalAlign : uint8_t {
    Top,
    Middle,
    Bottom,
};

/// UiTransform の矩形に対して TextComponent をどう配置するかを決めるコンポーネント.
/// 文字列・フォント・色・サイズは engine の TextComponent が持つ。
/// 水平方向の寄せも TextComponent::align をそのまま使う。
/// このコンポーネントは矩形内での位置決めだけを担当する.
class UiText final : public OriGine::IComponent {
public:
    UiText()           = default;
    ~UiText() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    /// 矩形の内側の余白 (px). (左, 上, 右, 下)
    OriGine::Vec4f padding            = {8.0f, 4.0f, 8.0f, 4.0f};
    UiTextVerticalAlign verticalAlign = UiTextVerticalAlign::Middle;

    // --- 実行時の状態 (UiLayoutSystem が書き込む。JSON には保存しない) ---
    /// 直近に計測したテキストの高さ (px)
    float measuredHeight = 0.0f;
    /// 前フレームの内容領域サイズ。変化検出用。初期値は「未計測」を表す負値
    OriGine::Vec2f lastContentSize = {-1.0f, -1.0f};
};

void to_json(nlohmann::json& _j, const UiText& _c);
void from_json(const nlohmann::json& _j, UiText& _c);

} // namespace LogGuide

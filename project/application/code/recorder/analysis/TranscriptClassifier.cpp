#include "analysis/TranscriptClassifier.h"

#include "analysis/LocalLLM.h"

/// externals
#include <nlohmann/json.hpp>

/// stl
#include <array>
#include <string>

namespace LogGuide {

namespace {

constexpr std::array<const char*, 5> kValidTags = {
    "confusion", "frustration", "delight", "surprise", "none",
};

bool IsValidTag(const std::string& tag) {
    for (const char* t : kValidTags) {
        if (tag == t) {
            return true;
        }
    }
    return false;
}

} // namespace

bool TranscriptClassifier::IsAvailable() const {
    return llm_ != nullptr && llm_->IsModelLoaded();
}

std::string TranscriptClassifier::BuildPrompt(const std::string& text) const {
    // シンプルな指示 + 少数 shot。JSON のみを要求する。
    // 特定モデルへ過適合しないよう、タグ集合と出力形式だけを明示する。
    std::string p =
        "あなたはユーザーテストの発話を分類する分類器です。\n"
        "次の発話を、UX 上の反応として最も近いタグ 1 つに分類してください。\n"
        "タグ: confusion(迷い), frustration(不満), delight(喜び), surprise(驚き), none(該当なし)\n"
        "出力は必ず JSON のみ。形式: {\"tag\":\"<タグ>\",\"confidence\":<0〜1の小数>}\n"
        "\n"
        "例:\n"
        "発話: 「え、どこ行けばいいの？」 -> {\"tag\":\"confusion\",\"confidence\":0.9}\n"
        "発話: 「うわ、なんでこれ動かないんだよ」 -> {\"tag\":\"frustration\",\"confidence\":0.85}\n"
        "発話: 「お、いいねこれ！」 -> {\"tag\":\"delight\",\"confidence\":0.8}\n"
        "発話: 「えっ、そうなるの!?」 -> {\"tag\":\"surprise\",\"confidence\":0.7}\n"
        "発話: 「次のステージに進む」 -> {\"tag\":\"none\",\"confidence\":0.6}\n"
        "\n"
        "発話: 「";
    p += text;
    p += "」 -> ";
    return p;
}

bool TranscriptClassifier::ParseResponse(const std::string& raw, Result& out) const {
    // 最初の '{' から対応する '}' までを取り出す（前後の余計な文を無視）。
    size_t begin = raw.find('{');
    if (begin == std::string::npos) {
        return false;
    }
    size_t end = raw.find('}', begin);
    if (end == std::string::npos) {
        return false;
    }
    const std::string jsonStr = raw.substr(begin, end - begin + 1);

    nlohmann::json j = nlohmann::json::parse(jsonStr, nullptr, false);
    if (j.is_discarded() || !j.contains("tag")) {
        return false;
    }

    std::string tag = j.value("tag", "");
    if (!IsValidTag(tag)) {
        return false;
    }
    double confidence = 0.0;
    if (j.contains("confidence") && j["confidence"].is_number()) {
        confidence = j["confidence"].get<double>();
    }
    if (confidence < 0.0) confidence = 0.0;
    if (confidence > 1.0) confidence = 1.0;

    out.tag        = tag;
    out.confidence = confidence;
    return true;
}

TranscriptClassifier::Result TranscriptClassifier::Classify(const std::string& text) {
    if (!IsAvailable() || text.empty()) {
        return {"unclassified", 0.0};
    }

    const std::string prompt = BuildPrompt(text);

    // 最大 2 回試行（初回 + リトライ 1 回）。
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string raw = llm_->Generate(prompt);
        Result r;
        if (ParseResponse(raw, r)) {
            return r;
        }
    }
    return {"unclassified", 0.0};
}

} // namespace LogGuide

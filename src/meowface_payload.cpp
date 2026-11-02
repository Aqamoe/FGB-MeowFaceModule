#include "meowface_payload.hpp"

#include "fgb/kit/json.hpp"

#include <cmath>
#include <string>

namespace meowface {

using fgb::kit::Arkit;
using fgb::kit::ArkitWeights;

namespace {

constexpr float kDegreeToRadian = 0.0174532924f;

float ReadAngle(const fgb::kit::JsonValue* vector, const char* key) {
    if (vector == nullptr) {
        return 0.0f;
    }
    const double value = vector->NumberOr(key, 0.0);
    if (std::isnan(value)) {  // 上游偶尔会发 NaN，不能让它污染整帧
        return 0.0f;
    }
    // 超过 ±90 度的眼动角度没有物理意义，截断以免 tan 爆炸
    const double clamped = value > 90.0 ? 90.0 : (value < -90.0 ? -90.0 : value);
    return static_cast<float>(clamped);
}

void ReadEye(const fgb::kit::JsonValue* node, EyeAngles& out) {
    if (node == nullptr) {
        return;
    }
    out.yaw_degrees = ReadAngle(node, "y");
    out.pitch_degrees = -ReadAngle(node, "x");
}

} // namespace

bool ParsePacket(std::string_view text, Packet& out, std::string& error) {
    const auto root = fgb::kit::ParseJson(text);
    if (!root.has_value()) {
        error = "JSON 解析失败";
        return false;
    }
    if (!root->IsObject()) {
        error = "顶层不是对象";
        return false;
    }

    out = Packet{};
    out.face_found = root->BoolOr("FaceFound", true);
    out.source_timestamp = static_cast<std::uint64_t>(root->NumberOr("Timestamp", 0.0));

    const fgb::kit::JsonValue* shapes = root->FindInsensitive("BlendShapes");
    if (shapes == nullptr || !shapes->IsArray()) {
        error = "缺少 BlendShapes 数组";
        return false;
    }

    for (std::size_t i = 0; i < shapes->Size(); ++i) {
        const fgb::kit::JsonValue* entry = shapes->At(i);
        if (entry == nullptr || !entry->IsObject()) {
            continue;
        }
        const std::string_view name = entry->TextOr("k");
        if (name.empty()) {
            continue;
        }
        // 缺 v 或类型不对的条目直接跳过：把「没上报」当成 0 会静默改掉一个形状。
        const fgb::kit::JsonValue* value = entry->FindInsensitive("v");
        if (value == nullptr || value->Kind() != fgb::kit::JsonKind::Number) {
            continue;
        }
        if (fgb::kit::SetArkitByName(out.weights, name, static_cast<float>(value->AsNumber()))) {
            ++out.shapes_parsed;
        } else {
            ++out.shapes_unknown;
        }
    }

    ReadEye(root->FindInsensitive("EyeLeft"), out.left);
    ReadEye(root->FindInsensitive("EyeRight"), out.right);

    if (out.shapes_parsed == 0 && out.shapes_unknown == 0) {
        error = "BlendShapes 为空";
        return false;
    }
    return true;
}

void DeclareTargets(fgb::kit::ShapeTable& table) {
    // MeowFace 相对通用 ARKit 映射多出来的几项，沿用 VRCFT 上游模块的近似：
    // 上抬唇同时喂 Raiser，微笑同时近似 CheekSquint 与 Dimple。
    table.Declare("MouthUpperDeepenLeft");
    table.Declare("MouthUpperDeepenRight");
    table.Declare("MouthRaiserLower");
    fgb::kit::DeclareUnifiedTargets(table);
}

namespace {

float Openness(float blink, float squint) {
    if (blink <= 0.0f) {
        return 1.0f;
    }
    const float penalty = blink + std::pow(blink, 0.33f) * std::pow(squint, 1.25f);
    return 1.0f - fgb::kit::Clamp01(penalty);
}

// 上游给的是角度，ABI 要的是 HMD 局部单位向量（-Z 前）。
// 用 tan 把偏航/俯仰转成分量，等效于把角度当作投影角处理。
void GazeFromAngles(float yaw_degrees, float pitch_degrees, float (&direction)[3]) {
    const float x = std::tan(yaw_degrees * kDegreeToRadian);
    const float y = std::tan(pitch_degrees * kDegreeToRadian);
    const float length = std::sqrt(x * x + y * y + 1.0f);
    direction[0] = x / length;
    direction[1] = y / length;
    direction[2] = -1.0f / length;
}

} // namespace

void WriteFrame(const Packet& packet, const fgb::kit::ShapeTable& table, FgbFrameSlot* slot) {
    const fgb::kit::ArkitWeights& weights = packet.weights;

    slot->attributes |= FGB_ATTR_EXPRESSION;
    fgb::kit::MapArkitToUnified(weights, table, slot);

    table.Write(slot, "MouthUpperDeepenLeft", fgb::kit::ArkitWeight(weights, Arkit::MouthUpperUpLeft));
    table.Write(slot, "MouthUpperDeepenRight", fgb::kit::ArkitWeight(weights, Arkit::MouthUpperUpRight));
    table.Write(slot, "MouthRaiserLower", fgb::kit::ArkitWeight(weights, Arkit::MouthShrugUpper));

    const float smile_left = fgb::kit::ArkitWeight(weights, Arkit::MouthSmileLeft);
    const float smile_right = fgb::kit::ArkitWeight(weights, Arkit::MouthSmileRight);
    table.Write(slot, "CheekSquintLeft", smile_left);
    table.Write(slot, "CheekSquintRight", smile_right);
    table.Write(slot, "MouthDimpleLeft", smile_left * 0.5f);
    table.Write(slot, "MouthDimpleRight", smile_right * 0.5f);
    table.Write(slot, "MouthStretchLeft", fgb::kit::ArkitWeight(weights, Arkit::MouthFrownLeft) * 0.5f);
    table.Write(slot, "MouthStretchRight", fgb::kit::ArkitWeight(weights, Arkit::MouthFrownRight) * 0.5f);

    slot->attributes |= FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS | FGB_ATTR_LEFT_GAZE |
                        FGB_ATTR_RIGHT_GAZE;

    slot->left_eye.openness = Openness(fgb::kit::ArkitWeight(weights, Arkit::EyeBlinkLeft), fgb::kit::ArkitWeight(weights, Arkit::EyeSquintLeft));
    slot->left_eye.openness_confidence = 1.0f;
    GazeFromAngles(packet.left.yaw_degrees, packet.left.pitch_degrees, slot->left_eye.gaze_dir);
    // 方向来自手机摄像头的角度估计，不是设备级眼动追踪，置信度压低。
    slot->left_eye.gaze_confidence = 0.5f;

    slot->right_eye.openness =
        Openness(fgb::kit::ArkitWeight(weights, Arkit::EyeBlinkRight), fgb::kit::ArkitWeight(weights, Arkit::EyeSquintRight));
    slot->right_eye.openness_confidence = 1.0f;
    GazeFromAngles(packet.right.yaw_degrees, packet.right.pitch_degrees, slot->right_eye.gaze_dir);
    slot->right_eye.gaze_confidence = 0.5f;
}

} // namespace meowface

#pragma once
// MeowFace 的 UDP JSON 负载解析与帧写入。
//
// 上游格式（MeowFace App → PC:12345 的一包 JSON）：
//   {"Timestamp":..,"Hotkey":0,"FaceFound":true,
//    "Rotation":{"x":..,"y":..,"z":..},"Position":{..},
//    "EyeLeft":{"x":..,"y":..,"z":..},"EyeRight":{..},
//    "BlendShapes":[{"k":"jawOpen","v":0.52}, ...]}
// 系数名是 49 个 ARKit 风格名（含 eyeWide/eyeSquint，以及 head* 等非表情项）。

#include "fgb/kit/arkit.hpp"
#include "fgb/kit/host.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace meowface {

struct EyeAngles {
    // 上游按「角度（度）」上报双眼方向，且极化是反的：
    // VRCFT 上游模块用 Gaze.x = Eye.y、Gaze.y = -Eye.x，这里保持同一口径。
    float yaw_degrees = 0.0f;
    float pitch_degrees = 0.0f;
};

struct Packet {
    fgb::kit::ArkitWeights weights{};
    EyeAngles left{};
    EyeAngles right{};
    bool face_found = true;
    std::size_t shapes_parsed = 0;
    std::size_t shapes_unknown = 0;
    std::uint64_t source_timestamp = 0;
};

// 解析一包 JSON。返回 false 表示这一包不可信（结构非法/超长），调用方应整包丢弃。
bool ParsePacket(std::string_view text, Packet& out, std::string& error);

// 登记本模块会写的全部 Unified Expressions 目标名。
void DeclareTargets(fgb::kit::ShapeTable& table);

// 写表情 + 双眼属性，并置上对应的 attributes 位。
void WriteFrame(const Packet& packet, const fgb::kit::ShapeTable& table, FgbFrameSlot* slot);

} // namespace meowface

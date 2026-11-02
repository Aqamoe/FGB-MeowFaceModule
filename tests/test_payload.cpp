#include "meowface_payload.hpp"

#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"

#include <cmath>
#include <string>

using namespace fgb::kit;
using meowface::Packet;
using meowface::ParsePacket;

namespace {

constexpr std::size_t Slot(Arkit name) { return static_cast<std::size_t>(name); }

const char* kBasicPayload =
    R"({"Timestamp":1700000000,"Hotkey":0,"FaceFound":true,
        "Rotation":{"x":-10.5,"y":3.25,"z":1.0},
        "Position":{"x":0.01,"y":0.02,"z":0.03},
        "EyeLeft":{"x":5.0,"y":-7.5,"z":0.0},
        "EyeRight":{"x":-4.0,"y":6.5,"z":0.0},
        "BlendShapes":[{"k":"jawOpen","v":0.62},{"k":"mouthSmileLeft","v":0.4},
                       {"k":"eyeBlinkLeft","v":0.25},{"k":"eyeSquintLeft","v":0.2},
                       {"k":"browInnerUp","v":0.8},{"k":"headUp","v":0.3}]})";

FgbFrameSlot FreshSlot() {
    FgbFrameSlot slot{};
    slot.struct_size = sizeof(FgbFrameSlot);
    slot.abi_version = FGB_MODULE_ABI_V2;
    slot.shape_count = FGB_MAX_SHAPES;
    return slot;
}

} // namespace

int main() {
    // ---- 正常包 ----
    Packet packet{};
    std::string error;
    CHECK(ParsePacket(kBasicPayload, packet, error));
    CHECK(packet.face_found);
    CHECK_EQ(packet.shapes_parsed, static_cast<std::size_t>(5));
    // headUp 不是表情系数，必须落到「未知」而不是被塞进 ARKit 槽
    CHECK_EQ(packet.shapes_unknown, static_cast<std::size_t>(1));
    CHECK_NEAR(packet.weights[Slot(Arkit::JawOpen)], 0.62, 1e-6);
    CHECK_NEAR(packet.weights[Slot(Arkit::BrowInnerUp)], 0.8, 1e-6);
    CHECK_NEAR(packet.weights[Slot(Arkit::EyeBlinkLeft)], 0.25, 1e-6);
    CHECK_NEAR(packet.left.yaw_degrees, -7.5, 1e-6);   // 上游极化：yaw 取 y
    CHECK_NEAR(packet.left.pitch_degrees, -5.0, 1e-6); // pitch 取 -x
    CHECK_NEAR(packet.right.yaw_degrees, 6.5, 1e-6);

    // ---- 结构非法的输入必须整包丢弃 ----
    Packet broken{};
    CHECK(!ParsePacket("not json", broken, error));
    CHECK(!ParsePacket(R"({"BlendShapes":{}})", broken, error));
    CHECK(!ParsePacket(R"({"FaceFound":true})", broken, error));
    CHECK(!ParsePacket(R"({"BlendShapes":[]})", broken, error));
    CHECK(!ParsePacket(std::string("{\"BlendShapes\":[{\"k\":\"jawOpen\",\"v\":") +
                           std::string(20000, '9') + "}]",
                       broken, error));

    // 元素缺字段 / 类型错，不应影响同一包里的合法项
    Packet partial{};
    CHECK(ParsePacket(R"({"BlendShapes":[{"k":"jawOpen","v":0.5},{"v":0.9},
                                          {"k":"mouthPucker"},{},null]})", partial, error));
    CHECK_NEAR(partial.weights[Slot(Arkit::JawOpen)], 0.5, 1e-6);
    CHECK_EQ(partial.shapes_parsed, static_cast<std::size_t>(1));

    // ---- 帧写入：通用映射 + MeowFace 的近似项 + 双眼属性 ----
    testing::HostMock host;
    Host logger{host.services()};
    ShapeTable table;
    meowface::DeclareTargets(table);
    CHECK(table.Build(logger));
    CHECK_EQ(table.ResolvedCount(), table.Size());  // 目标名必须都是真实形状

    FgbFrameSlot slot = FreshSlot();
    meowface::WriteFrame(packet, table, &slot);

    CHECK((slot.attributes & FGB_ATTR_EXPRESSION) != 0);
    CHECK((slot.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);
    CHECK((slot.attributes & FGB_ATTR_RIGHT_GAZE) != 0);

    CHECK_NEAR(slot.shapes[host.IndexOf("JawOpen")], 0.62, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("BrowInnerUpLeft")], 0.8, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("BrowInnerUpRight")], 0.8, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("MouthCornerPullLeft")], 0.4, 1e-6);
    // MeowFace 特有：微笑同时近似 CheekSquint，Dimple 取一半
    CHECK_NEAR(slot.shapes[host.IndexOf("CheekSquintLeft")], 0.4, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("MouthDimpleLeft")], 0.2, 1e-6);
    CHECK(slot.shape_valid[host.IndexOf("JawLeft")] == 0);  // 本包没有 jawLeft

    // 睁眼度沿用上游幂次公式：1 - (0.25 + 0.25^0.33 * 0.2^1.25)
    const float expected = 1.0f - (0.25f + std::pow(0.25f, 0.33f) * std::pow(0.2f, 1.25f));
    CHECK_NEAR(slot.left_eye.openness, expected, 1e-5);
    CHECK_NEAR(slot.right_eye.openness, 1.0, 1e-6);
    CHECK_NEAR(slot.left_eye.gaze_confidence, 0.5, 1e-6);

    // 方向必须是单位向量，且 -Z 前
    const float length = std::sqrt(slot.left_eye.gaze_dir[0] * slot.left_eye.gaze_dir[0] +
                                   slot.left_eye.gaze_dir[1] * slot.left_eye.gaze_dir[1] +
                                   slot.left_eye.gaze_dir[2] * slot.left_eye.gaze_dir[2]);
    CHECK_NEAR(length, 1.0, 1e-5);
    CHECK(slot.left_eye.gaze_dir[2] < 0.0f);
    CHECK(slot.left_eye.gaze_dir[0] < 0.0f);  // yaw 为负 → 向左
    CHECK(slot.left_eye.gaze_dir[1] < 0.0f);  // pitch 为负 → 向下

    // 只给表情、没有眨眼数据时，睁眼度按「全开」而不是 0
    Packet only_expression{};
    only_expression.weights[Slot(Arkit::JawOpen)] = 0.1f;
    FgbFrameSlot sparse = FreshSlot();
    meowface::WriteFrame(only_expression, table, &sparse);
    CHECK((sparse.attributes & FGB_ATTR_EXPRESSION) != 0);
    CHECK_NEAR(sparse.left_eye.openness, 1.0, 1e-6);

    return CheckSummary("meowface-payload");
}

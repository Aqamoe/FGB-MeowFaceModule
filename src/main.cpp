// MeowFace → FaceGaze Bridge 原生模块。
//
// 监听 MeowFace App 发到本机 12345 端口的 UDP JSON，把 49 个 ARKit 风格系数
// 映射成 Unified Expressions，并给出睁眼度与逐眼方向。
// 不需要 VRCFT，不注入任何进程，也不需要管理员权限。

#include "meowface_payload.hpp"

#include "fgb/kit/config.hpp"
#include "fgb/kit/module.hpp"

#include <cstdint>
#include <string>

namespace {

constexpr std::uint16_t kDefaultPort = 12345;
constexpr std::uint64_t kNoDataTimeoutNs = 10ull * 1000ull * 1000ull * 1000ull;
constexpr std::uint64_t kWarnIntervalNs = 5ull * 1000ull * 1000ull * 1000ull;

class MeowFaceSource : public fgb::kit::IModuleSource {
public:
    bool Start(fgb::kit::Host& host, const FgbInitParams& params, std::string& error) override {
        fgb::kit::ConfigIni config;
        std::string config_error;
        const std::string directory = params.module_dir != nullptr ? params.module_dir : "";
        if (config.Load(directory, "config.ini", config_error)) {
            port_ = static_cast<std::uint16_t>(config.GetInt("port", port_));
            if (!config_error.empty()) {
                host.Warn("config.ini：" + config_error);
            }
            if (!config.UnknownKeys().empty()) {
                host.Warn("config.ini 有未识别的键：" + config.UnknownKeys()[0]);
            }
        }

        fgb::kit::ShapeTable table;
        meowface::DeclareTargets(table);
        if (!table.Build(host)) {
            error = "宿主没有提供 shape_index，无法映射 Unified Expressions";
            return false;
        }
        if (table.ResolvedCount() < table.Size()) {
            host.Warn("部分 Unified Expressions 形状未被宿主识别，已跳过 " +
                      std::to_string(table.Size() - table.ResolvedCount()) + " 个");
        }
        table_ = std::move(table);

        if (!endpoint_.Bind(port_, error)) {
            return false;
        }
        host.Info("MeowFace 监听 UDP " + std::to_string(port_) + "，等待手机连接");
        host.StatusStarting("等待 MeowFace 数据");
        return true;
    }

    bool Read(fgb::kit::Host& host, FgbFrameSlot& slot, std::uint64_t now_ns) override {
        meowface::Packet packet;
        bool parsed = false;

        fgb::kit::UdpPacket datagram;
        // 手机按帧率推送，只处理最新一包；积压的旧包丢掉比逐帧回放更准。
        if (endpoint_.PollLatest(datagram)) {
            const std::string_view text{reinterpret_cast<const char*>(datagram.data.data()),
                                        datagram.data.size()};
            std::string error;
            if (meowface::ParsePacket(text, packet, error)) {
                parsed = true;
                if (packet.shapes_unknown > 0 && packet.shapes_unknown != reported_unknown_) {
                    reported_unknown_ = packet.shapes_unknown;
                    host.Warn("MeowFace 上报了 " + std::to_string(packet.shapes_unknown) +
                              " 个无法识别的系数名（模块会忽略它们）");
                }
                if (!packet.face_found) {
                    host.WarnThrottled(warn_ns_, now_ns, kWarnIntervalNs,
                                       "MeowFace 报告没检测到脸，输出可能抖动");
                }
            } else {
                host.WarnThrottled(warn_ns_, now_ns, kWarnIntervalNs, "丢弃一包：" + error);
            }
        }

        if (!staleness_.Check(host, endpoint_.LastPacketNs(), now_ns) || !parsed) {
            return false;
        }

        meowface::WriteFrame(packet, table_, &slot);
        return true;
    }

    void Stop(fgb::kit::Host& host) override {
        endpoint_.Close();
        host.StatusStopped("MeowFace 已停止");
    }

private:
    fgb::kit::UdpEndpoint endpoint_;
    fgb::kit::ShapeTable table_;
    fgb::kit::Staleness staleness_{kNoDataTimeoutNs, "MeowFace"};
    std::uint16_t port_ = kDefaultPort;
    std::uint64_t warn_ns_ = 0;
    std::size_t reported_unknown_ = 0;
};

} // namespace

FGB_KIT_EXPORT(MeowFaceSource, "com.fgbbridge.meowface", "MeowFace",
               FGB_CAP_EXPRESSION | FGB_CAP_EYES)

# FGB MeowFaceModule

[![build](https://github.com/Aqamoe/FGB-MeowFaceModule/actions/workflows/ci.yml/badge.svg)](https://github.com/Aqamoe/FGB-MeowFaceModule/actions/workflows/ci.yml)

**状态：0.1.0，未验证（untested）**。协议解析、映射与 ABI 生命周期有单元测试和合成 UDP
端到端冒烟；真机（手机端 MeowFace）验收待补，见 [docs/verification.md](docs/verification.md)。

[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge) 的 MeowFace 原生模块。
直接接收 **MeowFace** App 发到本机 12345 端口的 UDP JSON，输出 Unified Expressions 表情、
睁眼度与逐眼方向。不需要 VRCFT，不注入任何进程，也不需要管理员权限。

MeowFace native input module for FaceGaze Bridge. Reads the UDP JSON stream your
phone already sends and outputs Unified Expressions shapes, eye openness and
per-eye gaze. No VRCFT, no process injection, no admin rights.

## 功能 / Features

- **表情**：49 个 ARKit 风格系数 → Unified Expressions 形状（通用映射 + MeowFace 的近似项）。
- **眼睛**：睁眼度沿用上游公式 `1 - clamp(blink + blink^0.33 * squint^1.25)`；
  方向由 `EyeLeft`/`EyeRight` 的角度（度）转成 HMD 局部单位向量，置信度 0.5。
- **断连可恢复**：10 秒没有数据时在模块卡片提示原因，数据恢复后自动回到活跃。
- **可诊断**：端口冲突、畸形 JSON、未知系数名都有明确日志，不静默丢数据。

## 不提供 / Not provided

| 数据 | 原因 |
|---|---|
| 头姿 | 报文里有 `Rotation`/`Position`，但坐标系与单位未经验证，先不接（避免写错方向的头姿） |
| 瞳孔 | 报文没有该字段 |
| 2D 注视点 | 报文没有；方向是摄像头估计的角度，不用于 foveation |
| 渲染级注视 | 需要 `gaze` 通道时请配真正的眼动源；本模块只填 `eyes` |

## 环境要求

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 / 11 x64 |
| 宿主 | FaceGaze Bridge（原生模块 ABI v2） |
| 手机 | 安装 MeowFace，并填这台 PC 的局域网 IP |
| 其它 | 关闭 VRCFaceTracking：它和本模块抢同一个 UDP 端口 |

## 安装

1. 从 Releases 下载 `meowface-0.1.0.zip`。
2. FaceGaze Bridge →「模块库 → 原生模块 → 安装原生模块 (.zip)」，或「模块」页「从 .zip 安装」。
3. 「模块」页把 **MeowFace** 设为启动，角色通常给 `expression` 与 `eyes`。
4. 重启引擎；手机开始发送后模块卡片应显示活跃。

## 配置

模块目录下可选 `config.ini`：

```ini
port = 12345
```

改完重启引擎。未知键会被记录一条警告，不会静默失效。

## 从源码构建

需要 Visual Studio 2022（x64）、CMake ≥ 3.24 与 Node 20（脚本用）。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

SDK 与 Kit 由 `sdk.lock.json` / `kit.lock.json` 在配置阶段拉取并校验 SHA256，仓库里不含 SDK 头文件副本。
本地想指向已解压的 Kit：

```powershell
cmake -S . -B build -A x64 -DFGB_KIT_DIR="D:/code/FGB-Module/FGB-Module-Kit"
```

打包与测试：

```powershell
cmake --build build --config Release --target package-modules   # build/Release/meowface-<版本>.zip
ctest --test-dir build -C Release --output-on-failure           # 协议与映射单测
cmake --build build --config Release --target smoke-test        # 真实 ABI + 合成 UDP 冒烟（需 12345 空闲）
```

## 目录结构

```
src/main.cpp                ABI 生命周期、UDP 接收、状态上报
src/meowface_payload.*      JSON 解析与 Unified Expressions 写入
module/                     fgb-native.json 与图标
tests/                      协议单测、真实 ABI 冒烟
docs/protocol.md            线格式与映射依据
docs/verification.md        真机验收清单
cmake/                      依赖锁解析（SDK / Kit 都是拉取，不复制）
```

## 排错

| 现象 | 处理 |
|---|---|
| 卡片显示「无法绑定 UDP 12345：端口已被占用」 | 关闭 VRCFT 或其它接收端；或用 `config.ini` 换端口并同步改手机设置 |
| 一直提示「还没收到任何数据包」 | 确认手机与 PC 同网段、IP 填的是 PC 地址、防火墙放行该端口 |
| 表情不动、眼睛正常 | 角色没分给 `expression` |
| 日志出现「无法识别的系数名」 | 上游 App 改了字段名；把版本号和日志一并提到 issue |
| 头部转动带动表情 | 属正常：本模块不接头姿，画面外的头动不会反映到形状 |

## 致谢 / Attribution

- 线格式与映射依据参考 [VRCFaceTracking-MeowFace](https://github.com/regzo2/VRCFaceTracking-MeowFace)；
  本仓库是该线格式的独立 C++ 实现，未包含其源码。
- ABI 头与宿主：[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge)；共享工具链：[FGB-Module-Kit](https://github.com/Aqamoe/FGB-Module-Kit)。

## 许可

Apache License 2.0，见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。

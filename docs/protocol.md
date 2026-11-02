# MeowFace UDP 协议要点

来源：MeowFace App 向 PC 发送的 JSON 数据报，默认端口 **12345**（上游 VRCFT 模块同一端口）。
本模块按线格式独立实现，未复制上游源码。

## 报文结构

单个 UDP 数据报，UTF-8 JSON：

```json
{
  "Timestamp": 1700000000,
  "Hotkey": 0,
  "FaceFound": true,
  "Rotation": { "x": -10.5, "y": 3.25, "z": 1.0 },
  "Position": { "x": 0.01, "y": 0.02, "z": 0.03 },
  "EyeLeft":  { "x": 5.0, "y": -7.5, "z": 0.0 },
  "EyeRight": { "x": -4.0, "y": 6.5, "z": 0.0 },
  "BlendShapes": [ { "k": "jawOpen", "v": 0.62 }, { "k": "mouthSmileLeft", "v": 0.4 } ]
}
```

| 字段 | 类型 | 本模块用法 |
|---|---|---|
| `Timestamp` | 整数 | 记入日志，时间戳仍以接收时刻为准 |
| `Hotkey` | 整数 | 忽略 |
| `FaceFound` | 布尔 | false 时告警（数据仍会输出，与上游一致） |
| `Rotation` / `Position` | 向量 | **不使用**：坐标系与单位未验证，见「不提供」 |
| `EyeLeft` / `EyeRight` | 向量（度） | 逐眼注视方向 |
| `BlendShapes` | `{k,v}` 数组 | 表情系数与眨眼/眼周紧张 |

## 系数名

上游共 49 个键，命名接近 ARKit 但不完全相同：

- 含 ARKit 没有的 `eyeWideLeft/Right`、`eyeSquintLeft/Right`、`browInnerUpLeft/Right`；
- 缺 `mouthClose`、`mouthDimple*`、`mouthPress*`、`mouthStretch*`、`cheekSquint*`；
- 含非表情项 `headLeft/headRight/headUp/headDown/headRollLeft/headRollRight`。

`browInnerUpLeft/Right` 都归一到 ARKit 的单个 `browInnerUp`；`head*` 落到「未知系数」，
模块记一条日志后忽略，不会静默当成 0。

## 极化与单位

- 眼部角度按**度**上报；VRCFT 上游用 `Gaze.x = Eye.y`、`Gaze.y = -Eye.x`，本模块保持同一口径，
  再转成 ABI 要求的 HMD 局部单位向量（-Z 前）。
- 超过 ±90 度的眼动角度会被截断，避免 `tan` 爆炸。

## 睁眼度

沿用上游公式（眨眼与眼周紧张共同影响）：

```text
openness = 1 - clamp(blink + blink^0.33 * squint^1.25)
```

## 表情映射

通用部分由 FGB-Module-Kit 的 ARKit → Unified Expressions 表完成；MeowFace 特有近似：

| 目标形状 | 来源 |
|---|---|
| `MouthUpperDeepenLeft/Right` | `mouthUpperUpLeft/Right` |
| `MouthRaiserLower` | `mouthShrugUpper` |
| `CheekSquintLeft/Right` | `mouthSmileLeft/Right`（近似） |
| `MouthDimpleLeft/Right` | `mouthSmile* * 0.5`（近似） |
| `MouthStretchLeft/Right` | `mouthFrown* * 0.5`（近似） |

## 冲突

同一端口只能有一个接收者。VRCFT 的 MeowFace 模块、VSeeFace 等都可能占 12345；
绑定失败时模块卡片会直接给出端口与原因。需要改端口就在模块目录写 `config.ini`：

```ini
port = 12346
```

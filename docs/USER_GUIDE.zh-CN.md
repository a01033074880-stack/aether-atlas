# AETHER Micro 1.0.0 使用说明

## 安装与开始

1. 解压发布 ZIP。`VST3` 目录有 **AETHER Micro.vst3** 和 **AETHER Micro Synth.vst3** 两个完整目录，不能只复制里面的单个文件。
2. 在解压目录执行 `powershell -ExecutionPolicy Bypass -File .\install.ps1`。默认安装到当前用户的 `%LOCALAPPDATA%\Programs\Common\VST3`，不需要管理员权限。如宿主未自动扫描该位置，请手动添加搜索路径。
3. 在 DAW 中重新扫描 VST3。音频轨加载 **AETHER Micro**，MIDI 乐器轨加载 **AETHER Micro Synth**。升级时先关闭宿主；安装脚本会备份旧版本目录。
4. 独立程序位于 `Standalone`。打开设备设置选择声卡和 MIDI 输入。FX 独立版默认静音输入，先确认输入/输出不会产生声反馈，再解除静音。插件版由宿主负责音频路由。

## 三种模式

**FX** 处理输入音频。默认 Gentle Orbit 是轻度共振控制和短空间。想听染色可选择 Warm Console 或 Ember Guitar。

**Resynth** 用输入的单音音高和响度驱动谐波／噪声声源及共鸣。建议使用清晰的单音哼唱、单线条乐器；复调、气声、低电平和强噪声会降低跟踪稳定度。这是确定性重合成，不是训练后的 DDSP 网络。

**Synth** 接收 MIDI 发声，8 复音。支持力度、音符开关、通道弯音（±2 半音）、CC64 延音及 All Notes Off。Prism Keys 和 Copper Strings 可以直接演奏。Panic 按钮清空音符和空间尾音。

## 五个模块

| 模块 | 操作与听感 |
|---|---|
| Spectral | Tame 增加动态共振削减；Threshold 越低处理越多；Focus 把削减重点移向指定频段；Preserve 保护起音。采用 24 个感知分布的滤波带，适合温和雕刻。 |
| Modal | Resonance 混入共鸣，Decay 控制衰减，Tune 以半音移调，Brightness 改变泛音阻尼。String／Plate／Beam／Membrane 使用不同频率比例；是原创模态音色，不是校准过的真实乐器复刻。 |
| Colour | Drive 增加非线性，Silk／Ember／Iron 为三种不同曲线，Tone 调整亮度，Bias 改变非对称性。音量可能增加，请用 Output 匹配试听。 |
| Space | Space 混入湿声，Size 改变空间尺度，Time 改变尾音，Damping 吸收高频，Disperse 扩散相位。Freeze 保留已有空间并停止注入新激励；先让空间有声音再冻结。 |
| Master | Width=100% 为原立体声，0% 折叠单声道。Ceiling 是采样峰值保护；Release 控制增益恢复。Dither 可选 16/24-bit TPDF，内部浮点混音通常关闭，在最终降位深之前使用。 |

Master 始终位于最后，其余四个模块由 Signal order 选择 24 种串联顺序。每个模块都有独立开关。全局 Bypass 直接旁通；Mix=0 时仍经过输出音量和已启用的 Master，若需要完全原样比较请使用 Bypass。

## 调制、侧链与界面

LFO 支持正弦、三角和随机保持。Destination 选择 Drive／Resonance／Space／Focus，Depth 为零时不调制。Resonance 目的地调制模态音高；Beat sync 开启后，Rate 表示每拍循环数，实际速率随宿主 BPM 变化。没有播放头信息时按 120 BPM。

FX 的可选外部侧链接收单声道或立体声，并作为音高／响度分析与物理激励源。侧链不直接混入输出；没有连接时自动使用主输入。宿主必须启用并发送到 Sidechain 总线。

频谱来自实际输出音频。输入、输出和增益衰减表用于观察信号；未测量的 CPU 数据不伪装显示。Minimal 模式显示紧凑参数并停用频谱分析，便于专注调音。旋钮可双击复位，数值可直接编辑，鼠标悬停查看提示。

## 预设、A/B 与自动化

工厂预设覆盖通道、吉他、打击、模态、氛围及 MIDI 音色。Save/Load 使用开放 JSON `.aether` 文件。A/B 可暂存两个参数状态，切换时清空 DSP 尾音，避免旧空间干扰判断。宿主工程保存参数、预设名和 A/B 状态。文件损坏或不支持的 schema 会拒绝加载；超出范围的数值会限制到有效区间。

连续参数有短平滑；模块开关、材质、处理顺序及空间尺度等结构切换不能保证无缝。演出或录音中改变这些结构时建议先淡出，冻结空间也应避免突然切换。空间参数最大为 20 秒；冻结尾音会持续很长时间。零宿主补偿延迟不代表滤波器、饱和器和混响没有相位／时间响应。

## 版本边界

本版本交付 Windows x64 原生插件与独立程序。未包含 CLAP、ARM64、macOS/Linux 二进制、MIDI 2.0 Note Expression、跨进程轨道网络、神经模型导入、WGPU 或任意并联 DAG。产品规划中 3.8 MB、1.4% CPU、86 dB 抗混叠信噪比等为研究目标，本版本以 `VALIDATION.md` 的实际构建和测试结果为准。母带保护为采样峰值保护，不保证流媒体响度、LUFS 或重建真峰值标准。

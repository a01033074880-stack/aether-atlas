# AETHER Atlas 第二阶段开发报告

日期：2026-10-23，Asia/Shanghai。版本：2.0.0。平台：Windows x64。基于v1.0.0（0db4ca7）扩展，第一阶段发布包与标签保留。

## 1. 阶段结果

已把原五模块串联改造成12节点可编排系统，并加入六算子FM、完整调制工作区和超现实原生界面。这里的扩展有实际声音算法、连接行为、状态和验证，不是只增加模块名称或绘制无效线缆。

| 项目 | 第一阶段 | 第二阶段 |
|---|---|---|
| 效果结构 | 五个固定领域，前四重排 | 20类效果+Thru，12个可重复槽 |
| 路由 | 24种串联顺序 | 32种图算法+有符号12×12连接，串并联/树/汇合/循环 |
| 声源 | 简单8声部谐波 | 独立六算子/16声部FM，32算法、Morph、自定义6×6 |
| 重合成 | 基频/响度控制简单声源 | 可用同样的音频信息驱动连续FM声部，并叠加MIDI |
| 调制 | 单LFO/少量目的地 | 四LFO+包络/力度/音高/随机/Chaos，16行至84目的地 |
| 空间 | 八线反馈空间 | 新模块为16线Householder FDN，另有Diffuser、颗粒和频谱冻结 |
| 工厂预设 | 12 | 36：保留12个legacy+24个新音色 |
| 参数/状态 | 39参数/schema1 | 467参数/schema2，保持前39ID，兼容schema1 |
| UI | 暗色五卡片与频谱 | 象牙纸面、悬浮拱门/几何体，Patch/FM/Motion、搜索库、画布、检查器、全部参数 |

## 2. 真正集成的开源算法

| 来源 | 固定提交 | 实际调用 |
|---|---|---|
| Signalsmith Stretch1.3.2 | 88061fd4eda56a2353c5d65c74aa3f4ccf76f9da | Prism Pitch、Formant与STFT Freeze |
| Signalsmith Linear | 7bb1c8f36794d6980080ce817c91f8da8fe56a66 | Stretch依赖的FFT/STFT |
| DaisySP | 2c72eaf9eac5fc0dca1919d65d606da907832618 | GranularPlayer、Wavefolder、Resonator、String、Ladder、Decimator、Chorus |
| chowdsp_wdf | 43bcd295e8403de913f9966f4fd7ff9d17c9f1a5 | 实际波数字电阻源/电容/反并联二极管电路 |

前三项MIT，WDF为BSD-3-Clause；保留原文件作者、许可证与依赖哈希。原生标题字体Instrument Serif、技术字体DM Mono来自Google Fonts，按SIL OFL嵌入。完整应用仍AGPL-3.0-or-later；原创DSP/FM/图包装为MIT。

20类效果是Spectral、Modal、Colour、Space、Prism Pitch、Grain Cloud、Wavefolder、WDF Diode、Resonator、String、Ladder、Frequency Shift、Ring Mod、Comb、Diffuser、LoFi、Tape、Spectral Freeze、Chaos、Chorus。每槽四宏有真实行为检查；Thru是明确的透明工具。

新增的Space为16线网络，Spectral是24带动态共振控制，Prism才是频谱移调；没有用两个不同名称冒充同一效果。Modal/Resonator分别使用12/24模态配置并有不同实际响应。

## 3. 图、FM和调制架构

效果图按节点号顺序执行。前向连接读取当前采样；自身/反向读取前一采样并受全局反馈及饱和边界控制。输入/输出汇合归一化，避免稠密连线按线数失控。每槽有混合、电平、Pan、输入/输出send及四宏。

FM矩阵为[目的地][来源]，六算子用4倍内部处理、超声算子衰减及输出滤波。32种拓扑包含堆叠、分支、多载波、交叉和递归；自定义矩阵与Morph可保存。第一次从内置Morph手改矩阵时保存实际插值与载波，并归零Morph。

Gain/Index/Feedback有5ms样本时钟平滑；ADSR状态与步长采用double，避免合法高采样率长包络因float步长过小而永久停住。其他算子/拓扑变化仍可能即时改变，不虚称全部无点击。

调制有10个来源选择（含None），16条有符号分配，84个槽目的地。固定16采样控制节奏，不因宿主块切分重启。音频FM使用样本时钟跟踪和激励更新；已验证块长1与257的同输入输出一致。

## 4. UI与易用性

视觉理念为Suspended Cartography：纸面、墨色、陶土红/钴蓝、悬浮体、拱门和长影。原生矢量处理没有照搬商业插件或艺术家画作。独立设计板和理念随源码保存。

- Patch：分类可搜索模块库、32张算法图、12节点端口画布、真实电平和选中模块检查器。
- FM：算法图、载波输出、实际Morph矩阵、全局包络和六组算子控制。
- Motion：四LFO形状和实际值、16行来源/目标/深度。
- All params：滚动访问全部参数，保留legacy能力；键盘/文本/拖动结构编辑自动准备Custom状态。
- 帮助、预设文件、A/B、Panic、模式和全局电平均有入口。
- 默认1440×900，最小1180×780，已渲染检查默认/最小/大尺寸及各工作区。

本会话没有可调用的专用审美MCP/图像生成工具，已明确披露；使用frontend-design、canvas-design技能及实际下载的OFL字体完成目标，没有编造外部工具调用。

## 5. 验证结果

| 范围 | 已执行结果 |
|---|---|
| 原始DSP | 46项PASS |
| 六算子FM | 215项PASS；GCC/MSVC严格告警验证 |
| 模块/图 | 所有20效果×四宏、32模板、84目标、12重复实例、多采样率/块长/重置、冻结/无分配均通过 |
| 宿主/GUI | 21场景PASS，含真实组件操作和宿主手势监听 |
| CTest | 四套件全部通过；最终日志约32.55s |
| 实际VST3 | pluginval1.0.4 strictness5，FX/Synth均SUCCESS，含GUI/自动化/总线 |
| 声音输出 | 8份8秒、48kHz、立体声PCM16，非静音、无非有限值、峰值未过满幅 |
| 二进制依赖 | dumpbin仅Windows系统DLL，不要求安装独立DSP/JUCE运行库 |

普通堆分配拦截和代码审查覆盖prepare之后的DSP更新、处理、MIDI、reset；不等于任意第三方宿主/调度条件的形式化硬实时证明。pluginval的可选独立Steinberg validator没有提供，因此没有声称它通过。

## 6. 关键缺陷及关闭

1. FM长释放/高Sustain衰减在高采样率停滞：旧实现实际复现，double包络修复，48秒释放/20秒起音等回归通过。
2. 颗粒与相位查表边界：修正相位>=周期及索引/包络边界，保留上游许可。
3. 频谱Freeze reset随机相位不重复：显式reseed，无需重建FFT状态。
4. Chorus空构造未初始化方向：上游Init先初始化字段；同时先设置Delay再Depth，避免无关Pan编辑改变调制轨迹。
5. All params连接修改不生效：进入Custom前复制当前有效图/矩阵。
6. JUCE键盘未自动发出拖动手势：InstrumentSlider对箭头添加ScopedDrag；准备操作移到attachment开始前，监听实际begin/end和值事件验证不嵌套。
7. FM最大48秒释放被30秒尾音声明截断：尾音覆盖最长释放并留效果余量；冻结/潜在冻结调制/循环保守报告无限。
8. 首帧空标题、浅色数值框、微小字体、不可用CPU伪数字、未反映Morph的图示：经实际截图逐项修正。

独立FM、图、宿主/UI审查及限定复核都已完成，要求修复的问题已关闭。测试覆盖范围以日志为准，没有额外宣称商业DAW人工听音、外部声卡或其他平台通过。

## 7. 性能与边界

开发机i9-13900H，48kHz/立体声/64块长。一组12节点混合图，2秒音频耗时：算法0为0.210146s，3为0.322684s，14为0.251286s，25为0.211494s，约10.5%–16.1%单核实时预算。FM16声部一组2.73067s音频约用1.43394s，约52.5%。短测受调度/电源/并发负载影响，不是DAW总CPU或p99期限保证。

实际模块大小：FX5,347,840字节，Synth5,346,816字节；ZIP还包括独立程序、完整源码/依赖和试听。

Prism默认48kHz的声明/实测冲激延迟均2560采样；Hz频移有63采样Hilbert延迟。创作图PDC=0、支路不自动对齐，Texture只表示最大启用节点的固有延迟。多频谱节点串联可累加，Mix/并联会产生时间/相位差。

其他限制：极端PM/非线性可混叠；结构/捕获/尺寸变化可点击；Resynth仅单音；16声部+12昂贵效果负载未保证低于实时预算。没有训练DDSP/NAM/S4D、WGPU、CLAP/MIDI2、跨进程64轨网络、Atmos/64声道或非Windows二进制。

## 8. 交付与复现

交付名为 `AetherAtlas-2.0.0-Windows-x64.zip`，包含FX/Synth VST3、两个Standalone、36预设、项目/固定依赖源码、许可证、8试听、截图、模块JSON目录、中文指南、验证日志和SHA-256。第一阶段发布包保留。

```powershell
./scripts/build.ps1 -BuildDirectory build -Jobs 4
# 或在开发者环境使用Ninja
./scripts/build.ps1 -BuildDirectory build-ninja -Generator Ninja -JucePath C:/SDKs/JUCE
ctest --test-dir build-ninja -C Release --output-on-failure
./build-ninja/AetherAtlasRender.exe out/atlas-audio
```

具体字段和用法：[中文指南](docs/USER_GUIDE.zh-CN.md)、[需求验收索引](docs/ATLAS_ACCEPTANCE.md)、[实测记录](docs/VALIDATION.md)、[开源来源](docs/ATLAS_DEPENDENCIES.md)、[视觉理念](docs/ATLAS_VISUAL_PHILOSOPHY.md)。发布包旁的SHA-256及包内清单用于完整性核对，源码ZIP关联最终版本标签。

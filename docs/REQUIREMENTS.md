# Atlas 2.2.0 功能与来源对照

本阶段目标来自用户对第一阶段报告的扩展要求：更多且更另类的真实模块、融合开源算法、类似 FM 的可选择路由与调制、完整超现实艺术 UI 重构。维护中的开源代码不能客观等同于“全球最先进／音质最好”。

| 用户目标 | 实际实现与核查入口 |
|---|---|
| 模块数量大幅增加 | 20 类效果 + Thru；12 槽均可重复加载；ModularTests 检查全部类型、四宏及 12 份重复实例 |
| 效果不平凡 | 频谱移调/Formant、颗粒捕获冻结、WDF 二极管、物理共鸣/弦、频移/环调、波折叠、16 线空间、频谱冻结、Chaos |
| 真正融合开源库 | 实际编译 Signalsmith Stretch/Linear、DaisySP、chowdsp_wdf，保留提交、源码、许可和披露的安全补丁 |
| 不再是简单串联 | 32 种音频图算法、有符号 12×12 自定义连接、串并联/汇合/分支/一采样回送；全部模板实际路由音频 |
| 像 FM 一样选择算法 | 独立的 32 张效果图缩略图库，以及真实六算子/32 算法 FM/PM 声源 |
| 更丰富的调制 | FM 6×6 与 Morph；四 LFO、包络/力度/音高/随机/Chaos，16 条分配至 84 个节点目的地 |
| 完整 UI 与布局重构 | Patch/FM/Motion，搜索库、端口画布、连接增益、选中模块检查器、全部参数滚动视图和帮助 |
| 超现实审美 | frontend-design + canvas-design 指导；原生悬浮几何体、拱门、纸面构图、设计板与理念文件；实际截图核查 |
| 获取相关素材 | 实际下载固定 DSP 源码、OFL Instrument Serif 和 DM Mono，并嵌入本地构建 |
| 审美 MCP | 本会话未暴露可调用的专用审美 MCP，资源查询为空；未冒称调用不存在的 MCP/图像工具。视觉目标通过已提供技能与原生绘制实现 |
| 易用和可运行 | 36 预设、端口拖线/点击、矩阵点击/键盘数值编辑、A/B、旧工程兼容、Panic、提示与中文指南 |
| 版本化交付 | Windows x64 FX/Synth/Standalone、源码/依赖、预设、试听、日志/校验；最终状态以验证与标签为准 |

## 算法实际来源

| 效果/系统 | 来源 | 实现边界 |
|---|---|---|
| Prism Pitch / Spectral Freeze | Signalsmith Stretch 1.3.2 + Linear，MIT | 实际 STFT 移调/Formant；冻结持续读取但停止新输入；固有延迟 |
| Grain Cloud | DaisySP GranularPlayer，MIT | 双颗粒实时采集/冻结；整数读取特征，已修边界 |
| Wavefolder / LoFi / Chorus | DaisySP，MIT | 实际波折叠/Decimator/调制延迟；修正 Chorus 初始化和参数顺序 |
| Modal / Resonator / String | DaisySP、Emilie Gillet 等，MIT | 12/24 模态、音频激励弦；非校准真实乐器克隆 |
| Ladder | DaisySP Huovilainen 实现，MIT | 内部 4×；已加 MSVC 属性兼容守卫 |
| WDF Diode | chowdsp_wdf，BSD-3-Clause | 实际电阻源/电容/反并联二极管拓扑；无神经训练或硬件克隆 |
| Spectral | Aether 原创 24 带动态控制 | 与 Prism 的 STFT 不同；无 2048 带 CQT/Bark 掩蔽网络声明 |
| Space | Aether 16 线 Householder FDN | RT60 按反馈设置，阻尼改变高频衰减 |
| Colour / Hz Shift / Ring Mod / Comb / Diffuser / Tape / Chaos | Aether 原创 | 元数据可查；无商业算法复制或还原度承诺 |
| 六算子 FM | Aether 原创 | 16 声部、4× 和输出滤波，极端 PM 仍可能混叠 |
| 图与调制 | Aether 原创 | 固定 12 槽，16 采样控制节奏，权重归一化和有界延迟反馈 |

## 原始研究报告的未完成范围

Rust/nih-plug/WGPU、训练式 DDSP/S4D、NAM/AIDA-X 导入、2048 带 CQT、完整双64模态物理系统、跨进程64轨网络、CLAP/MIDI2、Atmos/64声道、macOS/Linux/ARM64 和嵌入式移植未交付。Atlas 针对这次扩展请求实现新模块、路由、FM 和视觉体系，没有把原研究剩余部分改名为完成。

3.8 MB、1.4% CPU、9.2 MB RAM、<6 ms 冷启动、86 dB 抗混叠 SNR 和120FPS/0%UI CPU 不作为本版本成绩。实际测量和限制见 VALIDATION.md。

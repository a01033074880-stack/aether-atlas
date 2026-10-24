# Atlas 2.1.0 需求验收索引

目标：依据第一阶段文档扩展更多另类开源效果、更丰富效果链、FM式算法/调制路由和超现实UI。依据是实际源码/构建/声音行为/界面/文件，不以计划或自述替代证据。

| 要求 | 权威证据 |
|---|---|
| 20类真实效果/12个可重复槽 | Modular.h枚举/slotCount、ModuleState.h实际分支、Metadata.cpp、ModularTests所有类型/宏/重复实例 |
| 来源是实际开源融合 | vendored源码、CMake选定编译单元、实际类调用、ATLAS_DEPENDENCIES.md、许可证和补丁 |
| 32路由及Custom矩阵 | Metadata.cpp applyAlgorithm、GraphEngine当前/前采样调度、32结构与声音测试、实际画布接线 |
| FM式算法和真实FM | FmSynth.cpp32定义/6算子/16声部/4×、215行为检查、UI矩阵/Morph/载波图和试听 |
| 更丰富调制 | 四LFO/来源池/16分配/84目标的API及算法、全部84目的地变化检查、Motion页面 |
| 三种工作流 | processor源路径、sample-offset MIDI、音频FM分块一致性和FM/Graph独立开关回归 |
| 更好操作 | 搜索库/节点检查器/端口拖线/矩阵/帮助/全参数，真实UI动作及键盘/宿主手势测试 |
| 艺术与布局全重构 | 原生AtlasTheme/几何构图、独立设计板及理念、Patch/FM/Motion/Allparams多尺寸截图 |
| 审美技能/获取素材 | frontend-design+canvas-design应用记录，实际OFL字体文件/许可；无可用专用审美MCP，已明确披露替代实现方式 |
| 兼容、状态和预设 | 首39ID稳定；schema1关闭新引擎、schema2完整矩阵、A/B、36工厂/文件预设及集成检查 |
| 发布可用 | 两个实际VST3的pluginval5 SUCCESS、四CTest套件、ZIP/source/hash/install一致性 |

实际限制（PDC0、极端混叠、结构切换、16声部CPU、单音跟踪、未测宿主/平台）与实现功能一同列出，见VALIDATION.md。原始白皮书的全部研究愿景不被自动当作本次验收内容，也未用未实现研究能力为Atlas命名背书。

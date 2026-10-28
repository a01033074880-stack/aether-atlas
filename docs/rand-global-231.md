# AETHER 2.3.1 — 随机化全局化：根因与改动方案

## 症状
1. 01 Patch 页的 Random 不是全局的，而且“像卡死”。
2. 03 Motion 页几乎没有随机；调制行的 source / destination 从不被随机。
3. 随机后可能出现冲突（反馈环、死调制、静音载波、无限尾音）。

## 根因（均已在 2.3.0 代码里核实）
- 面板写死 `Safety::Safe`；Safe 档窗口窄、硬规则多（≤5 活动槽、仅槽 1..8、不选 alg 32、不碰 Frozen 旋钮）。
- 参数分类：495 个里只有 175 个是 Random，273 个 Structural（节点类型/开关、路由矩阵、FM 矩阵、调制行 source/target、
  fm.custom…）只由“结构阶段”写，且结构阶段只开 8 个槽、只用预设 FM（`fm.custom` 恒 0）。
- Mutate 默认 amount 0.35 只推 175 个 Random 值，从不动结构，也从不动调制行；Motion 在 Mutate 里只微调已有行的 depth。
- 图关闭（`graph.enabled=0`）时 Full 返回空 patch（`graph-disabled`），UI 表现为“没反应/卡死”。
- 没有统一的“修复”阶段：只有事后 `validate` 拒绝，再重投（attempt 0..5），拒绝率高时用户看到“无变化”。

## 改动方案
- 新增 `Safety::Global`（index 3，默认档）：窗口取 Wild，硬规则放宽为 ≤10 活动槽（12 槽全可用）、
  同类型 ≤3、高风险 ≤5、冻结类型 ≤1（冻结旋钮 <0.5）、调制行 6..16 条。`Safe/Normal/Wild` 行为保持原样。
- 分类 175/273/47 → **445/1/49**：272 个 Structural 转 Random（角色不变，由结构阶段构造，不走通用采样器）；
  `graph.trim` 仍 Structural（引擎不写）；`fm.gain`、`source.gain` 新增 Never。
- 总线音量全部 Never：`global.input/output/mix`、`master.*`(enabled/width/ceiling/release/dither)、`graph.trim`、`fm.gain`、`source.gain`；
  调制目标 85(source.gain)、105(fm.gain)、87(transpose) 禁止。
- Patch：Global 下 `routing.algorithm` 可选 32（约 13%）→ 构造“自定义星座”：仅 src<dest 的前向弧（无反馈环）、
  ≥1 输入且≥1 输出且前向连通、≤24 弧、权重 {.25,.5,.75,1}（20% 负）。FM：约 20% 构造自定义矩阵
  （`fm.custom=1, fm.morph=0`，仅 src>dst 弧、2..6 条、入权和≤1.5、终端算子皆载波）；预设算法的载波权重取 ≥0.5。
- Motion：Full 重建 6..16 行，source 1..9、destination 1..112（含 Source/FM 目标）、depth 按类别上限、按目标限总和；
  Mutate 以概率 a 重选存活行的 source/dest、以 0.4a 启用空行；`Config::motionFull=true` 让 Mutate 对 Motion 也按 Full 语义。
- 图关闭：Global+Full 先写 `graph.enabled=1`（未锁、范围含 Chain/ModuleParams/Motion）；Mutate 不自动开图；撤销整体恢复。
- 统一校验/修复：`generate` 在 `finish()` 前调用 `repairGlobal()`（冻结旋钮、半空/重复/死行、每目标 depth 总和、行数上限、
  fm.index×入权上限），随后 `validate` 独立复核（含星座/FM 自定义矩阵、总线目标规则）。
- `kRandAlgoVersion` 1→2（同种子结果与 2.3.0 不同，黄金哈希已更新）。

## 验证
- `tests/RandGlobalTests.cpp`：>20 万次 generate（Full/Mutate × 4 档 × 10 种 scopeMask × 3 版本 × 5 类输入 × 随机锁），
  独立检查器 + `validate`；无锁 Global 失败数必须为 0。CMake：`aether_rand_global`。
- 既有 `RandTests / RandStateTests / RandInvariantTests(R02..R18)` 全部保持通过（计数 47→49、445/1/49 已更新）。
- 未验证：JUCE 构建、`RandCoverageTests`、`RandWiringTests`、pluginval、听感、MSVC 位一致。

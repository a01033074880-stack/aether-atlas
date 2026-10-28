# AETHER 2.3.0 · B 段 · Controlled Randomization — RandSpec 规格草案(P0)

## 0. 文件头

| 项 | 内容 |
|---|---|
| 文档 | `docs/randomization-spec.md`(附表 `docs/randomization-spec-params.tsv`) |
| 状态 | **草案,待用户评审(draft pending user review)**。**纯文档,没有任何代码、CMake、测试改动。** |
| 分支 | `docs/rand-spec-230`(工作树 `wt-randspec`) |
| 基线提交 | `8d92821cefd688833fda2fddae7f8bceb9275b29`(`integration/2.3.0-rc`,"Merge docs/test-adapt-230 (69b4e5a)…") |
| 日期 | 2026-10-25(Asia/Shanghai,UTC+8) |
| 事实来源 | 以**代码**为准;调研报告 `aether-randomization-research.md` 仅作思路参考(仓库外,未拷入)。每个数字来自读过的源码或跑过的命令,不确定处写「待确认 / 待实测 / 未验证 / 待用户确认」。 |
| 参数核对方法 | 在仓库外用一次性探针(`parameterSpecs()` + `Metadata.cpp` + `Modular.cpp`/`FmSynth.cpp` 的真实导出,链接工作树源码构建,**不提交**)展开全部 ParameterID,再用脚本逐 id 分类并断言「每个 id 恰好命中一条规则」。命令与输出见仓库外的 `wt-source-logs/randspec/verify.txt`。 |
| 许可证纪律 | 只取思想,全部用自己的话重写;不含任何 GPL / 无许可证项目的代码或文字;不使用 Mutable Instruments 商标或模块名(本仓库的模块名均为自有名称,如 Modal / Resonator / String 等)。 |

### 0.1 阅读约定

- **「草案,不是实现」**:带该标记的 C++ 片段只是接口示意,不能直接编译。
- **edition 记号**:`FX` = Micro 的 `global.mode`=0;`Resynth` = Micro 的 mode=1;`Synth` = Micro Synth 的 mode=2。Micro(FX/Resynth)插件的有效 mode 只能是 0 或 1,Micro Synth 恒为 2(见 §4)。
- **「单位值」** = 参数的真实范围值(dB、Hz、0..1…);**「归一化」** = JUCE 的 0..1(`NormalisableRange{min,max,step,skew}`,Parameters.cpp:225,`convertTo0to1` 先取比例再 `proportion^skew`)。
- 除非特别说明,**表中范围均为单位值**;节点旋钮 `node.N.a..d` 是 0..1 的旋钮值(其到 Hz/dB 的映射在 DSP 内部,**曲线未逐一核对,待确认**),抽样在旋钮空间进行。

### 0.2 与调研报告的差异(代码为准)

| # | 调研报告 | 代码实际 | 来源 |
|---|---|---|---|
| 1 | 495 参数 | **495**(一致) | `totalParameterCount`=495,`parameterSpecs()` 大小 495 |
| 2 | 12 槽 | 12(一致) | `slotCount`=12 |
| 3 | 20 种模块类型 | **21 种**(含 Thru=0;非 Thru 为 20) | `moduleTypeCount`=21,Metadata.cpp |
| 4 | 32 路由 | `routing.algorithm` 有 **33 个选项**:0..31 为 32 个预设图,32 = Custom constellation | 选项导出 |
| 5 | 旋钮叫 k1..k4 | **`node.N.a..d`**(Macro A..D),含义随 `node.N.type` 变化 | Parameters.cpp:123 |
| 6 | 7 个调制源 | `modulation.N.source` 有 **10 个选项**:None + LFO1..4 + Envelope + Velocity + Pitch + Random Hold + Chaos(**9 个真实源**) | `sourceCount`=10 |
| 7 | 16 条分配 | 16 行(一致),每行 source/target/depth 三元组 | `modulationCount`=16 |
| 8 | 目标 1..84 | **113 个目标选项**:0=None,1..84 = 节点 12×7,**85..112 = Source/FM 目标(2.3.0 新增)** | `modulationTargetCount`=113 |
| 9 | 6 算子 / 32 FM 算法、4 LFO | 一致 | `fmOperatorCount`=6,`fmAlgorithmCount`=32 |
| 10 | 路由矩阵 / 节点输入输出皆可随机 | **`routing.algorithm`<32 时 `node.N.input/output` 与 `route.*` 被 `applyAlgorithm` 覆盖**,仅算法 32 时生效;`fm.edge.*` 仅 `fm.custom=1` 时生效 | AtlasParameters.cpp `graphParameters()`;FmSynth.cpp:252 |
| 11 | 「FM 矩阵 2–6 个非零项」 | 出厂 32 个 FM 算法边数为 **0..7**(算法 17 为 0,算法 21 为 7);「2–6」只能作为**自定义矩阵**的设计准则 | FM 算法导出(§5.4) |
| 12 | 无 mode 钳制 | **已有** `clampModeForPluginType` / `ModeClamp` / `effectiveMode`(§4) | Parameters.h:34;PluginProcessor.h:48 |
| 13 | 「批量通知路径待确认」 | **没有批量通知路径**:出厂/文件加载逐参数 `beginChangeGesture + setValueNotifyingHost`;`toggleAB` 与 `setStateInformation` 用 `replaceState`(§P.5) | PresetManager.cpp:52-64,136-145,162-170 |
| 14 | — | `modal.tune` 一类经典链参数在 Atlas/FM/Source 活动时被禁用,**无 UI**;但 `modal.tune` 仍兼作 FM 移调 | PluginProcessor.cpp:390;docs |
| 15 | — | **有反馈弧的路由算法令 `getTailLengthSeconds()` 返回 ∞**(只要 `graph.feedback`>0 且存在非零反馈连接);冻结旋钮(≥阈值 0.5)同理 | PluginProcessor.cpp:63-88 |
| 16 | — | `fm.edge.*` 为 36 个、`fm.op.N.*` 为 36 个(每算子 ratio/level/carrier/detune/attack/release) | 参数导出 |

## 1. 参数清单与分组计数(已对代码核对)

### 1.1 布局(`Parameters.h`/`Parameters.cpp`/`AtlasParameters.cpp`)

| 区段 | 偏移 | 数量 | 内容 |
|---|---|---|---|
| 经典 | 0..38 | 39 | global.*(6) + spectral/modal/colour/space(23) + mod.*(5) + master.*(5) |
| graph | 39..42 | 4 | graph.enabled / routing.algorithm / graph.feedback / graph.trim |
| node | 43..174 | 132 | 12 槽 × 11(type,on,mix,level,pan,a,b,c,d,input,output) |
| route | 175..318 | 144 | 12×12 连接权重 `route.D.S` |
| lfo | 319..334 | 16 | 4 LFO × (rate,phase,shape,sync) |
| modulation | 335..382 | 48 | 16 行 × (source,target,depth) |
| fm | 383..394 | 12 | enabled/algorithm/custom/index/morph/feedback/attack/decay/sustain/release/gain/spread |
| operator | 395..430 | 36 | 6 算子 × (ratio,level,carrier,detune,attack,release) |
| fmEdge | 431..466 | 36 | `fm.edge.D.S` 6×6 |
| source | 467..494 | 28 | engine/enabled/antialias/seed/attack/decay/sustain/release/gain/spread/transpose + pg.*(10) + chaos.*(7) |
| **合计** | | **495** | 39+4+132+144+16+48+12+36+36+28 |

所有 495 个参数均 `automatable=true`(探针导出),即宿主对任何一个都能看到并录制自动化。无重复 id(`DUPLICATE_IDS=0`)。版本:467 个为 version 1,28 个 Source 参数为 version 2。

### 1.2 七个分组(本规格的分类单位)与计数

| 分组 | 数量 | 包含 |
|---|---|---|
| **Chain** | **194** | routing.algorithm、graph.feedback、node.N.type(12)、node.N.on(12)、node.N.input/output(24)、route.D.S(144) |
| **ModuleParams** | **84** | node.N.mix/level/pan/a/b/c/d(12×7) |
| **FM** | **84** | fm.*(12)、fm.op.N.*(36)、fm.edge.*(36) |
| **Source** | **28** | source.*(含 pg.*、chaos.*) |
| **Motion** | **64** | lfo.N.*(16)、modulation.N.*(48) |
| **MixGlobal** | **9** | global.input/output/mix、master.*(5)、graph.trim |
| **System** | **32** | global.bypass/mode/order、graph.enabled、spectral/modal/colour/space.*(23)、mod.*(5) |
| **合计** | **495** | 194+84+84+28+64+9+32 |

> 分组是为分类服务的人为划分,与代码里的 `ParameterSpec::group` 字符串(「Node 1」「LFO 1」「Atlas」…)不同。

## 2. 分类规则

### 2.1 三类、八种角色、九个安全类

- **分类(classification)**:每个 ParameterID 恰好属于下列之一
  - **Random**:数值/选择/开关类,可被数值采样器直接抽取或变异(受 Scope、锁、activeWhen 约束)。
  - **Structural**:决定拓扑或「哪些部分存在」,只由**结构流**按规则生成(不进入数值采样器,Mutate 模式默认不动)。
  - **Never**:随机化永不写入(只读取)。
- **角色(role)**:`Value`(连续值)、`Choice`(离散选择)、`Boolean`、`Structural`、`ModSource`、`ModDest`、`ModDepth`、`Never`。
  - 注:本表中开关类结构参数(`node.N.on`、`fm.enabled`…)的角色记为 `Structural`,类型列为 `bool`;`Boolean` 角色保留给将来非结构的开关(目前没有 Random 的纯布尔)。
- **安全类(safety)**:

| 类 | 含义 | 典型 |
|---|---|---|
| S0 | 无特殊风险(音色类) | 大部分旋钮 |
| S1 | 电平相关,可改变响度 | mix/level、Drive、gain、Trim |
| S2 | 反馈/自激风险 | Resonance、Feedback、fm.index、fm.op.level |
| S3 | 音高/调谐 | 比率、Detune、Fundamental、Pitch/Shift |
| S4 | 冻结/长尾/无限尾音 | Freeze 旋钮、Decay、Release |
| S5 | 瞬态/点击/重置风险 | source.antialias、source.seed |
| S6 | 主机/系统/版本,不可随机 | bypass、mode、sync、经典链 |
| S7 | 结构(拓扑) | type/on/algorithm/路由/FM 矩阵 |
| S8 | 调制 | 调制行的源/目标/深度 |

### 2.2 规则表(每个 id 恰好命中一条;`校验:495 个 id 命中规则 495 次,无遗漏、无重复`)

下表的「匹配」是正则(`\d+` 表示槽号/行号/算子号),「数量」是该规则在 495 个 id 中命中的个数。脚本断言:每个 id 命中的规则数 == 1,且没有未使用的规则。

| 规则名 | 匹配(正则) | 数量 | 分类 | 分组 |
|---|---|---|---|---|
| sys.bypass | `global\.bypass` | 1 | Never | System |
| sys.mode | `global\.mode` | 1 | Never | System |
| sys.order | `global\.order` | 1 | Never | System |
| sys.graph | `graph\.enabled` | 1 | Never | System |
| sys.legacy | `(spectral\|modal\|colour\|space)\.[a-z]+` | 23 | Never | System |
| sys.legacymod | `mod\.(rate\|depth\|target\|shape\|sync)` | 5 | Never | System |
| mix.global | `global\.(input\|output\|mix)` | 3 | Never | MixGlobal |
| mix.master | `master\.(enabled\|width\|ceiling\|release\|dither)` | 5 | Never | MixGlobal |
| mix.trim | `graph\.trim` | 1 | Structural | MixGlobal |
| chain.algo | `routing\.algorithm` | 1 | Structural | Chain |
| chain.fb | `graph\.feedback` | 1 | Random | Chain |
| chain.type | `node\.\d+\.type` | 12 | Structural | Chain |
| chain.on | `node\.\d+\.on` | 12 | Structural | Chain |
| chain.io | `node\.\d+\.(input\|output)` | 24 | Structural | Chain |
| chain.route | `route\.\d+\.\d+` | 144 | Structural | Chain |
| mod.mix | `node\.\d+\.mix` | 12 | Random | ModuleParams |
| mod.level | `node\.\d+\.level` | 12 | Random | ModuleParams |
| mod.pan | `node\.\d+\.pan` | 12 | Random | ModuleParams |
| mod.macro | `node\.\d+\.[abcd]` | 48 | Random | ModuleParams |
| fm.enabled | `fm\.enabled` | 1 | Structural | FM |
| fm.algo | `fm\.algorithm` | 1 | Structural | FM |
| fm.custom | `fm\.custom` | 1 | Structural | FM |
| fm.index | `fm\.index` | 1 | Random | FM |
| fm.morph | `fm\.morph` | 1 | Random | FM |
| fm.feedback | `fm\.feedback` | 1 | Random | FM |
| fm.attack | `fm\.attack` | 1 | Random | FM |
| fm.decay | `fm\.decay` | 1 | Random | FM |
| fm.sustain | `fm\.sustain` | 1 | Random | FM |
| fm.release | `fm\.release` | 1 | Random | FM |
| fm.gain | `fm\.gain` | 1 | Random | FM |
| fm.spread | `fm\.spread` | 1 | Random | FM |
| fm.ratio | `fm\.op\.\d+\.ratio` | 6 | Random | FM |
| fm.oplevel | `fm\.op\.\d+\.level` | 6 | Random | FM |
| fm.carrier | `fm\.op\.\d+\.carrier` | 6 | Structural | FM |
| fm.detune | `fm\.op\.\d+\.detune` | 6 | Random | FM |
| fm.openv | `fm\.op\.\d+\.(attack\|release)` | 12 | Random | FM |
| fm.edge | `fm\.edge\.\d+\.\d+` | 36 | Structural | FM |
| src.engine | `source\.engine` | 1 | Structural | Source |
| src.enabled | `source\.enabled` | 1 | Structural | Source |
| src.aa | `source\.antialias` | 1 | Never | Source |
| src.seed | `source\.seed` | 1 | Never | Source |
| src.transpose | `source\.transpose` | 1 | Never | Source |
| src.attack | `source\.attack` | 1 | Random | Source |
| src.decay | `source\.decay` | 1 | Random | Source |
| src.sustain | `source\.sustain` | 1 | Random | Source |
| src.release | `source\.release` | 1 | Random | Source |
| src.gain | `source\.gain` | 1 | Random | Source |
| src.spread | `source\.spread` | 1 | Random | Source |
| pg.seq | `source\.pg\.sequency` | 1 | Random | Source |
| pg.walshmix | `source\.pg\.walshmix` | 1 | Random | Source |
| pg.czshape | `source\.pg\.czshape` | 1 | Random | Source |
| pg.czdepth | `source\.pg\.czdepth` | 1 | Random | Source |
| pg.czenv | `source\.pg\.czenvelope` | 1 | Random | Source |
| pg.czvel | `source\.pg\.czvelocity` | 1 | Random | Source |
| pg.vpsd | `source\.pg\.vpsd` | 1 | Random | Source |
| pg.vpsv | `source\.pg\.vpsv` | 1 | Random | Source |
| pg.vpsratio | `source\.pg\.vpsratio` | 1 | Random | Source |
| pg.phaserand | `source\.pg\.phaserandom` | 1 | Random | Source |
| ch.segments | `source\.chaos\.segments` | 1 | Random | Source |
| ch.dist | `source\.chaos\.distribution` | 1 | Random | Source |
| ch.ampstep | `source\.chaos\.ampstep` | 1 | Random | Source |
| ch.durstep | `source\.chaos\.durstep` | 1 | Random | Source |
| ch.durrange | `source\.chaos\.durrange` | 1 | Random | Source |
| ch.momentum | `source\.chaos\.momentum` | 1 | Random | Source |
| ch.ampbarrier | `source\.chaos\.ampbarrier` | 1 | Random | Source |
| lfo.rate | `lfo\.\d+\.rate` | 4 | Random | Motion |
| lfo.phase | `lfo\.\d+\.phase` | 4 | Random | Motion |
| lfo.shape | `lfo\.\d+\.shape` | 4 | Random | Motion |
| lfo.sync | `lfo\.\d+\.sync` | 4 | Never | Motion |
| row.source | `modulation\.\d+\.source` | 16 | Structural | Motion |
| row.target | `modulation\.\d+\.target` | 16 | Structural | Motion |
| row.depth | `modulation\.\d+\.depth` | 16 | Random | Motion |
| **合计** |  | **495** |  |  |

### 2.3 计数结果(脚本输出,见 `verify.txt`)

| 分组 | Random | Structural | Never | 合计 |
|---|---|---|---|---|
| Chain | 1 | 193 | 0 | 194 |
| ModuleParams | 84 | 0 | 0 | 84 |
| FM | 39 | 45 | 0 | 84 |
| Source | 23 | 2 | 3 | 28 |
| Motion | 28 | 32 | 4 | 64 |
| MixGlobal | 0 | 1 | 8 | 9 |
| System | 0 | 0 | 32 | 32 |
| **合计** | **175** | **273** | **47** | **495** |

按类型:float 406、choice 60、bool 29。按角色:Value 147、Choice 12、Structural 241、ModSource 16、ModDest 16、ModDepth 16、Never 47。按安全类:S0 96、S1 36、S2 10、S3 13、S4 14、S5 2、S6 36、S7 240、S8 48。

> 说明:Structural 数量大(273)主要因为 144 个 `route.*`、24 个 `node.N.input/output`、36 个 `fm.edge.*` 属于「仅自定义模式生效」的结构参数,默认**不抽样**(见 §5.3/§5.4)。「可被默认流程实际改写的 id」远少于 273。

### 2.4 Never 名单(47 个)及理由

| id / 规则 | 数量 | 理由 |
|---|---|---|
| `global.bypass` | 1 | 宿主旁通语义,随机化不应触碰;`loadFactoryPreset` 也跳过它(PresetManager.cpp:53) |
| `global.mode` | 1 | 由插件形态决定(`clampModeForPluginType`),随机化**读取** `effectiveMode()` 而不写入(§4) |
| `global.order` | 1 | 经典链信号顺序(24 种),UI 不暴露;Atlas/FM/Source 活动时无效 |
| `graph.enabled` | 1 | Atlas 总开关。关闭则 node/route/lfo/modulation 全部不生效(docs/modulation-destinations.md);随机化在 graph=0 时应**拒绝或先提示**,不自动打开(会改变整个信号路径) |
| `spectral.* / modal.* / colour.* / space.*` | 23 | 经典链,Atlas/FM/Source 活动时被 `processBlock` 整体禁用(PluginProcessor.cpp:390 `atlasSourceActive`),**无 UI**;`modal.tune` 另兼作 FM 移调,更不能乱动 |
| `mod.rate/depth/target/shape/sync` | 5 | 经典单调制器,无 UI,Atlas 路径下无效 |
| **`global.input / output / mix`** | 3 | **Input 增益特别注意**:在 Synth 版里它是 Source/FM 声部增益(PluginProcessor.cpp:282,299,310;UI 提示「The Synth edition has no audio input: this gain scales the Source and FM voices」),在 FX 版是输入电平;随机化改它会直接改变响度与削波,属用户增益结构。Output、Dry/Wet 同理。响度补偿只能走 `graph.trim`(≤0 dB),不碰这三个 |
| `master.enabled/width/ceiling/release/dither` | 5 | 输出级(限幅器/抖动)是母线安全网,随机化不得削弱它 |
| **`source.antialias`** | 1 | 4× 过采样 + 抽取滤波器开关。切换时 `VoiceEngine.h:71-79` 会重设过采样因子并清滤波状态 → 点击/瞬态;且它是音质/CPU 偏好,不是音色参数 |
| **`source.seed`** | 1 | Source 的 PCG32 种子**只在 note-on 读取**(`VoiceEngine.h:112,219`:`v.rng.seed(splitmix64(seed ^ …))`),改动不改变正在发声的音符;且 `source.seed` 与本功能的「随机 seed」是两回事——若随机化去改它,「同一随机 seed 复现同一声音」的语义会被破坏,还会让同一预设每次弹奏听感不同而无法复现。随机化用**自己的 seed** 记入配方 |
| `source.transpose` | 1 | 整体音高(±24 st)是调性参考;也是目标 87。随机化不应改调(与 `modal.tune` 同理) |
| `lfo.N.sync` | 4 | 是否跟随宿主速度属于用户对工程的设定,不是音色;随机化只改 rate/phase/shape |

### 2.5 逐 id 附表

完整 495 行见 `docs/randomization-spec-params.tsv`,列:
`id, type(float/choice/bool), default_norm(默认值的 JUCE 归一化), classification, group, role, safety, range_lo, range_hi(Normal 窗口,单位值), safe_lo, safe_hi, wild_lo, wild_hi, distribution, edition, activeWhen, note`。

- `edition` 取值:`ALL`(FX/Resynth/Synth 均适用)、`RS`(FX 模式跳过,Resynth/Synth 适用;细节见 §4)。
- `range_*` 为 `§3.4` 的参数在 `mod.macro` 行里写 `§3.4`,`fm.op.N.ratio` 写 `set`(离散比率集,§5.4)。
- Never 行的范围列为 `-`,`note` 以 `Never:` 开头并给出理由。
- 默认归一化值按 `proportion^skew` 计算;由脚本生成,不手写。

## 3. 范围表

### 3.1 分布与记号

| 分布 | 抽样(`u`=PCG32 均匀 [0,1),24 位尾数) | 用途 |
|---|---|---|
| Uniform | `x = lo + (hi-lo)·u`(在**旋钮归一化空间**) | 通用 |
| Log | `x = lo·(hi/lo)^u` | 时间、倍率、Hz(参数本身是单位值时) |
| Kumaraswamy `K(a,b)` | `x = (1-(1-u)^(1/b))^(1/a)`(**闭式逆 CDF**,不用 `std::*_distribution`),再映射到 [lo,hi] | 偏低/偏高的「少数大值」分布(a=1,b>1 偏低) |
| Centered | 两个均匀的差(三角分布)围绕中心,中心默认取该参数默认值或 0 | 偏移类:pan、detune、level dB、bias |
| Sparse | 以概率 `P(非零)` 取 0 之外,否则恒为 0;非零取 `U[0.6,1]` | 冻结、morph 等「通常为 0」的开关式旋钮 |

- **mutateStep**:Mutate 时单步位移的尺度倍数(相对窗口宽度)。Uniform/Log=1.0;Centered/Kumaraswamy=0.8;Sparse=翻转(不连续);Choice=重新抽取;Bool=翻转。
- **reflectAtEdge**:Mutate 位移越过窗口边界时是否**反射**(而非夹紧)。连续型=true;Sparse/Choice/Bool=false(夹紧/重抽)。
- **Safe / Normal / Wild 窗口**:每个 Random 参数在三档下的允许区间(单位值或旋钮 0..1)。**Safe ⊂ Normal ⊂ Wild**。窗口都在参数规格范围之内(脚本断言 `RANGE_VIOLATIONS=0`)。
- 所有数值均为**初始值**,放入 `RandSpec` 数据表(可迭代,无需改代码),**全部「待用户确认」**——它们没有经过听测(见 §9)。

### 3.2 全局封顶(与三档绑定)

| 项 | Safe | Normal | Wild |
|---|---|---|---|
| 活动槽数(node.on=1 且 type≠Thru) | 2..5 | 2..8 | 2..8 |
| 路由算法集合 | 20 个无反馈弧算法(§5.3) | 全部 0..31 | 0..31(+ 可选 32,待用户确认) |
| `graph.feedback` | 0.15–0.6 | 0.15–0.8 | 0.1–0.9(代码硬上限 0.95) |
| 调制行数 | ≤4 | ≤8 | ≤12 |
| `fm.index` 有效上限(§5.4) | 3.5 / 算子最大入边和 | 6 / … | 12(代码硬上限) |
| `fm.feedback` | ≤0.25 | ≤0.45 | ≤0.8(硬上限 0.95;有效反馈 = 2×) |
| 高风险模块数 | ≤2 | ≤3 | ≤5 |
| 含冻结/无限尾 | 不允许 | ≤1 个且 P 小 | ≤2 |

### 3.3 非节点参数的范围

非节点参数(Chain/FM/Source/Motion 的 Random 与受控 Structural)的 Safe/Normal/Wild 窗口、分布、activeWhen **已逐 id 写在附表 tsv 中**(由规则表生成;`range_lo/hi` 为 Normal 窗口)。此处不重复 400 行,只列需要强调的:

- `fm.index`:Log,Normal [0.5, 6],规格上限 12(`FmSynth.cpp:353` bound 0..12)。
- `fm.feedback`:K(1,2) 偏低,Normal [0, 0.45]。
- `fm.morph`:Sparse,Safe 恒 0,Normal P=0.2,Wild P=0.4(morph 在算法 i 与 `(i+1)%32` 之间混合,**二者都必须合法**)。
- ADSR(fm / source):Log,Safe 的 release ≤2 s、Normal ≤3 s、Wild ≤8 s(受 `getTailLengthSeconds` 有限尾影响,PluginProcessor.cpp:63-70,`releaseScale` 待确认)。
- `lfo.N.rate`:Log,Normal 0.05–8 Hz,Wild 0.02–30 Hz;`lfo.N.shape`:Safe 排除 Random(4)。
- `modulation.N.depth`:Centered,**上限按目标类别再收紧**(§5.7)。

### 3.4 节点旋钮表:按 (模块类型, 旋钮序号)

`node.N.a..d` 的含义与单位随 `node.N.type` 变化(名称、单位范围、默认值来自 `Metadata.cpp` 的 `macro(...)` 与探针导出)。**窗口为旋钮 0..1 值;抽样在旋钮空间,Hz 类旋钮到 Hz 的映射曲线未核对(待确认)。** `S0..S4` 为安全类。`Thru` 的四个旋钮未使用,**不抽样**。

分类说明:`Uniform(旋钮空间)` 用于 Hz 类;`K(1,1.8) 偏低` 用于 Drive/Decay 等「少数大值」;`Centered` 以 0.5(0 偏移)为中心;`Sparse` 为冻结旋钮。

| type | 模块 | 旋钮(参数 id) | 名称 | 单位范围 | 默认(0..1 旋钮值) | 分布 | Safe 窗口 | Normal 窗口 | Wild 窗口 | 安全类 | 备注 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | Thru | node.N.a | Unused A | 0..1 | 0.5 | — | — | — | — | - |  |
| 0 | Thru | node.N.b | Unused B | 0..1 | 0.5 | — | — | — | — | - |  |
| 0 | Thru | node.N.c | Unused C | 0..1 | 0.5 | — | — | — | — | - |  |
| 0 | Thru | node.N.d | Unused D | 0..1 | 0.5 | — | — | — | — | - |  |
| 1 | Spectral | node.N.a | Threshold | -60..0 dB | 0.5 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 | 阈值 dB |
| 1 | Spectral | node.N.b | Amount | 0..1 | 0.45 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 1 | Spectral | node.N.c | Focus | 100..16000 Hz | 0.2 | Uniform(旋钮空间) | [0.25, 0.75] | [0.1, 0.9] | [0, 1] | S0 |  |
| 1 | Spectral | node.N.d | Transient | 0..1 | 0.6 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 2 | Modal | node.N.a | Fundamental | 40..1200 Hz | 0.15 | Uniform(旋钮空间) | [0.25, 0.75] | [0.1, 0.9] | [0, 1] | S3 | 音高类(Hz):调性随机,待用户确认是否限制 |
| 2 | Modal | node.N.b | Material | 0..1 | 0.35 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 2 | Modal | node.N.c | Brightness | 0..1 | 0.55 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 2 | Modal | node.N.d | Decay | 0..1 | 0.6 | Uniform 偏高 | [0.35, 0.85] | [0.2, 0.95] | [0, 1] | S4 |  |
| 3 | Colour | node.N.a | Drive | 1..12 x | 0.3 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.4] | [0, 0.6] | [0, 0.9] | S1 |  |
| 3 | Colour | node.N.b | Bias | -0.3..0.3  | 0.55 | Centered 三角,中心0.5 | [0.4, 0.6] | [0.3, 0.7] | [0.1, 0.9] | S0 |  |
| 3 | Colour | node.N.c | Tone | 200..18000 Hz | 0.55 | Uniform(旋钮空间) | [0.25, 0.75] | [0.1, 0.9] | [0, 1] | S0 |  |
| 3 | Colour | node.N.d | Character | 0..1 | 0.4 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 4 | Space | node.N.a | Size | 20..250 ms | 0.4 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 4 | Space | node.N.b | Decay | 0.2..20 s | 0.18 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.5] | [0, 0.7] | [0, 1] | S4 | 混响衰减,最长 20 s |
| 4 | Space | node.N.c | Damping | 0..1 | 0.45 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 4 | Space | node.N.d | Diffusion | 0..1 | 0.65 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 5 | Prism Pitch | node.N.a | Pitch | -24..24 st | 0.625 | Centered 三角,中心0.5 | [0.4, 0.6] | [0.3, 0.7] | [0.1, 0.9] | S3 |  |
| 5 | Prism Pitch | node.N.b | Formant | -12..12 st | 0.5 | Centered 三角,中心0.5 | [0.4, 0.6] | [0.3, 0.7] | [0.1, 0.9] | S3 |  |
| 5 | Prism Pitch | node.N.c | Tonality | 0..1 | 0.5 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 5 | Prism Pitch | node.N.d | Width | 0..2  | 0.5 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 6 | Grain Cloud | node.N.a | Speed | 0.25..2 x | 0.43 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 6 | Grain Cloud | node.N.b | Pitch | -12..12 st | 0.5 | Centered 三角,中心0.5 | [0.4, 0.6] | [0.3, 0.7] | [0.1, 0.9] | S3 |  |
| 6 | Grain Cloud | node.N.c | Grain | 10..150 ms | 0.4 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 6 | Grain Cloud | node.N.d | Freeze | 0..1 | 0 | Sparse | 恒 0 | P=0.05→U[0.6,1] | P=0.15→U[0.6,1] | S4 | 冻结:≥0.5 时 getTailLengthSeconds=∞(PluginProcessor.cpp:82-85);Sparse P(非零) Safe 0/Normal 0.05/Wild 0.15,非零取 U[0.6,1] |
| 7 | Wavefolder | node.N.a | Fold | 1..16 x | 0.4 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.4] | [0, 0.6] | [0, 0.9] | S1 |  |
| 7 | Wavefolder | node.N.b | Bias | -0.5..0.5  | 0.5 | Centered 三角,中心0.5 | [0.4, 0.6] | [0.3, 0.7] | [0.1, 0.9] | S0 |  |
| 7 | Wavefolder | node.N.c | Tone | 200..18000 Hz | 0.6 | Uniform(旋钮空间) | [0.25, 0.75] | [0.1, 0.9] | [0, 1] | S0 |  |
| 7 | Wavefolder | node.N.d | Trim | -18..0 dB | 0.7 | Uniform 偏高(电平回落) | [0.6, 1] | [0.45, 1] | [0.2, 1] | S1 | 修整 dB,只允许更低(降低响度) |
| 8 | WDF Diode | node.N.a | Drive | 1..20 x | 0.25 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.4] | [0, 0.6] | [0, 0.9] | S1 |  |
| 8 | WDF Diode | node.N.b | Resistance | 100..10100 ohm | 0.25 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 8 | WDF Diode | node.N.c | Capacitance | 1..101 nF | 0.2 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 8 | WDF Diode | node.N.d | Diodes | 1..4  | 0.33 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 9 | Resonator | node.N.a | Fundamental | 40..1200 Hz | 0.12 | Uniform(旋钮空间) | [0.25, 0.75] | [0.1, 0.9] | [0, 1] | S3 | 音高类(Hz) |
| 9 | Resonator | node.N.b | Material | 0..1 | 0.65 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 9 | Resonator | node.N.c | Brightness | 0..1 | 0.5 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 9 | Resonator | node.N.d | Decay | 0..1 | 0.7 | Uniform 偏高 | [0.35, 0.85] | [0.2, 0.95] | [0, 1] | S4 |  |
| 10 | String | node.N.a | Pitch | 50..1200 Hz | 0.15 | Uniform(旋钮空间) | [0.25, 0.75] | [0.1, 0.9] | [0, 1] | S3 | 音高类(Hz) |
| 10 | String | node.N.b | Brightness | 0..1 | 0.6 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 10 | String | node.N.c | Decay | 0.1..0.94  | 0.65 | Uniform 偏高 | [0.35, 0.85] | [0.2, 0.95] | [0, 1] | S2 |  |
| 10 | String | node.N.d | Excitation | 1..8 x | 0.25 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.4] | [0, 0.6] | [0, 0.9] | S1 |  |
| 11 | Ladder | node.N.a | Cutoff | 40..18000 Hz | 0.18 | Uniform(旋钮空间) | [0.25, 0.75] | [0.1, 0.9] | [0, 1] | S0 |  |
| 11 | Ladder | node.N.b | Resonance | 0..1.4  | 0.3 | Kumaraswamy K(1,2) 偏低 | [0, 0.35] | [0, 0.55] | [0, 0.8] | S2 | 共振 0..1.4,自激边缘 |
| 11 | Ladder | node.N.c | Drive | 0.2..4 x | 0.4 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.4] | [0, 0.6] | [0, 0.9] | S1 |  |
| 11 | Ladder | node.N.d | Compensation | 0..0.5  | 0.5 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 12 | Frequency Shift | node.N.a | Shift | -500..500 Hz | 0.6 | Centered 三角,中心0.5 | [0.4, 0.6] | [0.3, 0.7] | [0.1, 0.9] | S3 |  |
| 12 | Frequency Shift | node.N.b | Stereo | 0..50 Hz | 0.1 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.5] | [0, 0.7] | [0, 1] | S0 |  |
| 12 | Frequency Shift | node.N.c | Feedback | 0..0.8  | 0.15 | Kumaraswamy K(1,2) 偏低 | [0, 0.35] | [0, 0.55] | [0, 0.8] | S2 | 频移反馈 |
| 12 | Frequency Shift | node.N.d | Sideband | 0..1 | 1 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 13 | Ring Mod | node.N.a | Carrier | 1..2000 Hz | 0.07 | Uniform(旋钮空间) | [0.25, 0.75] | [0.1, 0.9] | [0, 1] | S0 | 载波 Hz,音高/金属感 |
| 13 | Ring Mod | node.N.b | Shape | 0..1 | 0.1 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 13 | Ring Mod | node.N.c | Stereo | 0..0.5 cycles | 0.25 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 13 | Ring Mod | node.N.d | Depth | 0..1 | 1 | Uniform 偏高 | [0.35, 0.85] | [0.2, 0.95] | [0, 1] | S0 |  |
| 14 | Comb | node.N.a | Time | 2..80 ms | 0.12 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 14 | Comb | node.N.b | Feedback | -0.9..0.9  | 0.75 | Centered 三角,中心0.5 | [0.4, 0.6] | [0.3, 0.7] | [0.1, 0.9] | S2 | 有符号反馈;Safe/Normal 窗口为 [0.35,0.8]/[0.2,0.9] |
| 14 | Comb | node.N.c | Damping | 0..1 | 0.4 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 14 | Comb | node.N.d | Polarity | -1..1  | 0.8 | Uniform 偏高 | [0.35, 0.85] | [0.2, 0.95] | [0, 1] | S0 |  |
| 15 | Diffuser | node.N.a | Time | 1..30 ms | 0.35 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 15 | Diffuser | node.N.b | Diffusion | 0.1..0.85  | 0.65 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 15 | Diffuser | node.N.c | Damping | 0..1 | 0.4 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 15 | Diffuser | node.N.d | Spread | 0..1 | 0.6 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 16 | LoFi | node.N.a | Rate reduction | 0..0.8  | 0.18 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.5] | [0, 0.7] | [0, 1] | S0 |  |
| 16 | LoFi | node.N.b | Bit crush | 0..0.9  | 0.5 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.5] | [0, 0.7] | [0, 1] | S0 |  |
| 16 | LoFi | node.N.c | Tone | 200..18000 Hz | 0.65 | Uniform(旋钮空间) | [0.25, 0.75] | [0.1, 0.9] | [0, 1] | S0 |  |
| 16 | LoFi | node.N.d | Drive | 1..4 x | 0.2 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.4] | [0, 0.6] | [0, 0.9] | S1 |  |
| 17 | Tape | node.N.a | Drive | 1..8 x | 0.25 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.4] | [0, 0.6] | [0, 0.9] | S1 |  |
| 17 | Tape | node.N.b | Wow | 0..3 ms | 0.3 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.5] | [0, 0.7] | [0, 1] | S0 |  |
| 17 | Tape | node.N.c | Tone | 300..16000 Hz | 0.5 | Uniform(旋钮空间) | [0.25, 0.75] | [0.1, 0.9] | [0, 1] | S0 |  |
| 17 | Tape | node.N.d | Echo | 0..0.8  | 0.2 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.5] | [0, 0.7] | [0, 1] | S2 | 回声反馈 |
| 18 | Spectral Freeze | node.N.a | Pitch | -12..12 st | 0.5 | Centered 三角,中心0.5 | [0.4, 0.6] | [0.3, 0.7] | [0.1, 0.9] | S3 |  |
| 18 | Spectral Freeze | node.N.b | Formant | -12..12 st | 0.5 | Centered 三角,中心0.5 | [0.4, 0.6] | [0.3, 0.7] | [0.1, 0.9] | S3 |  |
| 18 | Spectral Freeze | node.N.c | Freeze | 0..1 | 0 | Sparse | 恒 0 | P=0.05→U[0.6,1] | P=0.15→U[0.6,1] | S4 | 冻结:同上 |
| 18 | Spectral Freeze | node.N.d | Tonality | 0..1 | 0.4 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 19 | Chaos | node.N.a | Speed | 0.1..4  | 0.3 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 19 | Chaos | node.N.b | Depth | 0..1 | 0.6 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 19 | Chaos | node.N.c | Tone | 100..10000 Hz | 0.4 | Uniform(旋钮空间) | [0.25, 0.75] | [0.1, 0.9] | [0, 1] | S0 |  |
| 19 | Chaos | node.N.d | Drive | 1..8 x | 0.25 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.4] | [0, 0.6] | [0, 0.9] | S1 |  |
| 20 | Chorus | node.N.a | Rate | 0.05..5 Hz | 0.12 | Kumaraswamy K(1,1.8) 偏低 | [0, 0.5] | [0, 0.7] | [0, 1] | S0 |  |
| 20 | Chorus | node.N.b | Depth | 0..1 | 0.5 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 20 | Chorus | node.N.c | Delay | 1..5 ms | 0.4 | Uniform | [0.2, 0.8] | [0.1, 0.9] | [0, 1] | S0 |  |
| 20 | Chorus | node.N.d | Feedback | 0..0.8  | 0.25 | Kumaraswamy K(1,2) 偏低 | [0, 0.35] | [0, 0.55] | [0, 0.8] | S2 | 反馈 |

另需注意:

- 各类型默认值取自 `moduleInfo(type).macros[i].defaultValue`(同表第 6 列);切换 `node.N.type` 时 `assignModule` 会把四个旋钮重置为新类型默认(Metadata.cpp:219),**所以「先抽 type,再按新 type 抽旋钮」,顺序固定**。
- 冻结旋钮:`freezeMacro`(Modular.h:24)>=0 的类型,当「旋钮值 + Σ|关联调制深度| ≥ `freezeThreshold`(=0.5,Modular.h:25)」时,`getTailLengthSeconds()` 返回 ∞(PluginProcessor.cpp:82-85)。按旋钮名推断 Grain Cloud 的 D、Spectral Freeze 的 C 为冻结旋钮——**该推断待确认**(`freezeMacro` 的具体赋值未逐类型核对)。因此:冻结旋钮 Safe 恒 <0.5;调制**目标**为冻结旋钮时必须保证 `旋钮值+|depth| < 0.5`。
- `node.N.mix`:Uniform,Normal [0.2, 0.9];`node.N.level`:Centered,中心 0 dB,Normal [-9, +3] dB,Safe [-6, 0] dB;`node.N.pan`:Centered,Normal ±0.6。
- 节点 `type` 的权重与角色见 §5.2。

## 4. Edition 钳制(Micro FX / Resynth 与 Micro Synth)

### 4.1 代码里已有的机制(随机化只读取,不重复实现)

| 位置 | 行为 |
|---|---|
| `Parameters.h:34` `clampModeForPluginType(int mode, bool isSynth)` | `isSynth` → 2;否则 mode==1 → 1;其余(0、2…)→ 0。即 FX 插件的有效 mode ∈ {0,1},Synth 插件恒为 2 |
| `PluginProcessor.h:48` `enum class ModeClamp { none, fxPlugin, synthPlugin }`;`:88` 默认 `JucePlugin_IsSynth ? synthPlugin : fxPlugin` | 钳制类型随编译形态,`setModeClamp()` 可由测试改变 |
| `PluginProcessor.cpp:106-111` `effectiveMode(float stored)` | 「存储的 mode 唯一被解释的地方」(`PluginProcessor.h:100` 注释);`:119` `p.mode=effectiveMode(v(mode))` |
| `PresetManager.cpp:55-62` | Synth 版加载出厂预设时 `global.mode` 强制为 2(值为 0 时改 2) |
| `PluginProcessor.cpp:383-400`(`processBlock`) | `fmActive = fm.enabled>0.5`;`wantedSource = (source.enabled && engine>0 && (mode==1‖mode==2)) ? engine : 0`;`atlasSourceActive = graphActive ‖ ((fmActive ‖ wantedSource>0) && mode≠0)`;FM 路径 `if(fmActive && p.mode!=0)`(:273) |

**随机化的接口约定**:引擎的输入包含 `EditionContext{ effectiveMode, isSynth }`(由处理器用 `effectiveMode(parameterValue("global.mode"))` 与 `modeClamp` 填入)。**永远不写 `global.mode`**。若存储的 mode 与有效 mode 不一致(如 FX 插件里存的是 2),以**有效 mode** 为准。

### 4.2 三种有效 mode 下的规则

| 有效 mode | 规则 | 依据 |
|---|---|---|
| **FX (0)** | **跳过 FM 与 Source**:`fm.*`、`fm.op.*`、`fm.edge.*`、`source.*` 的 Random/Structural 全部不写;**不生成**目标 85..112 的调制行(FM/Source 此时无声,UI 会警告「A row targets FM, but FM is silent now.」/「…Source is silent now.」,PluginEditor.cpp:228-236);只随机 graph 链(Chain/ModuleParams/Motion 的 1..84 目标) | `fmSilent()` = `fm.enabled<=.5 ‖ shownMode()==0`(PluginEditor.cpp:226);`atlasSourceActive` 在 mode 0 只由 `graphActive` 决定 |
| **Resynth (1)** | **仅当 Source 启用时随机 Source/FM**:`source.enabled=1 且 source.engine>0` 才抽 Source 参数与 FM 参数;否则 Source/FM 整段保持不变(不替用户打开)。Resynth 的 FM 随输入音频驱动(UI 提示「FM follows the input audio in Resynth mode.」)。`sourceDriven = (mode==1 && activeSource()!=nullptr)`(PluginProcessor.cpp:258) | 任务约定 + `wantedSource` 条件(:388) |
| **Synth (2)** | **至少 `fm.enabled` 与 `source.enabled&&engine>0` 之一启用**,否则无任何声音源(Micro Synth 没有音频输入);Safe 档默认保证 `fm.enabled=1`;Full 模式若两者都关则由结构流打开 FM(Synth 版默认就是 FM 开) | UI:「No audio input: IN carries the Source and FM voices.」(PluginEditor.cpp:248);`getTailLengthSeconds`(:65)只在 `effectiveMode>0` 计入 FM 释放 |

补充:

- `source.engine` 可取 0=Off/1=Phase Geometry/2=Chaos。engine=0 时 `pg.*`/`chaos.*` 不抽;换引擎会 `reset()` 两个声源(PluginProcessor.cpp:389)——因此 `source.engine` 是 **Structural**,在 Mutate 里不动。
- Synth 版的 Envelope 源跟随声部,Pitch 源取最后一个 MIDI 音(PluginEditor.cpp:195,246);**调制源 6/7 在 FX 版的含义是输入包络/输入音高**。随机化选择源时不区分,但试听信号需按 edition 选择(§7)。
- **edition 钳制由引擎的 `EditionContext` 实现,不是第二个 mode 钳制**;附表 `edition` 列:`ALL`/`RS`。
- 以上钳制在 `Full` 与 `Mutate` 两种模式下都适用(Mutate 本身只改已存在且 activeWhen 为真的 Random 参数,因此天然满足)。

## 5. 结构规则

### 5.1 活动槽

- 「活动槽」= `node.N.on=1 且 node.N.type≠Thru`。数量:Safe 2..5、Normal/Wild 2..8、**下限 1**(仅当用户锁住其余槽时才允许 1)。
- **停用的槽是直通,不是静音**(Modular.cpp `process`:`enabled ? tick : x`)。所以停用不会让信号消失;两个 12 槽的图里,所有算法的 12 个槽都「存活」(导出:每个算法 `live slots`=1..12,`dead slots`=空)。
- 停用槽的写法:`on=0` 且 `type=Thru`(UI 显示为空槽);同时该槽的 mix/level/pan/a..d 不再抽样,**也不得被调制行作为目标**(死目标)。
- 槽位偏好:活动槽从 1..8 号位按递减权重选取(靠前的槽在链式算法里更靠近输入)。**待用户确认**。

### 5.2 模块类型的角色、风险与重复上限

21 种类型(含 Thru)按角色分组如下(分组为本规格的人为划分,名称来自 Metadata.cpp):

| 角色 | 类型(`node.N.type` 值) | 风险标记 |
|---|---|---|
| 谱整形 / 去刺耳 | Spectral(1) | — |
| 失真 / 染色 | Colour(3)、Wavefolder(7)、WDF Diode(8)、LoFi(16)、Tape(17) | **响度**(Drive);Tape 含回声反馈 |
| 谐振 / 物理 | Modal(2)、Resonator(9)、String(10)、Comb(14) | **音高**、**长尾**、Comb 有符号反馈 |
| 滤波 | Ladder(11) | **自激**(Resonance 到 1.4) |
| 音高 / 频率 | Prism Pitch(5)、Frequency Shift(12)、Ring Mod(13) | **音高**;FreqShift 有反馈 |
| 时间 / 空间 / 调制 | Space(4)、Diffuser(15)、Grain Cloud(6)、Chorus(20) | Space 长衰减;Grain Cloud 含冻结 |
| 冻结 / 混沌 | Spectral Freeze(18)、Chaos(19) | **冻结 → 无限尾** |
| 空槽 | Thru(0) | — |

规则(初始值,待用户确认):

- 同一类型重复上限:Safe 1、Normal 2、Wild 3;Thru 不计。
- 「高风险模块」= {Ladder, WDF Diode, Wavefolder, Resonator, String, Comb, Modal, Frequency Shift};计数上限见 §3.2。
- 「冻结类」= {Spectral Freeze, Grain Cloud}:Safe 不抽;Normal 至多 1;且抽到后其冻结旋钮仍受 §3.4 的 `<0.5` 约束(除非 Wild)。
- 建议至少含一个「空间/扩散」类(Space/Diffuser/Chorus)的概率 ≥60%(听感先验,**待听测**)。
- 出厂 24 个 Atlas 预设(`PresetManager.cpp:65-113`,索引 ≥12;`switch(index-12)` 的 24 条链,第 71-96 行)可作为类型组合的参考样本:**每条链 6 个节点(槽 1..6,槽 7、8 关闭),且每条都含 Space**,末尾多为 Spectral/Chorus/Diffuser/Tape。该「收尾模式」是否进入规则 **待用户确认**。

### 5.3 路由算法的合法性(32 个预设图,逐个评估)

数据来自对 `graphParameters()`/`applyAlgorithm` 的真实展开(探针导出的连接矩阵,不是读取文档)。图引擎的相关事实(Modular.cpp):输入缩放 `1/max(1, Σ|in|+Σ|edge|)`,输出缩放 `1/max(1, Σ|out|)`;反馈弧(s≥d)使用上一采样,乘以 `graph.feedback`(≤0.95)并软限幅;控制 tick 每 16 样本。

关于表中列:`输入槽/输出槽` 为被馈入/被取出的槽;`前向边` = s<d 的连接数;`反馈弧` = s≥d 的连接数;`ρ@fb=1` = 回路增益的谱半径(fb=1 时),`稳定所需 fb<` = 令 ρ·fb<1 的 fb 上界;`最小输入缩放` = 任一槽 `1/max(1,Σ|in|)` 的最小值。

| 算法 | 名称 | 输入槽 | 输出槽 | 前向边 | 反馈弧 | ρ@fb=1 | 稳定所需 fb< | 最小输入缩放 | 判定 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | Twelve Steps | 1 | 12 | 11 | 0 | 0.000 | inf | 1.000 | 合法:无反馈弧 |
| 1 | Twelve Horizons | 1..12(12) | 1..12(12) | 0 | 0 | 0.000 | inf | 1.000 | 合法:无反馈弧 |
| 2 | Six Twins | 1,3,5,7,9,11 | 2,4,6,8,10,12 | 6 | 0 | 0.000 | inf | 1.000 | 合法:无反馈弧 |
| 3 | Four Triptychs | 1,4,7,10 | 3,6,9,12 | 8 | 0 | 0.000 | inf | 1.000 | 合法:无反馈弧 |
| 4 | Three Quartets | 1,5,9 | 4,8,12 | 9 | 0 | 0.000 | inf | 1.000 | 合法:无反馈弧 |
| 5 | Two Rivers | 1,7 | 6,12 | 10 | 0 | 0.000 | inf | 1.000 | 合法:无反馈弧 |
| 6 | Root and Leaves | 1 | 2..12(11) | 11 | 0 | 0.000 | inf | 1.000 | 合法:无反馈弧 |
| 7 | Gathering | 1..11(11) | 12 | 11 | 0 | 0.000 | inf | 0.091 | 合法:无反馈弧;汇聚缩放 0.091,响度偏低,修整需补偿 |
| 8 | Binary Canopy | 1 | 7..12(6) | 11 | 0 | 0.000 | inf | 1.000 | 合法:无反馈弧 |
| 9 | Inverted Canopy | 1,2,3,4,5,6 | 12 | 11 | 0 | 0.000 | inf | 0.500 | 合法:无反馈弧 |
| 10 | Diamond Stair | 1 | 11,12 | 14 | 0 | 0.000 | inf | 0.500 | 合法:无反馈弧 |
| 11 | Three Diamonds | 1,5,9 | 4,8,12 | 12 | 0 | 0.000 | inf | 0.500 | 合法:无反馈弧 |
| 12 | Parallel Ladder | 1,7 | 6,12 | 16 | 0 | 0.000 | inf | 0.667 | 合法:无反馈弧 |
| 13 | Braided Steps | 1 | 12 | 21 | 0 | 0.000 | inf | 0.667 | 合法:无反馈弧 |
| 14 | Alternating Banks | 1,2,3,4,5,6 | 7..12(6) | 12 | 0 | 0.000 | inf | 0.667 | 合法:无反馈弧 |
| 15 | Twin Fans | 1,7 | 2..12(10) | 10 | 0 | 0.000 | inf | 1.000 | 合法:无反馈弧 |
| 16 | Long Echo | 1 | 12 | 11 | 1 | 0.444 | 2.250 | 0.556 | 合法(fb≤0.95 线性稳定,ρ·fb<1);尾长=∞ |
| 17 | Nested Echoes | 1 | 12 | 11 | 3 | 0.698 | 1.432 | 0.588 | 合法(fb≤0.95 线性稳定,ρ·fb<1);尾长=∞ |
| 18 | Three Echo Rings | 1,5,9 | 4,8,12 | 9 | 3 | 0.444 | 2.250 | 0.556 | 合法(fb≤0.95 线性稳定,ρ·fb<1);尾长=∞ |
| 19 | Four Echo Rings | 1,4,7,10 | 3,6,9,12 | 8 | 4 | 0.412 | 2.429 | 0.588 | 合法(fb≤0.95 线性稳定,ρ·fb<1);尾长=∞ |
| 20 | Twelve Mirrors | 1..12(12) | 1..12(12) | 0 | 12 | 0.444 | 2.250 | 0.556 | 合法(fb≤0.95 线性稳定,ρ·fb<1);尾长=∞ |
| 21 | Six Crossfeeds | 1,3,5,7,9,11 | 2,4,6,8,10,12 | 6 | 6 | 0.412 | 2.429 | 0.588 | 合法(fb≤0.95 线性稳定,ρ·fb<1);尾长=∞ |
| 22 | Signed Stair | 1 | 12 | 11 | 0 | 0.000 | inf | 1.000 | 合法:无反馈弧 |
| 23 | Signed Horizons | 1..12(12) | 1..12(12) | 0 | 0 | 0.000 | inf | 1.000 | 合法:无反馈弧 |
| 24 | Orbit Six | 1,7 | 6,12 | 11 | 1 | 0.143 | 7.000 | 0.571 | 合法(fb≤0.95 线性稳定,ρ·fb<1);尾长=∞ |
| 25 | Orbit Twelve | 1 | 1..12(12) | 11 | 1 | 0.459 | 2.176 | 0.541 | 合法(fb≤0.95 线性稳定,ρ·fb<1);尾长=∞ |
| 26 | Crossed Rivers | 1,7 | 6,12 | 11 | 1 | 0.000 | inf | 0.588 | 合法:有反馈弧但不成环;fb>0 仍令 getTailLengthSeconds=∞ |
| 27 | Spiral Taps | 1 | 1..12(12) | 11 | 3 | 0.596 | 1.678 | 0.714 | 合法(fb≤0.95 线性稳定,ρ·fb<1);尾长=∞ |
| 28 | Butterfly | 1,2 | 11,12 | 20 | 0 | 0.000 | inf | 0.625 | 合法:无反馈弧 |
| 29 | Cascaded Fans | 1 | 8,9,10,11,12 | 15 | 0 | 0.000 | inf | 0.200 | 合法:无反馈弧 |
| 30 | Weave | 1,4,7,10 | 3,6,9,12 | 17 | 3 | 0.000 | inf | 0.556 | 合法:有反馈弧但不成环;fb>0 仍令 getTailLengthSeconds=∞ |
| 31 | Constellation | 1,4,7,10 | 1..12(12) | 18 | 6 | 0.399 | 2.505 | 0.513 | 合法(fb≤0.95 线性稳定,ρ·fb<1);尾长=∞ |

结论:

- **可达性**:32 个算法中每个槽都可达输入与输出(`dead slots` 全空),无需额外剔除。
- **反馈上限**:所有算法的 `稳定所需 fb<` ≥ 1.432(最小值,算法 17),而代码把 `graph.feedback` 限制为 ≤0.95 → **图本身的线性回路在任何算法下都不会因 `graph.feedback` 变得不稳定**。风险来自模块自身(Ladder 共振、Comb 反馈、FreqShift 反馈、Space 长衰减),由软限幅与输出级兜底;规则里仍给 `graph.feedback` Safe 0.6 的上限。
- **有限尾**:有反馈弧的 12 个算法(16..21、24..27、30、31)只要 `graph.feedback>0` 就令 `getTailLengthSeconds()=∞`(PluginProcessor.cpp:66-69)。算法 26 的唯一反馈弧不成环(ρ=0)但同样触发。→ **Safe 档只使用 20 个无反馈弧算法**(0..15、22、23、28、29),并可把 `graph.feedback` 保持默认以免误导。
- **汇聚响度**:算法 7(1/11)、9/10/11(0.5)、29(0.2)等「多输入汇聚」会按 `1/Σ|in|` 压低电平,修整步骤(§7.3)必须能补偿(只允许写 `graph.trim`(代码范围 −24..+12 dB,默认 0)且**规则限定在 −24..0 dB(只降不升)**;是否允许 +dB 待用户确认)。
- **算法 32(Custom constellation)**:默认**不抽**;如果启用(Wild,待用户确认),需要结构流**构造**路由——只生成严格下三角(s<d)的 DAG,边数 ≤24,权重取 {0.25..1.0},至多 3 条反馈弧——并同样走校验。**不得**对 144 个 `route.*` 做逐项独立抽样。
- 算法选择的权重(初始):Safe 在 20 个无反馈算法上均匀;Normal 无反馈算法权重 1、有反馈算法权重 0.5;Wild 均匀(+32 的小概率,若启用)。

### 5.4 FM 规则

FM 出厂算法(`libs/aether-fm/src/FmSynth.cpp:47-79` 的 `definitions` 表,探针导出矩阵):

| FM 算法 | 名称 | 载波 | 边数 | 自反馈边 | 环 | 每算子最大入边权重和 | 到不了载波的算子 | 判定 |
|---|---|---|---|---|---|---|---|---|
| 0 | Monolith / Six Stack | [1] | 5 | 0 | acyclic | 1.00 | [] | 合法 |
| 1 | Twin Towers / Two Stacks | [1, 4] | 4 | 0 | acyclic | 0.96 | [] | 合法 |
| 2 | Triptych / Three Pairs | [1, 3, 5] | 3 | 0 | acyclic | 0.92 | [] | 合法 |
| 3 | Solar Crown / Five Into One | [1] | 5 | 0 | acyclic | 4.40 | [] | 入边和 4.40,index 上限需按 §5.4 收紧 |
| 4 | River Delta / One Into Five | [1, 2, 3, 4, 5] | 5 | 0 | acyclic | 0.84 | [] | 合法 |
| 5 | Four Storeys / Double Crown | [1, 2] | 5 | 0 | acyclic | 1.60 | [] | 合法 |
| 6 | Forked Stair / Split Chain | [1, 2] | 5 | 0 | acyclic | 0.76 | [] | 合法 |
| 7 | Twin Fans / Two Into Two | [1, 2] | 6 | 0 | acyclic | 1.44 | [] | 合法 |
| 8 | Nested Arches / Double Nest | [1, 2] | 5 | 0 | acyclic | 0.68 | [] | 合法 |
| 9 | Orbit / Six Cycle | [1] | 6 | 0 | CYCLE | 0.64 | [] | 含环(算子级反馈环,靠 clamp ±24 rad 限幅) |
| 10 | Crossed Towers / Bridge | [1, 4] | 5 | 0 | acyclic | 1.20 | [] | 合法 |
| 11 | Diamond / Convergent Crown | [1] | 6 | 0 | acyclic | 1.12 | [] | 合法 |
| 12 | Canopy / Three Branches | [1, 2] | 5 | 0 | acyclic | 1.04 | [5] | 算子 5 孤立(不影响输出) |
| 13 | Sundial / Four Satellites | [1, 2] | 4 | 0 | acyclic | 1.92 | [] | 合法 |
| 14 | Lattice / Layered Pairs | [1, 2] | 6 | 0 | acyclic | 1.88 | [] | 合法 |
| 15 | Falling Rooms / Cascade Taps | [1, 3, 5] | 5 | 0 | acyclic | 0.90 | [] | 合法 |
| 16 | Open Windows / Full Cascade Taps | [1, 2, 3, 4, 5, 6] | 5 | 0 | acyclic | 0.86 | [] | 合法 |
| 17 | Parallel Suns / Six Carriers | [1, 2, 3, 4, 5, 6] | 0 | 0 | acyclic | 0.00 | [] | 合法 |
| 18 | Mirror Pool / Coupled Pairs | [1, 3, 5] | 6 | 0 | CYCLE | 0.78 | [] | 含环(算子级反馈环,靠 clamp ±24 rad 限幅) |
| 19 | Helix / Dual Cycles | [1, 4] | 6 | 0 | CYCLE | 0.74 | [] | 含环(算子级反馈环,靠 clamp ±24 rad 限幅) |
| 20 | Wheel / Hub And Return | [1] | 6 | 0 | CYCLE | 3.50 | [] | 含环(算子级反馈环,靠 clamp ±24 rad 限幅); 入边和 3.50,index 上限需按 §5.4 收紧 |
| 21 | Braided River / Two Bridges | [1] | 7 | 0 | acyclic | 1.32 | [] | 合法 |
| 22 | Pendulum / Carrier Recursion | [1] | 6 | 1 | acyclic | 1.24 | [] | 含自指边 |
| 23 | Glass Steps / Two Recursive Tops | [1, 4] | 6 | 2 | acyclic | 1.16 | [] | 含自指边 |
| 24 | Triangular Courtyard / Three Cycle | [1, 4] | 5 | 0 | acyclic | 0.54 | [] | 合法 |
| 25 | Asymmetric Arch / Long Short | [1, 2] | 4 | 0 | acyclic | 0.50 | [] | 合法 |
| 26 | Suspended Bridge / Four Outputs | [1, 2, 3, 4] | 4 | 0 | acyclic | 0.92 | [] | 合法 |
| 27 | Five Bells / Paired Accent | [1, 3, 4, 5, 6] | 1 | 0 | acyclic | 0.98 | [] | 合法 |
| 28 | Folded Map / Cross Modulation | [1, 2] | 6 | 0 | acyclic | 1.82 | [] | 合法 |
| 29 | Spiral Rooms / Recursive Fan | [1, 2, 3, 4, 5] | 6 | 1 | acyclic | 0.83 | [] | 含自指边 |
| 30 | Broken Column / Cascade And Solo | [1, 2] | 4 | 0 | acyclic | 0.73 | [] | 合法 |
| 31 | Impossible Stair / Nested Feedback | [1] | 7 | 0 | CYCLE | 1.26 | [] | 含环(算子级反馈环,靠 clamp ±24 rad 限幅) |

引擎事实:调制深度 = `edge × fm.index`(`edge.weightedDepth = depth × index`,FmSynth.cpp:474),相位偏移夹在 ±24 rad(:297);有效反馈 = 2×`fm.feedback`(:477),`fm.feedback` 上限 0.95(:355);`fm.index` 夹 0..12(:353);算子输出 `amplitude = op.level × audible`(:302);载波归一 `carrierNorm = 1/max(1, Σ carriers)`(:274);4× 过采样(:11),16 声部。

规则(初始值,**待用户确认**):

1. **至少 1 个载波**:预设算法天然满足(每个算法的载波列都 ≥1);自定义矩阵(Wild)时 `fm.op.N.carrier` 至少一个为 1(`carriers[src] = weight × op.carrier`,:265)。
2. **稀疏矩阵**:预设算法不改矩阵。**自定义矩阵**(`fm.custom=1`,仅 Wild):`fm.edge.A.B`(参数名 "OpA to OpB")为 DAG,**方向与出厂一致(出厂边:高号算子调制低号算子,FmSynth.cpp:45-46 注释)**——`fm.edge.A.B` 里哪一维是调制源、哪一维是目标**待确认**(须先读 FmSynth 的矩阵装载代码);**非零项 2..6**;权重取值范围参考出厂权重(约 0.46..1.0,由探针导出的入边权重和反推,离散集合**待确认**)。出厂算法的边数是 0..7,「2..6」是**自定义**的设计准则,不是出厂事实。
3. **每算子调制指数上限**:令 `M = 该算法(及 morph 邻居 `(i+1)%32`)中任一算子的最大入边权重和`(上表倒数第三列)`;规则:`fm.index × M × op.level_max ≤ Cap`,Cap = Safe 3.5 / Normal 6 / Wild 12。例:算法 3(M=4.4)在 Safe 下 `fm.index ≤ 3.5/4.4 ≈ 0.79`;算法 13(M=1.92)在 Normal 下 `fm.index ≤ 3.1`。**`op.level` 是否乘在调制深度上存在歧义(:302 只确认乘在算子输出幅度上),按保守假设(level≤1)处理,待实测。**
4. **反馈上限**:`fm.feedback` ≤ 0.25(Safe)/ 0.45(Normal)/ 0.8(Wild);另外含环的预设算法(9、18、19、20、31)与反馈同时偏高时,风险叠加:这些算法 Safe 档不抽(或把 `fm.feedback` 限到 ≤0.1)。**待用户确认**。
5. **音乐比率集**(`fm.op.N.ratio` 为 Choice 角色,值取集合内):
   - Safe:{0.5, 1, 2, 3, 4};
   - Normal:Safe ∪ {1.5, 5, 6, 8};
   - Wild:Normal ∪ {0.25, 0.75, 7, 9, 11, 12, 16} ∪ 少量非谐值 {1.414, 2.718, 3.14}(探索性,**待听测**);
   - 载波(carrier=1)的 ratio 取 {0.5, 1, 2} 并保证 **至少一个载波 ratio=1**(音高锚);
   - 规格范围 0.125..16,集合在其内。出厂预设 24..31 号用的是 {1, 2, 3, 1.5, 4, 0.5}(PresetManager.cpp:104),与 Normal 子集一致。
6. **算子 detune**:Centered,Safe ±3 cent,Normal ±8,Wild ±30;**算子 attack/release 倍率**:Log,Safe 0.7..1.4,Normal 0.5..2,Wild 0.25..4。
7. **孤立算子**:算法 12 的算子 5 无法到达载波(导出),其 `fm.op.5.*` 的值**不影响声音**;随机化可以照常写(无害),但**不应把调制目标 111(Op 5 level)分给它**——算作死目标(§5.7)。

### 5.5 声源(Source)规则

- Synth 版:`fm.enabled` 与 `source.enabled&&engine>0` 至少其一(§4)。
- `source.engine`:Safe 偏向 Phase Geometry(1);Chaos(2)的 `ampbarrier` 低时更安静,Safe 下限 0.6。引擎特有参数只在对应引擎下抽(附表 `activeWhen`)。
- Source/FM 同开时的响度叠加:修整步骤负责(§7.3)。

### 5.6 调制行数与规则

- **至多 12 行**(共 16 行可用;Safe ≤4、Normal ≤8、Wild ≤12)。Full 模式:先把 16 行全部清为 (None,0,0)(除非被锁),再按规则写入 ≤12 行;**行 13..16 保持清空**,以便未来扩展。
- 每行 = (source, target, depth) **三元组由调制流整体生成**,不是三个独立随机数。校验:
  - 源 ≠ None,目标 ≠ None,`|depth| ≥ 0.02`(不留死行);
  - LFO 源:被引用的 LFO 才抽 rate/phase/shape(附表 `activeWhen`),未被引用的 LFO 保持不变;
  - 同一 (source,target) 不重复;同一目标的多行**先求和**(引擎语义,docs/modulation-destinations.md),所以按目标累计深度上限校验;
  - 目标必须**有效**(见 §5.7)。
- 调制源可用性:Envelope/Velocity/Pitch/Random Hold/Chaos 在 FX 版与 Synth 版含义不同(§4.2),**均可用**;Random 形状的 LFO 在 Safe 不用。

### 5.7 目标 85..112 的 activeWhen、深度上限

**冻结(frozen)**:`modulation.N.target` 的选项列表只在末尾增长,存储值为选项索引;85..112 的含义已冻结,不得重排(docs/modulation-destinations.md)。

85..112 的**生效条件**(与 UI 警告一致):
- 引擎整体:`graph.enabled=1`、有效 mode ≠ FX、目标所属引擎在运行、该行 `source≠None 且 depth≠0`(docs/modulation-destinations.md「The new path is only active while…」)。
- **Source 目标**(85..101):`source.enabled=1 且 engine>0`(PluginEditor.cpp:233 `sourceSounding()`);引擎特定目标需**引擎匹配**:88..96 → Phase Geometry;97..101 → Chaos;85..87 通用。
- **FM 目标**(102..112):`fm.enabled=1` 且 mode≠FX(PluginEditor.cpp:226,232);107..112(Op N level)还要求该算子能到达载波(§5.4 第 7 条)。
- 不满足时引擎渲染与未调制**逐位相同**(文档明确),但会被 UI 警告,所以规则是:**不生成**这样的行。
- `source.chaos.segments`(非目标,结构参数)与 `source.transpose` 外的 Never 参数不作为目标;**目标 87(Source/Transpose)**:Safe/Normal 不抽;Wild 仅允许 `|depth| ≤ 0.02`(`source.transpose` 范围 −24..+24 st,`depth=1` 即整个 48 st 行程,0.02 ≈ ±0.96 st)——**待用户确认**。

**深度语义**:`depth ∈ [-1,1]`,`amount = source × depth` 在目标参数**自己的归一化范围**内累加后夹到 [0,1]。所以 `depth` 等于「旋钮行程占比」。

**按目标类别的 `|depth|` 上限**(初始值,待用户确认):

| 目标类别 | 例 | Safe | Normal | Wild | 理由 |
|---|---|---|---|---|---|
| 节点 Mix / Pan(1..84 目标的归一化语义按文档表,dB/Hz 映射 **待确认**)| 1..84 中的 mix/pan | 0.3 | 0.5 | 1.0 | |
| 节点 Level(dB,行程 36 dB) | level | 0.06(≈±2 dB) | 0.1(≈±3.6 dB) | 0.25 | 响度 |
| 节点旋钮 a..d(普通) | | 0.3 | 0.5 | 1.0 | |
| 节点冻结旋钮 | Grain Cloud D / Spectral Freeze C(待确认) | 不作目标 | 仅当 `旋钮值+abs(depth)<0.5` | `旋钮值+abs(depth)<0.5` 或明示 | 无限尾(PluginProcessor.cpp:84) |
| Source 通用(85,86) | 电平/展宽 | 0.15 | 0.25 | 0.5 | 电平 |
| Source 引擎参数(88..101) | | 0.3 | 0.5 | 1.0 | |
| 目标 87 Transpose | | 不抽 | 不抽 | 0.02 | 调性 |
| FM Mod index(102) | 行程 0..12 | 0.1(≈±1.2) | 0.15 | 0.3 | 刺耳/混叠 |
| FM Feedback(104) | | 0.1 | 0.2 | 0.4 | 自激 |
| FM Morph/Spread(103,106) | | 0.3 | 0.5 | 1.0 | |
| FM Voice level / Op level(105,107..112) | | 0.15 | 0.3 | 0.6 | 电平 |

另:每目标的**深度总和** ≤ 该类别上限(同一目标多行求和后校验);超限时按比例缩小而不是丢弃。

## 6. 引擎约定

### 6.1 随机数

- **PCG32,每个参数一个独立子流**:`PCG32(seed, fnv1a64(paramId))`。参数的取值只依赖 `(seed, id, config)`,与遍历顺序无关;新增参数不会改变已有参数的结果。
- 另有结构流 `PCG32(seed, fnv1a64("@structure"))`、调制流 `PCG32(seed, fnv1a64("@modulation"))`。
- **试听重掷**:第 `k`(0..5)次尝试使用 `attemptSeed = seed ^ (k · 0x9E3779B97F4A7C15)`;配方记录被接受的 `attempt`,这样无需再次试听即可复现(§P.4)。
- 仓库里已有 `aether::source::Pcg32(state, sequence)`(`libs/aether-source/include/aether/Source.h:38-40`),可复用其写法思路;**是否直接复用该类、还是在 `RandSpec` 内自带 PCG32 以避免耦合,待用户确认**(推荐自带,几十行)。
- 均匀数:`u = (next32() >> 8) · 2⁻²⁴`(float 精确)。
- **不使用 `std::*_distribution`**(实现定义,跨标准库不可复现)。Kumaraswamy 用闭式逆 CDF:`x = (1-(1-u)^(1/b))^(1/a)`;`a=1` 时退化为 `1-(1-u)^(1/b)`。
- **`kRandAlgoVersion`**:整数常量,初始 1;任何改变抽样结果的修改(规则表、分布、流名)都必须 +1,配方记录它;旧配方在新版本上**不保证同结果**(UI 提示「algo v1 ≠ v2」)。
- 跨编译器位级一致性:`pow/log/exp` 来自 libm,不同平台末位可能不同。**推荐**:(a) 自带 `log2/exp2` 多项式(≤1e-7),或 (b) 抽样结果量化到 16 位归一化网格。只承诺 **Windows(MSVC)下位级可复现**,跨平台「待实测」(与调研报告风险 5 一致)。

### 6.2 Mutate 与 Amount

- `Amount a ∈ [0,1]`。**初始值**(放入配置表):
  - 步长 `r = 0.35 · a^0.8`(相对窗口宽度的最大位移);
  - 保持概率 `p = 0.25 + 0.65 · (1−a)^1.5`:**我把 p 解释为「该参数保持不变的概率」**(a→0 时 p=0.9,a→1 时 p=0.25),这样 a 越大改动越多。**该解释待用户确认**(如果 p 是「被触碰概率」,则公式含义相反)。
- Mutate:对每个 Random 且 activeWhen 为真、未锁的参数,以概率 `1-p` 触碰;新值 = `clampToWindow(x) + r · mutateStep · δ · 窗口宽度`,`δ` 为三角分布(两个均匀之差),越界按 `reflectAtEdge` 反射;Choice/Bool:以概率 `(1-p)·a` 重抽。
- **Mutate 不改 Structural 与 Never**(含活动槽、type、algorithm、engine、路由);Full 才重掷 Structural(受锁与 Scope 限制)。**待用户确认**。
- **`Amount=0` 逐位不变**:在 `generate()` 入口 `if (amount == 0 && mode == Mutate) return emptyPatch;`——不依赖 `r=0` 的数值结果,避免 `-0.0f`/舍入引入差异。补丁为空即不写任何参数。
- Mutate 起点在窗口之外的参数(用户手动调过头):先夹到窗口再位移;若 `r` 极小则可能因夹紧而被「拉回」窗口——这是有意的(「Safe 窗口外的值首次被触碰时回到窗口」),但需在 UI 中说明。**待用户确认**。

### 6.3 配置表与数据驱动

- `RandSpec`:一张编译期常量表,字段 = 附表 tsv 的列;**任何新增 ParameterID 若不在该表则覆盖率测试失败**(§8,`aether_rand_coverage`)。
- 所有阈值(Safe/Normal/Wild 窗口、深度上限、权重、`r`/`p` 公式的系数)放在配置结构里,不写死在算法中,便于听测后迭代。

### 6.4 持久化:插件状态中的可选 `random` 子树

现状审读(`PluginProcessor.cpp`):

- `getStateInformation`(:470-486):`tree = state.copyState()`;往**副本**写入 `preset`、`snapshotB`、`alternateSnapshot`(B 面的 `ValueTree` 的 XML 字符串)、`program`、`schema`、`version`;`schema = extended ? 4 : 3`,其中 `extended` 来自 `treeUsesExtendedDestination`(任一 `modulation.N.target` ≥84.5,:460-468)对**活动状态与 B 面快照**的并集判断。
- `setStateInformation`(:487-529):拒绝 `size>1 MiB`、非 `AetherState` 根、`schema<1 或 >4`;对 `parameterSpecs()` **逐个已知 id 合并**进规范树(`merge` lambda),再 `state.replaceState(clean)`;之后读 `preset`、`alternateSnapshot`、`snapshotB`、`program`,并 `panic=true`。**未知子树/属性被 `merge` 丢弃**——所以「放进 APVTS 树里的额外子树」在一次 `setStateInformation` 往返后会丢,也会被 A/B 的 `replaceState` 一起换走。
- A/B(`toggleAB`,PresetManager.cpp:162-170):`current=state.copyState()`;`target=snapshot.createCopy(); snapshot=current`;`state.replaceState(target)`。

推荐:

1. **`random` 不放进 APVTS 树**,而是像 `alternateSnapshot` 一样作为**处理器级**数据,在 `getStateInformation` 的副本上写成根属性/子元素 `random`(XML 字符串或子节点),在 `setStateInformation` 里显式读回;A/B 切换**不换走**它(配方属于「当前实时状态」而不是 B 面)。⚠️ 这意味着 B 面快照里没有配方——**「Randomize into B」产生的配方需单独存**(`randomB`,§P.4),待用户确认是否值得。
2. 子树内容(见 §P.4):`{v, seed, algoVersion, mode, amount, safety, scope, attempt, baselineHash, sourcePreset, time}` + `locks`(可选)。
3. **schema 不升级(推荐保持 3/4)**:
   - `random` 只是附加根属性,schema 3/4 的旧版本(2.2.0 及更早)`setStateInformation` 不认识就忽略(它们只读已知属性)——**对旧版安全**,**待实测**(用 SchemaProbe 对 991e493 源码验证,docs/modulation-destinations.md 中已有同类探针先例);
   - schema 的含义是「状态里是否出现旧版无法安全解释的参数值」(目标 85..112),`random` 不改变这一点。
   - 若将来 `random` 必须被旧版**拒绝**(而不是忽略),才需要 schema 5;当前没有这种需求。
4. **风险**:
   - `setStateInformation` 的 1 MiB 上限:配方 + 锁 ≪ 8 KB,无压力;但**不要把 16 步快照环写进状态**(每步 ~2 KB 浮点数组,16 步 ~32 KB 本身不大,但意义不大;环为运行期数据,不持久化)。
   - `treeUsesExtendedDestination` 只看活动树与 B 面 `snapshot`;随机化若在 B 面写入 85..112 目标(Randomize into B),`getStateInformation` 已经会因 `alternateSnapshot` 判成 schema 4(:477-479),无需改。
   - 加载时:先 `merge`+`replaceState`,**再**读 `random`;`random` 与参数值不一致(例如用户之后手动改了很多旋钮)时,配方只表示「来源」,不保证可复现——用 `baselineHash` 与当前状态哈希比较来决定 UI 是否显示「(Random)」脏标记(§P.4)。
5. **`.aether` 预设**(`savePreset` → `presetObject`,PresetManager.cpp:117-128):只写 `product/version/schema/name/parameters`;**默认不写 `random`**,保持格式与 schema 3/4 不变;「把配方存进预设」作为可选项(`Include Random recipe`,**待用户确认**,默认关)。预设名由文件名决定(`savePreset` 把名字设为文件名,:130-133),因此「(Random) seed」名在存盘时会被替换为用户给的文件名。

## 7. 安全护栏

三层,按顺序执行:**构造即安全 → 规则校验 → 离线试听**。三层任何一层失败都不得把半成品写进实时参数。

### 7.1 第一层:构造即安全

- 所有抽样窗口、结构选择(§3、§5)都在「构造」时就限制在 Safe/Normal/Wild 窗口内;不靠事后裁剪。
- 抽样顺序固定:`type`/`on` → 算法 → 节点旋钮(按新 type)→ FM/Source → LFO → 调制行(目标有效性依赖前面结果)。
- 依赖项:调制行的目标只从**已选定的活动槽**与**已启用引擎**中选(§5.7)。
- Mutate:写入前把起点夹到窗口(§6.2)。

### 7.2 第二层:规则校验(纯函数,无音频)

对 `Patch` 作用于 `StateView` 后的**结果状态**检查,不通过则重抽(最多同一 `attempt` 内修复 3 次,再不行换下一个 `attempt`):

1. 活动槽数在档位范围内;Synth:`fm.enabled` 或 `source.enabled&&engine>0` 至少其一(§4)。
2. 算法 ∈ 档位集合;FM:载波≥1、指数上限、反馈上限、比率集合(§5.4)。
3. 调制:行数、目标有效性、按目标类别的累计深度上限、冻结旋钮 `值+|depth|<0.5`(§3.4)。
4. 无 `NaN/Inf`;所有值在参数规格范围内(`parameterSpecs()`)。
5. 锁定项**逐位未变**(§P.3)。
6. 若校验不能通过:返回 `Report{ ok=false, reason }`,**不写任何参数**。

### 7.3 第三层:离线试听(audition)

目的:捕获规则表无法表达的坏结果(静音、爆音、DC、削波、无限增长)并**把响度修整到可比较的水平**。

**试听实例**:一个私有的、无头的 `AetherAudioProcessor`(与 `RenderPresets`/测试里的做法相同:构造 → `prepareToPlay` → `setStateInformation`/`replaceState` → 循环 `processBlock`;已有 `aether_fixtures` 等测试在做同样的事)。不使用活动的音频线程实例,不影响宿主。

**试听信号**(确定性,Edition 相关):
- FX / Resynth:1.5 s 固定信号 = 0.3 s 白噪声脉冲(固定 PCG32 种子)+ 220 Hz 锯齿 0.6 s + 静默 0.6 s(用于检查尾部);
- Synth:MIDI C3 音符,力度 100,持续 1.0 s,再 1.0 s 释放尾。

**试听指标与门限**(初始值,**待用户确认**/待实测):

| 指标 | 门限 | 失败处理 |
|---|---|---|
| 非有限样本(NaN/Inf) | 0 个 | 拒绝并重掷 |
| 峰值 | ≤ +6 dBFS(试听实例的输出级之前,若可读)或实际输出 ≤0 dBFS | 重掷 |
| 全程 RMS | ≥ −60 dBFS(非静音) | 重掷 |
| DC(均值/RMS) | < 0.1 | 重掷 |
| 尾部(最后 0.4 s)RMS 相对前段 | 不得 ≥ 0 dB(持续增长) | 重掷 |
| 响度 | 修整到目标 RMS(默认 −18 dBFS,待确认) | 用 `graph.trim` 补偿,限 −24..0 dB |

**响度修整**:`trimDb = clamp(target − measured, −24, 0)`,写入 `graph.trim`(参数范围 −24..+12,规则限 ≤0)。**为什么不写 `global.output`**:它在 Never 名单(§2.4),是用户增益结构。修整后若仍超门限则视为失败。

**重掷**:最多 **6** 次,每次 `attempt k` 派生自己的种子(§6.1)。6 次后仍失败:**回退**到「低 Amount 的 Mutate」(Amount 0.15、仅 Value 参数),并在 UI 上提示「Rolled safe fallback」。回退结果同样要通过校验层,不再试听。

**性能(实测基础)**:无头渲染 36 个出厂预设 × 3 s:复用同一处理器 14.6 s(≈0.41 s/预设),每次新建实例 23.4 s(≈0.65 s/预设)。试听 1.5 s 信号约 0.2–0.3 s/次,6 次重掷最坏 ≈1.5 s——**这是桌面基准,在插件的工作线程里是否可行(CPU 配额、与宿主音频线程争用、与宿主多实例并存)待实测**。因此试听**必须是异步、可取消**(§P.6)。

**必须在设计里回答的未知项(待实测)**:
1. 在插件进程内另建一个 `AetherAudioProcessor` 是否可行(静态/单例/`MessageManager` 依赖;DSP 部分在测试中已无头运行,但「在宿主进程里的工作线程」未验证)。
2. 内存:每个实例约有多大(FM 4× 过采样 16 声部缓冲、图延迟线)——**待实测**。
3. 试听实例的 `ModeClamp` 必须与当前实例一致(`setModeClamp`)。
4. 若不能起第二个实例,备选:只做规则校验层(不试听),并接受响度未修整——**质量更差,不推荐**。

## 8. 测试计划

### 8.1 已有测试(CTest,`CMakeLists.txt` 里的 `add_test`)

`aether_dsp`、`aether_fm`、`aether_modular`、`aether_source`、`aether_plugin`、`aether_plugin_synth_ui`、`aether_fixtures`、`aether_split_unchanged`、`aether_resynth_split`、`aether_mod_targets`、`aether_modtargets_invariant`、`aether_modtargets_new`、`aether_modtargets_schema4`(共 13 个,已由探针/配置列出;具体构建与运行**不属于本任务**)。

### 8.2 T1–T4(随机化引擎的测试层)

| 层 | 名称(拟) | 内容 |
|---|---|---|
| **T1 不变量** | `aether_rand_invariants` | 对 3 种 edition × 3 档位 × 3 种模式 × N 个种子(CI 默认 N=2000):结果在参数范围内;§7.2 全部规则;Never 参数逐位不变;锁定项逐位不变;Amount=0 的 Mutate 补丁为空;FX 下 FM/Source/目标≥85 零写入;Synth 满足至少一个声源 |
| **T2 渲染** | `aether_rand_render` | 对 M 个种子(CI 默认 M=64,每日扩展 512)用无头处理器渲染 §7.3 的试听信号:无 NaN/Inf、峰值有限、非静音率 ≥ 阈值(Safe ≥ 99%,Normal ≥ 95%,Wild ≥ 85%,**阈值待实测**);Safe 档不出现无限尾 |
| **T3 确定性** | `aether_rand_determinism` | 同 `(seed, config, state)` 两次 `generate` 补丁逐位相同;参数顺序打乱后每个 id 的值不变(per-id 子流);记录一份「金标」文件(`kRandAlgoVersion` + 前 100 个种子的哈希),版本不变时哈希必须不变 |
| **T4 宿主压力** | `aether_rand_hostfuzz` | 随机化后做 `getStateInformation → setStateInformation` 往返,状态等价;随机化过程中同时在另一线程高速 `processBlock`,无崩溃/断言(**需 TSAN/ASAN 构建,待确认是否纳入 CI**);连续 1000 次 Randomize/Undo,内存无增长 |

### 8.3 其他必须的测试

| 名称(拟) | 内容 |
|---|---|
| `aether_rand_coverage` | **覆盖率测试**:遍历 `parameterSpecs()` 的全部 id,每个 id 必须在 RandSpec 表里被分类**恰好一次**;新增参数没有分类 → 测试失败。这就是让「新参数不会悄悄被随机化也不会被遗忘」的闸门。同时断言:总数 == `aether::totalParameterCount`(当前 495),Never 名单与 §2.4 一致 |
| `aether_rand_factory_zerodiff` | **36 个出厂预设零差异**:对每个出厂预设,经「加载 → 引擎未启用(`Amount=0` Mutate 或功能关闭)→ 渲染」与「直接加载 → 渲染」比较,差异 **< −120 dB**(实际应逐位相同)。目的:保证新增的代码路径不改变既有预设的声音。需要 `ctest` 里的渲染夹具(`aether_fixtures` 的做法) |
| `aether_rand_blocksize` | **块大小无关**:同一补丁在块大小 {32, 64, 256, 1024, 4096 以及 512+511 的不规则切分} 下渲染的输出一致(随机化不应引入块相关性;沿用 `aether_fixtures` 已有的块无关检查方法,**阈值待与既有测试保持一致**) |
| `aether_rand_notify` | 通知计数:对每个 `AudioProcessorParameter` 加 `Listener`,统计 `parameterValueChanged` 与 `parameterGestureChanged`;断言「**仅被改写的参数**各收到一组 begin/set/end;未改的零通知」(**这只验证进程内监听器,不等于宿主 DAW 收到通知,待实测**) |
| `aether_rand_state_roundtrip` | `random` 子树:写入 → `getStateInformation` → 旧版源码(991e493)`setStateInformation` 仍能加载(忽略 `random`);新版读回 `random` 与锁集相同;schema 保持 3/4 |
| `aether_rand_undo` | 环容量 16 的 Undo 正确性:第 17 次 push 丢弃最旧;Undo Random 后状态逐位还原(含 B 面与 `presetName`);Undo 不影响与随机化无关的手动编辑(按 §P.2 的约定) |

测试数据不入库:金标哈希、阈值常量放在 `tests/` 下的纯文本/头文件里(后续实现任务决定)。

## 9. 待决问题(每条附推荐默认值)与未验证项

### 9.1 待决问题(按重要性)

| # | 问题 | 推荐默认值 | 备注 |
|---|---|---|---|
| 1 | Mutate 公式里 `p` 的含义 | **`p` = 参数保持不变的概率**(a→0 时 0.9,a→1 时 0.25) | 若用户的原意是「被触碰概率」,公式要改成 `1−p`(§6.2) |
| 2 | Safe/Normal/Wild 的窗口与封顶值(§3.2、§3.4、§5.7) | 采用本文初始值,听测后用 `RandSpec` 配置表迭代,每次改动 `kRandAlgoVersion+1` | 全部「待听测」 |
| 3 | 是否限制音高类旋钮/比率(Ring Mod、FreqShift、Prism、FM 比率、`modal.tune`) | **限制**:音高相关旋钮 Safe 取小范围,FM 比率用谐波集合(§5.4);`source.transpose` 与 `modal.tune` 为 Never | 避免「随机但走调」 |
| 4 | Wild 档是否允许算法 32(Custom constellation)与自定义 FM 矩阵 | **默认不允许**(Wild 才作为可选开关) | 路由/矩阵构造复杂,先发布不含 |
| 5 | Undo Random 之后用户的手动编辑怎么办 | **Undo 还原到随机化之前的整个状态**(含其间手动编辑);UI 在有手动编辑时提示一次 | 另一种(只撤销随机化涉及的参数)实现复杂,需要补丁级快照 |
| 6 | 随机化后是否 `requestPanic`(静音所有声部与效果尾) | **仅当结构变化**(type/algorithm/engine/`fm.enabled`/`source.enabled` 变化)时;Mutate 默认不触发 | `setStateInformation` 一律 panic;(`PluginProcessor.cpp:487-529`) |
| 7 | 种子长度/显示 | **40 位,8 字符 Base32**,名称 `"<预设名> (Random) <seed>"` | 够短、够分享;冲突概率 2⁻⁴⁰ 可忽略 |
| 8 | 试听响度修整是否写 `graph.trim` | **是**,限 −24..0 dB;目标 RMS −18 dBFS(待确认) | `global.output` 属 Never;若用户不愿动 `graph.trim`(也是用户参数),则只做拒绝不做修整 |
| 9 | Never 名单是否确认(§2.4),尤其 `source.seed`、`source.antialias`、Input gain | **确认全部 47 个** | 若把 `source.seed` 开放,需明确「同一随机 seed → 同一声音」的语义被破坏 |
| 10 | 是否把配方写进 `.aether`(`Include Random recipe`) | **默认关** | 默认不改文件格式 |
| 11 | 「Randomize into B」的配方是否单独保存 | **保存**(`randomB`),但**不写入 B 面快照树** | 见 §6.4 |
| 12 | 是否直接复用 `aether::source::Pcg32` | **自带一份**,避免耦合 | 几十行 |
| 13 | 每类模块重复上限 / 高风险上限 / 空间类偏好 / 收尾模式 | 见 §3.2、§5.2 | 待听测 |
| 14 | 试听信号与目标响度 | 见 §7.3 | 待听测 |

#### 9.1.1 已冻结(2026-10-28,用户决策,经总控转述)

下列条目按「推荐默认值」**冻结**,P1 起实现以此为准;其余 §9.1 条目仍按原状态(待听测/待确认)。冻结不等于听测通过:所有听感范围仍是**未验证**。

| # | 冻结决定 | 对规格正文的影响 |
|---|---|---|
| 1 | **`p` = 参数保持不变的概率**(a→0 时 0.9,a→1 时 0.25);被触碰概率 = `1−p` | §6.2 的解释成为正式语义 |
| 2 | **Safe 为默认档位** | **与 §P.1 草案不一致**:草案 `Config.safety = Safety::Normal`;实现里 `Config{}.safety` 默认 `Safety::Safe`,§P.1 以本条为准 |
| 3 | **Wild 不含算法 32**(Custom constellation);也不生成自定义 FM 矩阵(`fm.custom` 恒不被引擎置 1) | §3.2「路由算法集合」Wild 列去掉「+ 可选 32」;§5.3/§5.4 的自定义构造在 P1 不实现 |
| 4 | **响度补偿写 `graph.trim`**(−24..0 dB,只降不升) | §7.3 与 §9.1 #8;P1 的引擎本身**不写 `graph.trim`**(由试听阶段追加一条 diff,§P.1) |
| 5 | **随机化不升 schema**(仍为 3 / 4,按内容决定) | §6.4、§P.7 |
| 6 | **`.aether` 预设默认不写配方**(`Include Random recipe` 默认关) | §6.4 第 5 点、§9.1 #10 |

实现备忘:`kRandAlgoVersion` 仍为 1;冻结的是**语义**,不是窗口数值,窗口数值仍可在听测后迭代(每次改动 `kRandAlgoVersion+1`)。

### 9.2 未验证 / 待实测清单

- 所有「听感」范围(窗口、深度上限、feedback 上限、比率集)——**待听测**,不是代码事实。
- **节点旋钮 0..1 → Hz/dB 的映射曲线**:`moduleInfo()` 给默认值与名称,但未逐类型核对映射;§3.4 的窗口是**旋钮空间**窗口。
- **`freezeMacro` 的逐类型赋值**(`Metadata.cpp` 未能用静态检索确认):Grain Cloud D、Spectral Freeze C 是从名称推断,**待确认**。
- **1..84 号调制目标的归一化语义**:docs/modulation-destinations.md 对 85..112 明确;1..84 按同一规则(depth=1=归一化全行程)**待确认**。
- 节点 Level 的调制是否按 dB 线性(−24..+12 dB,36 dB 行程)**待实测**。
- **跨编译器/平台的位级确定性**(`pow/log/exp`):**待实测**。
- **试听实例**能否在插件的工作线程里运行、成本多大(§7.3):**待实测**。
- **宿主(DAW)是否收到批量写入通知**:**宿主是否收到通知未验证**(§P.5;只能用进程内监听器验证 JUCE 层面)。
- **自动化影响**:随机化会产生大量参数变化事件,不同 DAW 的自动化录制行为(写入 latch/touch)**待实测**。
- `random` 根属性被旧版(2.2.0、991e493)忽略:**待实测**(用 SchemaProbe 做法)。
- Resynth 下 `fm.op.N.level` 是否乘在调制深度上(FmSynth.cpp:302 只确认乘在算子输出上):**待实测**。
- 调研报告中带有「待实测/待确认」的项,本规格未重新验证者:见 §0.2。

## P. 处理器侧接口草案

> **草案,不是实现。** 本节是写给后续实现任务的接口约定,所有 C++ 片段都只是「草案,不是实现」。本任务不改代码。
>
> 已定的用户决策(来自父会话 2026-10-25 22:11 UTC+8 的转述,**不再讨论**):
> 1. 入口是一个**弹出面板(popover)**;
> 2. 随机化后预设名显示为 **`(Random) <seed>`**,并**记住来源预设**;
> 3. **Undo 只覆盖随机化**,名称为「Undo Random」,是一个 **16 步的整状态快照环**;
> 4. **「Randomize into B」放在 popover 里**。

### P.0 约定与现状(代码为准)

| 事实 | 位置 |
|---|---|
| 当前状态 = `AudioProcessorValueTreeState state`(`PluginProcessor.h:37`);A/B 的「另一面」= `juce::ValueTree snapshot`(:89)+ 原子标志 `snapshotB`(:86);两者与 `presetName` 受 `CriticalSection metadataLock`(:90)保护 | `PluginProcessor.h` |
| 读当前名:`currentPresetName()` 持锁拷贝 | `PresetManager.cpp:49` |
| `requestPanic()` 只是设置原子标志,音频线程在 `processBlock` 开头 `reset()`(:376) | `PluginProcessor.h:52`、`.cpp:376` |
| 当前程序号 `currentProgram`(原子);`setCurrentProgram(i)` = `loadFactoryPreset(i)` | `PluginProcessor.cpp:458` |

### P.1 引擎纯函数签名

引擎是**无状态、无 JUCE 依赖**的纯函数(可在 CTest 中不起处理器直接测试):

```cpp
// 草案,不是实现
namespace aether::rand {

inline constexpr int kRandAlgoVersion = 1;

enum class Mode   { Full, Mutate };
enum class Safety { Safe, Normal, Wild };

struct EditionContext {            // 由处理器填入;引擎不写 global.mode
    int  effectiveMode;            // 0 FX / 1 Resynth / 2 Synth(来自 effectiveMode())
    bool isSynth;
};

struct Config {
    uint64_t seed;                 // 40 位有效
    Mode     mode      = Mode::Full;
    float    amount    = 1.f;      // 0..1;Mutate 下 0 => 空补丁
    Safety   safety    = Safety::Normal;
    uint32_t scopeMask = ~0u;      // 见下:Chain / ModuleParams / FM / Source / Motion 位
    int      attempt   = 0;        // 重掷序号 0..5(§6.1);引擎不自己试听
    bool     intoB     = false;    // 只影响调用方写入哪里,不影响抽样
};

struct StateView {                 // 输入:当前状态的只读视图
    std::array<float, aether::totalParameterCount> value;   // 参数单位值(非归一化),按 parameterSpecs() 顺序
    EditionContext edition;
    // presetName 不参与抽样,只进配方
};

struct LockSet;                    // §P.3

struct Patch {
    struct Diff { int specIndex; float value; };            // 单位值
    std::vector<Diff> diffs;       // 只含与当前状态不同的项;按 specIndex 升序
    struct Report { bool ok; const char* reason; int activeSlots; int algorithm;
                    int modulationRows; } report;
};

Patch generate(const StateView&, const Config&, const LockSet&) noexcept;
// 保证:同输入 → 逐位同输出;锁定项、Never 项绝不出现在 diffs 中。
}
```

- 补丁**只包含差异**:调用方按 diff 逐参数提交(§P.5),没有变化的参数**零通知**。
- `generate` 的读取量只有 `StateView`;`parameterSpecs()` 与 RandSpec 表是 `constexpr`/只读全局,线程安全。
- `Patch` 不含试听/响度(`graph.trim` 由试听阶段追加一条 diff,见 §P.6)。

### P.2 快照环(Undo Random)

```cpp
// 草案,不是实现
struct RandomSnapshot {
    enum class Target { Live, AlternateSnapshot } target;    // 被随机化的是实时状态还是 B 面
    std::array<float, aether::totalParameterCount> raw;      // 495 个 float ≈ 1980 字节(单位值)
    juce::String presetName;                                 // 随机化之前的名字
    RandomRecipe  recipe;                                    // 随机化之前生效的配方(可能为空)
    int program;                                             // 随机化之前的 currentProgram
};
class SnapshotRing { /* 容量 16,环形,push 在随机化提交前执行 */ };
```

- **容量 16**;第 17 次 push 覆盖最旧。内存 ≈ 16 × (1980 B + 名字 + 配方) ≈ 32 KB。
- **内容**:整状态(495 个值)+ 名字 + 配方 + 程序号,**不含**撤销/重做栈本身,**不持久化**(运行期数据,会话结束即清空;不写进插件状态,见 §P.7)。
- **线程安全**:环**只由消息线程**读写;音频线程**从不接触**(音频线程只通过 `AudioProcessorValueTreeState` 的原子参数看到结果)。因此**不需要锁**;`metadataLock` 只在读写 `presetName`/`snapshot` 时短暂持有(沿用现有做法)。Undo 的写回也走 §P.5 的提交流程(逐参数 begin/set/end)。
- **Undo Random 还原整个状态**(包括随机化与之后之间用户的手动编辑,这是「整状态快照」的必然结果)。推荐 UI 在检测到手动编辑后提示一次——**待用户确认**(§9 #5)。
- **「Randomize into B」**:`target=AlternateSnapshot`;快照保存的是 B 面树的整份拷贝(`snapshot` 的 `ValueTree` 转为 `raw`),Undo 时写回 B 面树,**不触发宿主通知**,也不改 A 面。
- 手动 A/B 切换(`toggleAB`)**不入环**(任务约定「Undo 只覆盖随机化」);但切换后环里 `target=Live` 的条目对应的是「当时的 A 面」——**待确认**这一语义是否需要在 UI 上提示(推荐:A/B 切换后清空 `Target::Live` 的条目,避免撤销到另一面的状态)。

### P.3 锁定集(LockSet)

```cpp
// 草案,不是实现
struct LockSet {
    std::bitset<aether::totalParameterCount> params;                 // 逐参数锁(按 spec 下标)
    uint32_t sections;           // 整段:Chain 槽 1..12 的每槽一位 | FM | Source | LFO 1..4 每个一位 | Mod 总开关 (2026-10-28 勘误:槽位共 12 个,每槽一位 = kLockSlot0<<s,s=0..11)
    uint16_t modRows;            // 调制行 1..16:一位 = (source,target,depth) 三元组整体锁
    bool isLocked(int specIndex) const noexcept;
};
```

- **粒度三档**:① 单个参数;② 整段(节点槽 N 含其 11 个参数;FM 段含 `fm.*`/`fm.op.*`/`fm.edge.*`;Source 段含 `source.*`;LFO 段含 `lfo.N.*`);③ **调制行三元组**(source/target/depth 同锁同放)。
- **规则**:锁定项在补丁里**不出现**,并且**会约束其他项**(例如锁住的节点槽 N 保持 `on`/`type`,则「活动槽数」计数把它算进去;锁住的调制行占用行数配额;锁住的目标死掉时仍保留,但在报告里标红)。
- **与 Never 的关系**:Never 参数(§2.4)无需锁,总是不动;锁只对 Random/Structural 有意义。
- **持久化**:可选,写进**同一个可选 `random` 子树**(§6.4、§P.7)的 `locks` 节点(位图用十六进制字符串,495 位 ≈ 124 字符);**不写进 `.aether` 预设**(默认,`Include Random recipe` 打开时也不含锁——锁属于「用户操作习惯」而不是「声音」,**待用户确认**)。
- 锁在 Undo 环里**不入快照**(锁不是状态的一部分)。

### P.4 配方元数据(Recipe)

```cpp
// 草案,不是实现
struct RandomRecipe {
    uint8_t  version;            // 配方格式版本(本规格为 1)
    uint64_t seed;               // 40 位
    int      algoVersion;        // kRandAlgoVersion
    Mode     mode;
    float    amount;
    Safety   safety;
    uint32_t scopeMask;
    int      attempt;            // 被接受的重掷序号(不用重新试听即可复现)
    float    trimDb;             // 试听写入的 graph.trim 补偿
    bool     intoB;
    int      edition;            // 有效 mode
    uint64_t baselineHash;       // 随机化开始时 495 个值的 FNV-1a-64(用于 Mutate 的复现与脏标记)
    juce::String sourcePreset;   // 来源预设名(随机化之前的 currentPresetName())
    int      sourceProgram;      // 来源出厂预设号,非出厂为 -1
    int64_t  timeUtcMs;          // UTC 毫秒
    bool     valid;
};
```

- **复现条件**:Full 模式只依赖 `(seed, algoVersion, config, scope, locks, edition)` 与 `attempt`;**Mutate 还依赖 `baselineHash` 对应的起点状态**,所以只有起点可得(来源预设名 + 程序号仍能加载同一状态)时才能复现。
- **种子显示**:40 位 → 8 字符 Base32(Crockford,无 `I L O U`)。
- **名字存放**:预设名字符串存成 **`"<来源预设名> (Random) <seed>"`**,例如 `Motion / Lunar Loom (Random) 7K3M9Q2X`;`presetName` 上限 128 字符(`loadPreset` 的 `substring(0,128)`,PresetManager.cpp:159),**来源名过长时截断来源名**而不是种子。UI 只显示「(Random) 7K3M9Q2X」,悬停显示来源预设。
- **来源预设放在配方里**,不依赖解析名字(名字只是展示;用户重命名/存盘后配方仍在)。
- 存盘:`savePreset` 会把名字改成文件名(PresetManager.cpp:130-133),文件**不含**配方(默认)。已加载的预设一旦被手动大幅修改,配方仍在,但 UI 的「(Random)」脏标记用 `baselineHash` 之外的「**当前值哈希 ≠ 配方结果哈希**」判断(需要在提交时再存一份 `resultHash`,草案里加字段 `uint64_t resultHash;`)。
- 脏标记之后名字**不自动改**(避免用户的预设名被悄悄改写);只是 popover 显示「modified」。**待用户确认**。

### P.5 批量写入与宿主通知

**读到的代码**(PresetManager.cpp、PluginProcessor.cpp):

| 路径 | 写参数的方式 | 宿主能看到什么 |
|---|---|---|
| `loadFactoryPreset`(:50-116) | 对每个 spec:`beginChangeGesture` → `setValueNotifyingHost` → `endChangeGesture`(:63);之后若干 `setParameterValue(...)`(:66-111,该函数的实现**本次未读**);末尾写 `presetName`、`currentProgram`、`requestPanic()` | **逐参数的手势与通知**:495 个参数每次加载都会各发一组(跳过 `global.bypass`) |
| `loadPreset`(:146-161)→ `applyValues`(:136-145) | 同样逐参数 begin/set/end(:143) | 同上。**UI 里「loadPreset 会发通知」的说法与代码一致** |
| `toggleAB`(:162-170) | `state.replaceState(target)`,**不发手势**;之后 `requestPanic()` | JUCE 的 `replaceState` → `setNewState` → `setValueNotifyingHost`(**无** begin/endChangeGesture)。「A/B 不经手势」与代码一致 |
| `setStateInformation`(:487-529) | `replaceState` + 读 `preset`/`alternateSnapshot`/`snapshotB`/`program` + `panic=true` | 同 `toggleAB`:每个值变化的参数会 `setValueNotifyingHost`,但无手势 |

JUCE 8.0.12 的 `replaceState`/`setNewState` 路径行为是对源码的阅读(JUCE `juce_AudioProcessorValueTreeState.cpp`),**宿主(DAW)是否收到通知未验证**:这只能用进程内 `AudioProcessorParameter::Listener` 验证 JUCE 层面(§8.3 `aether_rand_notify`),不能证明任何具体 DAW 的行为。

**推荐**:

1. 随机化的**提交只写补丁中真正变化的参数**,在**消息线程**上用与 `applyValues` 相同的逐参数 `beginChangeGesture → setValueNotifyingHost → endChangeGesture` 循环(沿用现有已被宿主接受的路径,自动化录制行为与加载预设一致)。
2. 不用 `replaceState`(无手势,录制行为与预设加载不同;且会连带替换树里的非参数属性)。
3. **「Randomize into B」只改 `snapshot`**(`ValueTree`),**不发任何宿主通知**,与手动 A/B 一样。
4. `requestPanic()`:**仅当结构变化时**(type/algorithm/engine/`fm.enabled`/`source.enabled`)调用(§9 #6);Mutate 默认不调用,避免尾音被切断。
5. 提交顺序:先 `presetName`+配方,再参数(或反之均可,但名字与参数应在同一消息线程回合内完成,避免 UI 看到半状态);与 `metadataLock` 的交互只涉及 `presetName`/`snapshot` 短暂持锁。
6. 自动化影响:一次随机化最多 495 个参数各发一组手势;宿主自动化的行为**待实测**(§9.2)。

### P.6 异步试听与回送消息线程

```cpp
// 草案,不是实现
class RandomizeJob {
public:
    uint64_t id;                          // 单调递增的 job 号
    std::atomic<bool> cancel{false};
    // 输入快照(消息线程拷贝):StateView + Config + LockSet + baselineHash
};
```

- **线程模型**:一个**单线程**工作线程(或 `juce::ThreadPool` 大小 1)+ **job 号**;新建 job 时对旧 job 置 `cancel=true`,旧 job 的结果**被丢弃**。
- 工作线程内**私有无头 `AetherAudioProcessor`**(§7.3),渲染前先 `setModeClamp` 与当前实例一致;每 N 个块检查一次 `cancel`。
- **回送**:`juce::MessageManager::callAsync`,lambda 持有**弱引用**(`WeakReference<AetherAudioProcessor>` 或 `shared_ptr` 令牌);消息线程上先检查 `jobId == currentJobId` 且 `baselineHash == 当前状态哈希`(用户在试听期间手动改了参数 → 结果过期,**丢弃并提示「Preset changed, roll again」**,或按 Mutate 无关则放行,**待用户确认**);通过后才执行 §P.5 的提交与 §P.2 的压环。
- **UI**:popover 在 job 运行期间显示「**Rolling…**」,按钮变为「Cancel」(置 `cancel`);完成后恢复。
- **生命周期**:处理器析构时 `cancel=true`、等待工作线程结束(带超时),再销毁试听实例;`prepareToPlay`/`releaseResources` 不影响试听实例。
- 失败路径:试听失败/6 次重掷用尽 → 回退(§7.3)并在 popover 提示。
- **未验证**:在插件进程内起第二个处理器实例并在工作线程里渲染的可行性与开销(§7.3 的「待实测」);桌面基准数据:36 预设 × 3 s = 14.6 s(复用)/23.4 s(新建)。

### P.7 与 schema 3/4 的关系

- **推荐不升 schema**(仍为 3 / 4):
  - `random` 子树是**处理器级的可选根属性**,像 `alternateSnapshot` 一样在 `getStateInformation` 的**副本**上追加(`PluginProcessor.cpp:470-486`),在 `setStateInformation` 里显式读取(:487-529);`merge` 逐参数 id 合并的规范树**会丢掉**未知属性,所以必须在 `merge` 之外读取(§6.4)。
  - 旧版(2.2.0,schema 最新=3)对 schema 3 的状态只读已知属性,**应忽略 `random`**(**待实测**,按 docs/modulation-destinations.md 的 SchemaProbe 做法在 991e493 源码上验证);schema 4 的状态 2.2.0 本来就会整体拒绝(`setStateInformation` 直接返回)。
  - `treeUsesExtendedDestination`(:460-468)只看 `modulation.N.target`,与 `random` 无关;「Randomize into B」若写入 85..112 目标,`getStateInformation` 已因 `alternateSnapshot` 把 schema 判成 4(:477-479),**无需改动**。
- 大小:`random` 子树(配方+锁)< 8 KB,远小于 1 MiB 上限(`setStateInformation` 的拒绝门限)。
- 若将来需要「旧版必须拒绝带 random 的状态」才升 schema 5;当前没有该需求。
- **`.aether` 预设**:格式不变(schema 1..4 + `parameters`);默认不写 `random`(§6.4)。

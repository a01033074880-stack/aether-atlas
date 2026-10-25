# AETHER Atlas 2.2.0 使用说明

顶部缩放选择器提供 **50%／75%／100%** 三档。它只改变编辑器窗口和绘制比例，不改变音频、参数、自动化或宿主状态。50%适合查看完整结构，100%适合精细调节。

Atlas 把第一版五级串联扩展为：12 个可重复模块槽、32 种效果路由、六算子/32算法 FM、16 条调制分配和36个预设。三个主要工作区为 **Patch 接线、FM 声源、Motion 调制**。 另有第四个工作区 **Source**（见 §5），提供 Phase Geometry / Chaos 两种声源引擎。

## 1. 安装与插件选择

> 本节和 §5（Source）、§6 末尾的“新增调制目的地”属于 2.3.0 阶段 A 的文档草稿（分支 `docs/test-adapt-230`）。版本号尚未升级，安装包名称仍以实际发布为准。

### 安装位置与 `install.ps1`

2.3.0 起推荐安装到系统 VST3 目录 `C:\Program Files\Common Files\VST3`，写入该目录需要管理员权限。

`scripts/install.ps1` 的实际行为（逐行读自脚本，本分支未修改它）：

- 只有一个参数 `-Destination`，默认值是当前用户目录 `%LOCALAPPDATA%\Programs\Common\VST3`，**不是**系统目录。要装到系统目录，必须在“以管理员身份运行”的 PowerShell 里显式指定：`.\install.ps1 -Destination 'C:\Program Files\Common Files\VST3'`。
- 脚本必须在解压后的发布包里运行（旁边要有 `VST3` 文件夹），否则报错退出。
- 目标目录不存在会自动创建；`VST3` 下每个 `.vst3` 目录依次复制。
- 如果目标位置已有同名 `.vst3`，脚本不覆盖也不删除，而是先改名为 `<原名>.vst3.backup-<yyyyMMdd-HHmmss-fff>`，再复制新版本。备份目录留在同一个 VST3 目录里。
- 脚本不检查管理员权限，也不会清理其他目录里的旧版本。

**旧版本必须先移除或替换。** 2.2.0 的默认位置是用户目录，因此从 2.2.0 升级到系统目录时，用户目录里的旧版本不会被脚本处理：

1. 关闭 DAW（以及所有 AETHER 独立程序）。
2. 删除或移走用户目录 `%LOCALAPPDATA%\Programs\Common\VST3` 下的 `AETHER Micro.vst3` 和 `AETHER Micro Synth.vst3`（以及其中残留的 `*.backup-*` 备份目录）。
3. 如果系统目录里已有旧版 `.vst3`，用脚本替换（旧版会被改名为 `.backup-*`），确认新版工作后手动删除备份。
4. 重新扫描插件；宿主未扫描该位置时手动添加搜索路径。

同一插件同时存在于用户目录和系统目录时，宿主可能列出两份或加载到旧版（具体行为取决于宿主，本文未逐一验证），所以不要两处并存。备份目录名不以 `.vst3` 结尾，但各宿主是否会忽略它们未验证，建议确认后清理。

脚本默认目录是否在 2.3.0 正式版改为系统目录：待定。

### 两个插件与模式拆分

| 插件 | 放置位置 | 可用模式 | 用途 |
|---|---|---|---|
| AETHER Micro（效果器） | 音频/效果轨 | 只有 FX 和 Resynth | FX、侧链激励、音频 Resynth；Source 声源只在 Resynth 下发声 |
| AETHER Micro Synth（乐器） | MIDI/乐器轨 | 只有 Synth | 16声部六算子FM和 Source 声源，没有主音频输入 |

模式选择器只列出当前版本适用的项目。`global.mode` 参数的选项列表和参数ID没有变（0 = FX，1 = Resynth，2 = Synth）；限制发生在处理器读取模式的那一处（`clampModeForPluginType`），不会改写已保存的状态。**旧工程回退规则：**

- AETHER Micro（效果器）读到保存的 Mode = 2（Synth）→ 按 FX（0）运行；0 和 1 保持不变。
- AETHER Micro Synth（乐器）读到保存的 Mode = 0 或 1 → 按 Synth（2）运行。

因此在 AETHER Micro 里打开原来保存为 Synth 的工程，会听到 FX 渲染而不是合成声。

独立程序位于 `Standalone`，从设备设置选择声卡和MIDI。FX独立程序初始静音输入，先确认没有声反馈再启用。插件版本由DAW负责路由。

## 2. 三分钟开始

### 处理音频

1. 音频轨插入 AETHER Micro，Mode选择FX。
2. 选择 `Atlas / Diode Garden`、`Atlas / Grain Cathedral` 或 `Atlas / Shattered Prism`。
3. 点画布节点，在右侧调整该模块四个专用宏。
4. 点上方算法缩略图，比较串联、并联和循环。
5. 用全局Dry/Wet和Output匹配试听；长反馈时点Panic。

### 演奏FM

1. 乐器轨插入AETHER Micro Synth，选择 `FM / Porcelain Keys` 或 `FM / Inverted Bells`。
2. 打开FM页，弹MIDI。
3. 切算法、调Modulation index，再用Algorithm morph插值。
4. 改某个算子的Frequency ratio、Level和To output，观察矩阵和载波输出标记。
5. 只听声源时关闭Atlas graph。六算子仍运行；关闭Six operator engine才使用旧声源。

### 哼唱驱动FM

1. 使用有音频输入的AETHER Micro效果器，选择Resynth。
2. 开启Six operator engine，送入清晰单音。
3. 可从 `FM / Singing Glass` 开始；输入音高和响度控制一个连续FM声部，可叠加MIDI。
4. 和弦、噪声、气声和复杂波形可能跟踪不稳；这里不是训练式DDSP，不能完整保留歌词和复杂原始频谱。

## 3. Patch：搭建效果图

### 模块库与检查器

左侧Materials按名字/类别搜索。先点目标节点，再点模块名称替换该槽；相同效果可重复12次。Thru为透明连接。

| 控制 | 作用 |
|---|---|
| Processor / Enabled | 模块类型/开关；关闭后输入仍沿连接穿过 |
| Mix | 节点处理结果与其输入的干湿混合 |
| Level / Pan | 节点电平和左右平衡 |
| 四宏 | 标签和物理单位随处理器变化，实际控制DSP |
| Input send | 主输入/生成声源直接送入此节点的有符号权重 |
| Output send | 此节点直接送到总输出的有符号权重 |

节点下短条是真实电平，斜线表示处理关闭。换模块清理该槽历史；FFT/大缓冲清理虽不分配内存仍有成本，不宜在极小块长连续高速换类型。

### 算法、线缆与反馈

选择器和32张缩略图包括串联、并联、扇出、汇合、树、菱形、交叉和循环；声音路由实际改变。

- 从OUT拖到IN，或依次点两个端口建立连接。
- 点已有线缆调有符号增益，负值反转极性。
- 右键线或用移除按钮删除。
- 改线缆/Input send/Output send会保存当前有效模板并切Custom constellation。
- All params的键盘/数值编辑也会准备Custom，而不只修改无效的图示数值。

**方向规则：** 小编号到大编号使用当前采样结果；自身或大编号回到小编号使用前一采样。没有无延迟代数环。Graph feedback缩放反向/自反馈，回送带饱和和边界保护。模块内部还可能有反馈。

输入/输出汇合按权重归一化，不是把所有线无限相加；密集并联的增益可能与直接求和不同。

### 20种实际效果

| 模块 | 工作方式/用途 | 宏方向 |
|---|---|---|
| Spectral | 24带动态共振控制，去刺耳/修整共振 | Threshold、Amount、Focus、Transient |
| Modal | DaisySP 12模态共鸣 | 基频、材质、亮度、衰减 |
| Colour | 非对称饱和、滤波、DC处理 | 驱动、偏置、Tone、Character |
| Space | 16线Householder FDN与扩散 | Size、RT60、Damping、Diffusion |
| Prism Pitch | Signalsmith频谱移调和Formant | 半音、Formant、Tonality、宽度 |
| Grain Cloud | 双颗粒读取实时约500ms采集 | 速度、半音、颗粒长度、Freeze |
| Wavefolder | DaisySP波折叠/金属泛音 | Fold、偏置、Tone、Trim |
| WDF Diode | 真正的波数字二极管/电容电路 | 驱动、电阻、电容、二极管参数 |
| Resonator | DaisySP 24模态共振体 | 基频、材质、亮度、衰减 |
| String | 音频激励Karplus–Strong弦 | 音高、亮度、衰减、激励 |
| Ladder | 内部4倍处理的梯形滤波 | 截止、共振、驱动、补偿 |
| Frequency Shift | 解析信号的加法Hz频移，不是倍频移调 | Hz、左右差、反馈、边带 |
| Ring Mod | 音频速率双极性调制 | 频率、形状、左右相位、深度 |
| Comb | 有阻尼的分数延迟梳状滤波 | 时间、反馈、阻尼、极性 |
| Diffuser | 多级全通扩散 | 检查器显示完整名字/单位 |
| LoFi | DaisySP抽样/位深及音色处理 | 检查器显示完整名字/单位 |
| Tape | 延迟、非线性、滤波、wow反馈纹理 | 检查器显示完整名字/单位 |
| Spectral Freeze | STFT捕获、持续和移调变形 | 半音、Formant、Freeze、Tonality |
| Chaos | 有界混沌驱动的音色运动 | 检查器显示完整名字/单位 |
| Chorus | DaisySP调制延迟合唱 | 检查器显示完整名字/单位 |

检查器说明反映实际原理。材质/电路/磁带名称表达处理方式和音色方向，没有承诺复刻特定硬件或校准真实乐器。

## 4. FM：相位架构

### 算法、载波和矩阵

六个算子都是振荡器。一个算子的输出送入另一个算子的相位，改变频谱；送往总输出的算子称为载波。

32种算法包括堆叠、分支、多载波、交叉和递归。图中边与输出标记反映算法、Morph和To output；矩阵列为来源，行为目的地。

- 点单元在0/0.5/1之间切换，右键清除。
- 首次改动保存当前有效矩阵及载波权重后切Custom。若原来正在Morph，会保存插值结果并把Morph归零，避免切换时丢失该状态。
- 非Custom的Morph从当前走向下一算法，第32之后回第1。
- Custom的Morph从自定义矩阵走向当前所选内置算法。
- 精细边深度可在All params的 `fm.edge.*` 中修改，同样自动准备Custom。

高编号到低编号算子可用当前内部子采样值，其余及递归用前一个4×内部子采样。这个FM调制图与效果图的一采样反馈是两个独立系统。

| 控制 | 含义 |
|---|---|
| Modulation index | 整体算子间调制深度 |
| Feedback | 各算子受控递归反馈 |
| Attack/Decay/Sustain/Release | 全局ADSR |
| Voice level / Stereo spread | 合成增益和算子立体声分布 |
| Frequency ratio | 相对音符基频倍数，非整数可产生非谐波关系 |
| Level | 算子本身电平，同时影响调制输出 |
| To output | 输出权重；内置模式乘以算法的载波掩码 |
| Fine tune | cents微调 |
| Attack scale / Release scale | 此算子对全局时长的倍率 |

MIDI支持16声部、力度、按通道弯音（宿主约±2半音）、CC64、按通道释放和最旧声部替换。Live voices为实际声部数，Panic清理声源和效果历史。

FM内部4×处理和输出滤波，超高频算子衰减。极端指数/反馈仍可能混叠。Gain/Index/Feedback约5ms平滑；Morph、比例、拓扑和其他算子变化仍可能即时跳变，应避免突变或先淡出。

## 5. Source：Phase Geometry 与 Chaos 声源

第四个工作区 `04  Source`。Source 层是与六算子FM并列的多声部声源（16声部、固定数组、无运行时分配），输出进入同一个 Atlas 图。本节的参数名、范围和默认值读自 `Source/Parameters.cpp`，界面文字读自 `Source/PluginEditor.cpp`，处理链描述来自 `libs/aether-source/README.md`；各参数的精确听感没有在此逐项验证，请以试听为准。

### 引擎与可用条件

Engine 菜单：**Off**（默认）/ **Phase Geometry** / **Chaos**。选 Off 时右侧显示“No engine selected”，此时 Source 不参与声音；选引擎后只显示该引擎的参数。切换引擎会同时复位两个引擎，声音不会在引擎之间泄漏。

| 版本 | 何时发声 | 界面状态文字 |
|---|---|---|
| AETHER Micro（效果器） | Mode = Resynth，Engine 不为 Off，Source on 打开 | `Active: <引擎> follows the input audio in Resynth mode.` |
| AETHER Micro Synth（乐器） | Engine 不为 Off，Source on 打开（Synth 模式由插件固定） | `Active: <引擎> sounds on MIDI notes in Synth mode.` |

AETHER Micro 处于 FX 模式时，Source 页显示 `Idle: Mode is FX. Switch Mode to Resynth in the footer to run this layer.`；Source on 关闭时显示 `Switched off. Turn Source on to hear <引擎>.`。

### Resynth 驱动的 Source（AETHER Micro）

在 Resynth 模式下，Source 声部由输入音频驱动：

- 音高（包含 Transpose）跟随检测到的输入音高，幅度跟随输入包络。
- 输入失去有效音高时，保持最后一次的音高，并以 30 ms 淡出；音高恢复时 4 ms 淡入，无爆音。
- Engine 为 Off 时输出与没有 Source 完全一致（逐位相同）。
- MIDI 音符在 Resynth 下仍然会触发 Source 声部。

### 公共控制（两个引擎共用）

| 控制 | 范围 | 默认 |
|---|---|---|
| Engine | Off / Phase Geometry / Chaos | Off |
| Source on | 开/关 | 开 |
| Attack | 0.001–5 s | 0.005 s |
| Decay | 0.01–5 s | 0.3 s |
| Sustain | 0–100% | 70% |
| Release | 0.01–12 s | 0.3 s |
| Level | 0–100% | 65% |
| Stereo spread | 0–100% | 25% |
| Transpose | −24…+24 半音 | 0 |
| Anti-alias 4x | 开/关 | 开 |
| Seed | 0–65535 | 0 |

所有 Source 参数都可自动化。Seed 只在音符开始时读取，已按住的声部保持原来的种子；Anti-alias 在下一个块边界生效，切换时重新调谐运行中的声部并清空抽取滤波器，会产生一个短暂瞬变（README 记录的测量值为同量级的波形台阶，不是超出波形本身的爆音）。Anti-alias 打开时每个声部以4倍采样率运行再抽取。

### Phase Geometry 引擎（10个参数）

处理链：斜坡 → 相位乘数 → VPS 折点映射 → CZ 风格相位失真（含共振窗）→ 余弦/Walsh 表。界面三列：`WALSH / PHASE`、`CASIO CZ`、`VARIABLE PHASE SHAPING`。

| 参数 | 范围 | 默认 |
|---|---|---|
| Walsh sequency | 0–63 | 0 |
| Walsh mix | 0–100% | 0 |
| CZ shape | Saw / Square / Pulse / Resonant saw / Resonant triangle / Resonant trapezoid | Saw |
| CZ depth | 0–100% | 50% |
| CZ envelope | 0–100% | 0 |
| CZ velocity | 0–100% | 0 |
| VPS breakpoint D | 0.1–99.9% | 50% |
| VPS breakpoint V | 0–100% | 50% |
| VPS ratio | 1–16 x | 1 x |
| Phase random | 0–100% | 0 |

Walsh 阶跃用一阶 ADAA 做带限；最低分音超过 Nyquist 的序列度无法表示，会被低通而不是折叠回来。

### Chaos 引擎（7个参数）

GENDYN 式随机波形：折点的幅度和时长按随机游走变化，带镜像边界，每个周期生成一个多边形。界面三列：`SEGMENTS`、`STEP SIZES`、`MEMORY / LIMITS`。

| 参数 | 范围 | 默认 |
|---|---|---|
| Segments | 2–32 | 12 |
| Distribution | Cauchy / Logistic / Hyperbolic cosine / Arcsine / Exponential / Uniform | Cauchy |
| Amplitude step | 0–100% | 15% |
| Duration step | 0–100% | 10% |
| Duration range | 0–100% | 50% |
| Momentum | 0–95% | 0 |
| Amplitude barrier | 5–100% | 100% |

随机数是确定性的（PCG32，种子来自 Seed 和声部年龄），输出不依赖宿主块大小。
## 6. Motion：来源与目的地

四LFO提供Sine/Triangle/Saw/Pulse/Random。未同步Rate为Hz；Beat sync时为每拍循环数，随BPM变化，无播放头信息时120BPM。Phase偏移周期。曲线是形状预览，活动点来自实际引擎。

16条分配各设Source、Destination、Depth：

- LFO1–4、输入包络、MIDI力度、音高、随机保持、Chaos；None不调制。
- 每槽四宏/Mix/Level/Pan，共12×7=84目的地。
- 深度正负均可，多行可指向同一目的地。
- 固定16采样控制节奏，不按宿主块切分重新开始。
- 不覆盖宿主基线；关闭分配即回基线。

起点：给Ladder的截止宏分配LFO1深度0.2；给Grain速度分配LFO2并调慢。Freeze也可被调制，可能周期捕获并产生无限尾音。

### 新增调制目的地（2.3.0 草案 · 待实现确认）

> **状态：草案。** 以下内容来自声源工程师的方案（分支 `feat/mod-targets-230`，基线 991e493），**尚未在本文档所在分支实现，也没有对照实现验证**。名称、编号和语义以该分支落地后的代码为准；落地前请勿把它当作已发布功能。2.2.0 实际可用的仍是上面的 12×7 = 84 个目的地。

**编号规则（草案）。** `modulation.N.target` 是选项参数：0 = None，1…84 = 12个节点 × 7项（Macro A–D / Mix / Level / Pan），共16行分配。草案只在末尾追加28项，编号 85…112，选项总数从 85 变为 113；已有编号和参数ID不变。

| 编号 | 分组 | 被调制的现有参数（参数ID／界面名称） |
|---|---|---|
| 85 | Source | `source.gain` / Level |
| 86 | Source | `source.spread` / Stereo spread |
| 87 | Source | `source.transpose` / Transpose |
| 88 | Source（Phase Geometry） | `source.pg.sequency` / Walsh sequency |
| 89 | Source（Phase Geometry） | `source.pg.walshmix` / Walsh mix |
| 90 | Source（Phase Geometry） | `source.pg.czdepth` / CZ depth |
| 91 | Source（Phase Geometry） | `source.pg.czenvelope` / CZ envelope |
| 92 | Source（Phase Geometry） | `source.pg.czvelocity` / CZ velocity |
| 93 | Source（Phase Geometry） | `source.pg.vpsd` / VPS breakpoint D |
| 94 | Source（Phase Geometry） | `source.pg.vpsv` / VPS breakpoint V |
| 95 | Source（Phase Geometry） | `source.pg.vpsratio` / VPS ratio |
| 96 | Source（Phase Geometry） | `source.pg.phaserandom` / Phase random |
| 97 | Source（Chaos） | `source.chaos.ampstep` / Amplitude step |
| 98 | Source（Chaos） | `source.chaos.durstep` / Duration step |
| 99 | Source（Chaos） | `source.chaos.durrange` / Duration range |
| 100 | Source（Chaos） | `source.chaos.momentum` / Momentum |
| 101 | Source（Chaos） | `source.chaos.ampbarrier` / Amplitude barrier |
| 102–106 | FM | `fm.index`、`fm.morph`、`fm.feedback`、`fm.gain`、`fm.spread`（调制指数、算法 Morph、反馈、声部电平、立体声分布，依次对应 102、103、104、105、106） |
| 107–112 | FM | `fm.op.1.level` … `fm.op.6.level`（算子 1…6 的 Level） |

**名称（待定）。** 目的地在 Motion 页下拉里的最终名称由声源工程师在 `modulationTargetNames()` 中确定，约定格式为：85–101 以 `Source / ` 开头，102–112 以 `FM / ` 开头，总长不超过 24 个字符；1–84 的名称（`Node N / Macro A…`）保持不变。最终名称出来之前，本文不列出具体名称。

“被调制的现有参数”一列是测试代码里为每个编号假定的目标参数（来自方案的编号表，需与实现核对）；该列里的名称是现有参数的界面名称，不是目的地名称。

**语义（草案）：**

- Depth = 1 表示整个旋钮行程，在参数的归一化（含 skew）域里相加；Depth 为负则反向。
- 基线仍是宿主里的参数值。调制只作用于内部副本，**不会写回**宿主参数或保存的状态；关闭分配即回到基线。
- 结果钳制在该参数自身的范围内。
- 调制每16个采样更新一次（与现有84个目的地相同）。
- 目的地不属于当前引擎时静音且不报错：例如引擎为 Chaos 时，指向 Phase Geometry 参数（88–96）的分配不起作用，反之亦然。

**待确认（本文没有验证）：**

- 最终名称（见上）和各编号对应的参数。分组边界（1–84 节点、85–101 Source、102–112 FM）与选项总数 113 是已商定的草案。
- 新目的地是否同样要求 Atlas graph 为开：现有的调制矩阵只在 `graph.enabled` 打开时运行，测试里按这个前提构造场景；若 Source/FM 目的地要求在“Atlas graph 关闭”时也能调制，需要实现方确认。
- 宿主自动化：`modulation.N.target` 的选项总数从 85 变为 113，按“下标/(总数−1)”归一化的宿主自动化值会随之变化；保存的状态里存的是选项下标，所以旧工程里 ≤84 的目的地读回后下标不变（由测试覆盖，需实现落地后转绿）。
## 7. 全局、旧工程和预设

全局Input/Output/Dry/Wet控制输入/最终输出/与原音混合；Master仍在最后，宽度、Ceiling、Release、Dither在All params。Mix=0仍经过Output和Master；完全透明比较用Bypass。

All params可滚动访问全部467宿主参数。旧Spectral/Modal/Colour/Space参数只用于legacy声音路径；Atlas节点用 `node.*` 四宏。原12个工厂声音保留为legacy，新增24个Atlas/FM/Motion声音启用新引擎。

内置算法生效时，All params中的矩阵/send数值表示保存的Custom状态，主画布与FM图显示当前实际算法。开始手工修改时先复制当前有效算法，再进入Custom；不要将尚未启用的Custom存储值误认为当前听到的连接。

schema-1工程/文件自动关闭Atlas graph与Six operator engine，使用v1声音路径。schema-2保存节点、两个矩阵、调制、FM和A/B。A/B及预设切换清理历史避免旧尾音污染比较。

Save/Open使用JSON `.aether`，拒绝非法产品/schema/非有限值/超大文件。完整保存包含全部参数；手工部分文件只覆盖提供的值。双击旋钮回默认，可编辑数值，悬停看说明。

## 8. 延迟、负载与限制

**TEXTURE为已启用节点最大的单模块固有延迟，PDC为宿主补偿。** 默认48kHz的Prism/Spectral Freeze实测2560采样（约53.3ms）；Frequency Shift为63采样。当前创作图不做全图路径对齐，PDC=0，串联多个频谱节点总延迟可大于TEXTURE；干湿/并联存在相位和时间差，需要主动选择。

冻结和循环可长期持续，插件保守报告无限尾音；可能触发Freeze的调制也计算在内。普通FM报告包含最长释放再留有效果尾音余量。离线导出无限尾音时自行指定长度或先释放/Panic。

16声部FM较重，12个昂贵效果还需额外CPU。本机一组12节点混合图约占单核实时预算10%–17%；16声部FM单独约50%–60%，负载/电源/调度都会改变结果。以DAW真实负载和掉音情况为准；UI没有测量时隐藏CPU数据。

类型/路由/材质/延迟尺寸/捕获切换可能有瞬变，未承诺所有参数无爆音。没有训练式DDSP、NAM模型、CLAP/MIDI2、多进程网络、真峰值/LUFS保证或非Windows二进制。实际证据和剩余风险见VALIDATION.md。

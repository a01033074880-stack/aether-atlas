# AETHER Atlas 2.0.0 使用说明

Atlas 把第一版五级串联扩展为：12 个可重复模块槽、32 种效果路由、六算子/32算法 FM、16 条调制分配和36个预设。三个主要工作区为 **Patch 接线、FM 声源、Motion 调制**。

## 1. 安装与插件选择

解压发布包运行 `install.ps1`，或把 `VST3` 内两个完整 `.vst3` 目录复制到 `%LOCALAPPDATA%\Programs\Common\VST3`。关闭 DAW 后再升级，脚本保留旧目录备份。重新扫描插件；宿主未扫描该位置时手动添加搜索路径。

| 插件 | 放置位置 | 用途 |
|---|---|---|
| AETHER Micro | 音频/效果轨 | FX、侧链激励、音频 Resynth |
| AETHER Micro Synth | MIDI/乐器轨 | 16声部六算子FM，没有主音频输入 |

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

## 5. Motion：来源与目的地

四LFO提供Sine/Triangle/Saw/Pulse/Random。未同步Rate为Hz；Beat sync时为每拍循环数，随BPM变化，无播放头信息时120BPM。Phase偏移周期。曲线是形状预览，活动点来自实际引擎。

16条分配各设Source、Destination、Depth：

- LFO1–4、输入包络、MIDI力度、音高、随机保持、Chaos；None不调制。
- 每槽四宏/Mix/Level/Pan，共12×7=84目的地。
- 深度正负均可，多行可指向同一目的地。
- 固定16采样控制节奏，不按宿主块切分重新开始。
- 不覆盖宿主基线；关闭分配即回基线。

起点：给Ladder的截止宏分配LFO1深度0.2；给Grain速度分配LFO2并调慢。Freeze也可被调制，可能周期捕获并产生无限尾音。

## 6. 全局、旧工程和预设

全局Input/Output/Dry/Wet控制输入/最终输出/与原音混合；Master仍在最后，宽度、Ceiling、Release、Dither在All params。Mix=0仍经过Output和Master；完全透明比较用Bypass。

All params可滚动访问全部467宿主参数。旧Spectral/Modal/Colour/Space参数只用于legacy声音路径；Atlas节点用 `node.*` 四宏。原12个工厂声音保留为legacy，新增24个Atlas/FM/Motion声音启用新引擎。

内置算法生效时，All params中的矩阵/send数值表示保存的Custom状态，主画布与FM图显示当前实际算法。开始手工修改时先复制当前有效算法，再进入Custom；不要将尚未启用的Custom存储值误认为当前听到的连接。

schema-1工程/文件自动关闭Atlas graph与Six operator engine，使用v1声音路径。schema-2保存节点、两个矩阵、调制、FM和A/B。A/B及预设切换清理历史避免旧尾音污染比较。

Save/Open使用JSON `.aether`，拒绝非法产品/schema/非有限值/超大文件。完整保存包含全部参数；手工部分文件只覆盖提供的值。双击旋钮回默认，可编辑数值，悬停看说明。

## 7. 延迟、负载与限制

**TEXTURE为已启用节点最大的单模块固有延迟，PDC为宿主补偿。** 默认48kHz的Prism/Spectral Freeze实测2560采样（约53.3ms）；Frequency Shift为63采样。当前创作图不做全图路径对齐，PDC=0，串联多个频谱节点总延迟可大于TEXTURE；干湿/并联存在相位和时间差，需要主动选择。

冻结和循环可长期持续，插件保守报告无限尾音；可能触发Freeze的调制也计算在内。普通FM报告包含最长释放再留有效果尾音余量。离线导出无限尾音时自行指定长度或先释放/Panic。

16声部FM较重，12个昂贵效果还需额外CPU。本机一组12节点混合图约占单核实时预算10%–17%；16声部FM单独约50%–60%，负载/电源/调度都会改变结果。以DAW真实负载和掉音情况为准；UI没有测量时隐藏CPU数据。

类型/路由/材质/延迟尺寸/捕获切换可能有瞬变，未承诺所有参数无爆音。没有训练式DDSP、NAM模型、CLAP/MIDI2、多进程网络、真峰值/LUFS保证或非Windows二进制。实际证据和剩余风险见VALIDATION.md。

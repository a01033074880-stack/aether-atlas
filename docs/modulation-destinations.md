# Modulation destinations

`modulation.N.target` (N = 1..16) is a choice parameter. Its option list only ever grows at the end; the stored value is the
option index.

| Option | Meaning |
| --- | --- |
| 0 | None |
| 1..84 | Node 1..12 x 7 (Macro A, B, C, D, Mix, Level, Pan). Unchanged since 2.0. |
| 85..112 | Source and FM parameters (2.3.0), table below. 113 options in total. |

**The motion matrix lives in the graph engine, so rows only run while `graph.enabled` is on.** This holds for the new
destinations exactly as for the old ones: with the graph off, no row (old or new) has any effect.

## 85..112

| # | Name in the menu | Parameter | Engine | Applies when |
| --- | --- | --- | --- | --- |
| 85 | Source / Level | `source.gain` | Generic (any Source engine) | a Source engine is on |
| 86 | Source / Spread | `source.spread` | Generic | a Source engine is on |
| 87 | Source / Transpose | `source.transpose` | Generic | a Source engine is on |
| 88 | Source / Walsh seq | `source.pg.sequency` | Phase Geometry | engine = Phase Geometry |
| 89 | Source / Walsh mix | `source.pg.walshmix` | Phase Geometry | engine = Phase Geometry |
| 90 | Source / CZ depth | `source.pg.czdepth` | Phase Geometry | engine = Phase Geometry |
| 91 | Source / CZ env | `source.pg.czenvelope` | Phase Geometry | engine = Phase Geometry |
| 92 | Source / CZ velocity | `source.pg.czvelocity` | Phase Geometry | engine = Phase Geometry |
| 93 | Source / VPS D | `source.pg.vpsd` | Phase Geometry | engine = Phase Geometry |
| 94 | Source / VPS V | `source.pg.vpsv` | Phase Geometry | engine = Phase Geometry |
| 95 | Source / VPS ratio | `source.pg.vpsratio` | Phase Geometry | engine = Phase Geometry |
| 96 | Source / Phase random | `source.pg.phaserandom` | Phase Geometry | engine = Phase Geometry |
| 97 | Source / Amp step | `source.chaos.ampstep` | Chaos | engine = Chaos |
| 98 | Source / Dur step | `source.chaos.durstep` | Chaos | engine = Chaos |
| 99 | Source / Dur range | `source.chaos.durrange` | Chaos | engine = Chaos |
| 100 | Source / Momentum | `source.chaos.momentum` | Chaos | engine = Chaos |
| 101 | Source / Amp barrier | `source.chaos.ampbarrier` | Chaos | engine = Chaos |
| 102 | FM / Mod index | `fm.index` | FM | FM on |
| 103 | FM / Morph | `fm.morph` | FM | FM on |
| 104 | FM / Feedback | `fm.feedback` | FM | FM on |
| 105 | FM / Voice level | `fm.gain` | FM | FM on |
| 106 | FM / Spread | `fm.spread` | FM | FM on |
| 107..112 | FM / Op 1 level .. FM / Op 6 level | `fm.op.1.level` .. `fm.op.6.level` | FM | FM on |

`source.chaos.segments` is not a destination (it is a stepped, structural parameter).
"Source engine on" means `source.enabled` with `source.engine` > 0; "FM on" means `fm.enabled`. A destination whose engine is
not running changes nothing (the render is bit-identical to the unmodulated one). The menu names all start with `Source / ` or
`FM / ` and are at most 24 characters long.

## Semantics

* **Depth 1 is the full knob travel.** Each row contributes `amount = source * depth`, with the source value as the graph
  produces it and depth in -1..1. Rows that point at the same destination are summed first.
* The sum is applied in the destination parameter's own normalised range (so skewed ranges such as `source.pg.vpsratio` move
  by the same fraction of the knob everywhere on it):
  `modulated = convertFrom0to1(clamp(convertTo0to1(host value) + amount, 0, 1))`. The result always stays inside the parameter
  range. Destinations with an amount of exactly 0 keep the host value bit for bit.
* **The baseline is the host parameter value.** Nothing is written back; the host sees and records only its own automation.
  Every block starts again from the host values.
* **Rate and latency.** The graph updates its modulation sources every 16 samples. The modulated Source/FM parameters change
  at those 16-sample boundaries and use the offsets computed at the previous boundary, i.e. the destinations lag the sources
  by at most one 16-sample control period (0.33 ms at 48 kHz). The lag is deterministic and independent of the host block size
  and of MIDI timing.
* Non-finite sources or depths contribute nothing. Out-of-range option indices are clamped to 112.
* The new path is only active while the graph is on, the mode is not FX, something consumes the destinations (FM on or a
  Source engine on) and at least one row has source != None, target > 84 and depth != 0. Otherwise the previous code path runs
  and the output is bit-identical to 2.2.0.
* Tail length is unaffected by destinations 85..112.

## Saved state

Both the state XML and `.json` presets store the option index as a number.

* **Write.** A save that has any motion row with `modulation.N.target` > 84 (live tree, A/B snapshot, or preset JSON) is
  stamped `schema` 4 (`aether::stateSchemaDestinations`). Every other save stays `schema` 3 (`aether::stateSchema`), so
  old content is byte-identical to 2.2.0. The nested A/B snapshot XML carries the same rule on its own tree.
* **Read.** This build accepts schemas 1..4 (`aether::stateSchemaNewest` = 4) and refuses 0 and 5+. Schema 1/2 still get
  the source-layer defaults they always did.
* **2.2.0.** A 2.2.0 build (schema newest = 3) refuses schema-4 state (`setStateInformation` returns without applying) and
  schema-4 presets (`loadPreset` fails with "Unsupported preset product or schema."). Verified by running a SchemaProbe
  against the 991e493 sources. If a 2.2.0 build somehow saw target 85..112 under schema 3, the loaders would clamp the
  index to 84 (`Node 12 / Pan`); schema 4 is the guard that prevents that silent substitution for new destinations.
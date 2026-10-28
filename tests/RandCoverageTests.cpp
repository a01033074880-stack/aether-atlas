// AETHER 2.3.0 RandSpec P1: checks that the committed static tables cover parameterSpecs() exactly (JUCE side).
// Usage: AetherRandCoverage <docs/randomization-spec-params.tsv>
#include "Parameters.h"
#include "aether/Rand.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace aether::rand;

static int g_checks = 0, g_failures = 0;
#define CHECK_MSG(cond, ...)                                                                             \
    do {                                                                                                 \
        ++g_checks;                                                                                      \
        if (!(cond)) { ++g_failures; std::fprintf(stderr, "FAIL %s:%d  %s | ", __FILE__, __LINE__, #cond); std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } \
    } while (0)

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else if (c != '\r') cur.push_back(c); }
    out.push_back(cur);
    return out;
}

int main(int argc, char** argv) {
    const auto& specs = aether::parameterSpecs();
    const auto table = paramTable();
    CHECK_MSG(specs.size() == static_cast<std::size_t>(kParamCount), "parameterSpecs() has %zu entries", specs.size());
    CHECK_MSG(table.size() == specs.size(), "table size");
    int missing = 0;
    for (std::size_t i = 0; i < specs.size() && i < table.size(); ++i) {
        const auto& s = specs[i];
        const auto& t = table[i];
        const std::string id = s.id.toRawUTF8();
        CHECK_MSG(id == t.id, "index %zu: spec '%s' vs table '%s'", i, id.c_str(), t.id);
        if (id != t.id) { ++missing; continue; }
        const ParamType type = s.boolean ? ParamType::Bool : (!s.choices.isEmpty() ? ParamType::Choice : ParamType::Float);
        CHECK_MSG(type == t.type, "%s type", id.c_str());
        CHECK_MSG(static_cast<float>(t.minV) == s.minimum && static_cast<float>(t.maxV) == s.maximum, "%s range %g..%g vs table %g..%g", id.c_str(), static_cast<double>(s.minimum), static_cast<double>(s.maximum), t.minV, t.maxV);
        CHECK_MSG(static_cast<float>(t.defaultV) == s.defaultValue, "%s default", id.c_str());
        CHECK_MSG(static_cast<float>(t.step) == s.step, "%s step", id.c_str());
        CHECK_MSG(t.numChoices == static_cast<int>(s.choices.size()), "%s choices", id.c_str());
        CHECK_MSG(paramIndex(id) == static_cast<int>(i), "%s paramIndex", id.c_str());
    }
    CHECK_MSG(missing == 0, "%d ids differ", missing);
    // layout constants used by the engine
    auto idAt = [&](int i) { return std::string(specs[static_cast<std::size_t>(i)].id.toRawUTF8()); };
    CHECK_MSG(idAt(kGraphEnabled) == "graph.enabled" && idAt(kRoutingAlgorithm) == "routing.algorithm" && idAt(kGraphFeedback) == "graph.feedback" && idAt(kGraphTrim) == "graph.trim", "graph layout");
    CHECK_MSG(idAt(nodeParam(0, 0)) == "node.1.type" && idAt(nodeParam(11, 10)) == "node.12.output" && idAt(nodeParam(3, 8)) == "node.4.d" && idAt(nodeParam(5, 9)) == "node.6.input", "node layout");
    CHECK_MSG(idAt(lfoParam(0, 0)) == "lfo.1.rate" && idAt(lfoParam(3, 3)) == "lfo.4.sync", "lfo layout");
    CHECK_MSG(idAt(modParam(0, 0)) == "modulation.1.source" && idAt(modParam(15, 2)) == "modulation.16.depth", "mod layout");
    CHECK_MSG(idAt(kFmEnabled) == "fm.enabled" && idAt(kFmAlgorithm) == "fm.algorithm" && idAt(kFmCustom) == "fm.custom" && idAt(kFmIndex) == "fm.index" && idAt(kFmMorph) == "fm.morph" && idAt(kFmFeedback) == "fm.feedback", "fm layout");
    CHECK_MSG(idAt(opParam(0, 0)) == "fm.op.1.ratio" && idAt(opParam(5, 5)) == "fm.op.6.release" && idAt(opParam(2, 2)) == "fm.op.3.carrier", "op layout");
    CHECK_MSG(idAt(kSourceEngine) == "source.engine" && idAt(kSourceEnabled) == "source.enabled" && idAt(kParamCount - 1) == "source.chaos.ampbarrier", "source layout");
    CHECK_MSG(idAt(kRouteOffset).rfind("route.", 0) == 0 && idAt(kEdgeOffset).rfind("fm.edge", 0) == 0, "route/edge layout");

    // the spec TSV: every row has a table entry with the same type/classification/group/role, in the same order
    if (argc > 1) {
        std::ifstream in(argv[1], std::ios::binary);
        CHECK_MSG(static_cast<bool>(in), "cannot open %s", argv[1]);
        std::string line;
        std::getline(in, line);
        const auto header = split(line, '\t');
        CHECK_MSG(header.size() > 5 && header[0] == "id" && header[3] == "classification", "unexpected TSV header");
        int rows = 0, cls[3] = {0, 0, 0}, neverUnchanged = 0;
        while (std::getline(in, line)) {
            if (line.empty() || line == "\r") continue;
            const auto c = split(line, '\t');
            if (c.size() < 16) { CHECK_MSG(false, "short TSV row %d", rows); continue; }
            CHECK_MSG(rows < kParamCount, "more TSV rows than parameters");
            if (rows >= kParamCount) break;
            const auto& t = table[static_cast<std::size_t>(rows)];
            CHECK_MSG(c[0] == t.id, "TSV row %d '%s' vs table '%s'", rows, c[0].c_str(), t.id);
            const char* cn = t.cls == Classification::Random ? "Random" : (t.cls == Classification::Structural ? "Structural" : "Never");
            CHECK_MSG(c[3] == cn, "%s classification TSV=%s table=%s", c[0].c_str(), c[3].c_str(), cn);
            const char* tn = t.type == ParamType::Float ? "float" : (t.type == ParamType::Bool ? "bool" : "choice");
            CHECK_MSG(c[1] == tn, "%s type", c[0].c_str());
            ++cls[static_cast<int>(t.cls)];
            ++rows;
        }
        CHECK_MSG(rows == kParamCount, "TSV rows %d", rows);
        CHECK_MSG(cls[0] == 175 && cls[1] == 273 && cls[2] == 47, "TSV classes %d/%d/%d", cls[0], cls[1], cls[2]);

        // run the engine on the real default state of each edition and check against the real ranges
        for (int mode = 0; mode < 3; ++mode) {
            StateView st;
            for (std::size_t i = 0; i < specs.size(); ++i) st.value[i] = specs[i].defaultValue;
            st.edition.isSynth = mode == 2;
            st.edition.effectiveMode = mode == 2 ? 0 : mode;
            st.value[kGraphEnabled] = 1.f;
            if (mode >= 1) { st.value[kSourceEnabled] = 1.f; st.value[kSourceEngine] = 1.f; }
            for (std::uint64_t seed = 1; seed <= 200; ++seed)
                for (Safety sf : {Safety::Safe, Safety::Normal, Safety::Wild}) {
                    Config cfg; cfg.seed = seed; cfg.safety = sf;
                    const Patch p = generate(st, cfg, LockSet{});
                    CHECK_MSG(p.report.ok, "generate not ok: %s", p.report.reason);
                    for (const auto& d : p.diffs) {
                        const auto& s = specs[static_cast<std::size_t>(d.specIndex)];
                        CHECK_MSG(d.value >= s.minimum && d.value <= s.maximum && std::isfinite(d.value), "%s out of real range", s.id.toRawUTF8());
                        if (table[static_cast<std::size_t>(d.specIndex)].cls == Classification::Never) ++neverUnchanged;
                    }
                }
        }
        CHECK_MSG(neverUnchanged == 0, "Never parameters were written %d times", neverUnchanged);
    } else {
        CHECK_MSG(false, "usage: AetherRandCoverage <randomization-spec-params.tsv>");
    }
    std::printf("aether_rand_coverage: %d checks, %d failures (%zu parameters)\n", g_checks, g_failures, specs.size());
    return g_failures == 0 ? 0 : 1;
}

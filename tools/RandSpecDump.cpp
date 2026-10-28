// Exports the parameter ranges of parameterSpecs() (the single source of truth) as a TSV.
// docs/randomization-spec-ranges.tsv is generated with it and consumed by tools/gen_rand_table.py.
// The aether_rand_coverage test re-checks the committed table against parameterSpecs() on every run.
// Usage: AetherRandSpecDump <output.tsv>
#include "Parameters.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: AetherRandSpecDump <output.tsv>\n"); return 2; }
    std::FILE* f = std::fopen(argv[1], "wb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    std::fprintf(f, "index\tid\ttype\tmin\tmax\tstep\tdefault\tnum_choices\n");
    const auto& specs = aether::parameterSpecs();
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const auto& s = specs[i];
        const char* type = s.boolean ? "bool" : (!s.choices.isEmpty() ? "choice" : "float");
        std::fprintf(f, "%zu\t%s\t%s\t%.9g\t%.9g\t%.9g\t%.9g\t%d\n", i, s.id.toRawUTF8(), type,
                     static_cast<double>(s.minimum), static_cast<double>(s.maximum), static_cast<double>(s.step),
                     static_cast<double>(s.defaultValue), static_cast<int>(s.choices.size()));
    }
    std::fclose(f);
    std::printf("wrote %zu parameters to %s\n", specs.size(), argv[1]);
    return 0;
}

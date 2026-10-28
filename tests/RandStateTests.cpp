// AETHER 2.3.0 randomization wiring: tests for the JUCE-free state helpers (aether/RandState.h):
// SnapshotRing, hashValues, seedToBase32, randomPresetName, patchIsStructural, serializeLocks/deserializeLocks.
#include "aether/RandState.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

using namespace aether::rand;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond)                                                                                      \
    do {                                                                                                 \
        ++g_checks;                                                                                      \
        if (!(cond)) { ++g_failures; std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

namespace {

RandomSnapshot snap(int tag) {
    RandomSnapshot s;
    s.face = (tag & 1) ? Face::B : Face::A;
    s.values.fill(static_cast<float>(tag));
    s.values[3] = static_cast<float>(tag) * 0.5f;
    s.presetName = "name" + std::to_string(tag);
    s.program = tag;
    s.nameChanged = (tag % 3) == 0;
    s.recipe.valid = true;
    s.recipe.seed = static_cast<std::uint64_t>(tag);
    return s;
}

void testRing() {
    SnapshotRing r;
    CHECK(SnapshotRing::capacity() == 16 && SnapshotRing::kCapacity == kUndoDepth);
    CHECK(!r.canUndo() && r.size() == 0 && r.peek() == nullptr);
    RandomSnapshot out;
    CHECK(!r.pop(out));
    // undo order is LIFO
    for (int i = 1; i <= 5; ++i) r.push(snap(i));
    CHECK(r.size() == 5 && r.canUndo());
    CHECK(r.peek() != nullptr && r.peek()->program == 5);
    for (int i = 5; i >= 1; --i) {
        CHECK(r.pop(out));
        CHECK(out.program == i && out.presetName == "name" + std::to_string(i) && out.values[0] == static_cast<float>(i) && out.values[3] == static_cast<float>(i) * 0.5f);
        CHECK(out.face == ((i & 1) ? Face::B : Face::A) && out.nameChanged == ((i % 3) == 0) && out.recipe.seed == static_cast<std::uint64_t>(i));
    }
    CHECK(!r.canUndo() && r.size() == 0 && !r.pop(out));
    // the 17th push overwrites the oldest; the newest 16 survive, in order
    for (int i = 1; i <= 17; ++i) r.push(snap(i));
    CHECK(r.size() == 16);
    for (int i = 17; i >= 2; --i) { CHECK(r.pop(out)); CHECK(out.program == i); }
    CHECK(!r.canUndo() && !r.pop(out));  // entry 1 was overwritten
    // many wraps (head index wraps repeatedly)
    for (int i = 1; i <= 100; ++i) r.push(snap(i));
    CHECK(r.size() == 16 && r.peek()->program == 100);
    for (int i = 100; i > 84; --i) { CHECK(r.pop(out)); CHECK(out.program == i); }
    CHECK(!r.canUndo());
    // push / pop interleaved across the wrap point
    for (int i = 1; i <= 20; ++i) r.push(snap(i));
    for (int i = 0; i < 3; ++i) CHECK(r.pop(out));
    r.push(snap(99));
    CHECK(r.size() == 14 && r.peek()->program == 99);
    CHECK(r.pop(out) && out.program == 99 && r.pop(out) && out.program == 17);
    // clear
    r.clear();
    CHECK(r.size() == 0 && !r.canUndo() && r.peek() == nullptr && !r.pop(out));
    r.push(snap(7));
    CHECK(r.size() == 1 && r.pop(out) && out.program == 7);
}

void testHash() {
    std::array<float, kParamCount> a{};
    const std::uint64_t h0 = hashValues(a);
    CHECK(hashValues(a) == h0);                       // stable
    CHECK(h0 == 0x31aca7b1de162255ULL);               // pinned: 495 x +0.0f (FNV-1a-64 over 1980 zero bytes, cross-checked with an independent Python computation)
    std::array<float, kParamCount> b = a;
    b[200] = 1.f;
    CHECK(hashValues(b) != h0);
    b = a;
    b[494] = std::numeric_limits<float>::denorm_min();
    CHECK(hashValues(b) != h0);
    b = a;
    b[0] = -0.f;                                       // bit pattern differs from +0
    CHECK(hashValues(b) != h0);
    // empty-input property of FNV: a single changed bit in the last element still changes the hash
    std::array<float, kParamCount> c = a;
    c[kParamCount - 1] = 1.f;
    std::array<float, kParamCount> d = a;
    d[kParamCount - 2] = 1.f;
    CHECK(hashValues(c) != hashValues(d));
}

void testSeed() {
    CHECK(seedToBase32(0) == "00000000");
    CHECK(seedToBase32(1) == "00000001");
    CHECK(seedToBase32(31) == "0000000Z");
    CHECK(seedToBase32(32) == "00000010");
    CHECK(seedToBase32((1ULL << 40) - 1) == "ZZZZZZZZ");
    CHECK(seedToBase32(1ULL << 40) == "00000000");   // only 40 bits are significant
    CHECK(seedToBase32(0xFFFFFFFFFFFFFFFFULL) == "ZZZZZZZZ");
    const char* alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    for (std::uint64_t s : {0ULL, 7ULL, 12345ULL, 987654321012ULL, 0xFFFFFFFFFFULL, 0xABCDEF123ULL}) {
        const std::string t = seedToBase32(s);
        CHECK(t.size() == 8);
        for (const char ch : t) CHECK(std::strchr(alphabet, ch) != nullptr && ch != 'I' && ch != 'L' && ch != 'O' && ch != 'U');
    }
    // distinct seeds give distinct text
    CHECK(seedToBase32(123456) != seedToBase32(123457));
}

void testNames() {
    CHECK(randomPresetName("Motion / Lunar Loom", 0) == "Motion / Lunar Loom (Random) 00000000");
    CHECK(randomPresetName("", 31) == "(Random) 0000000Z");
    const std::string longName(300, 'x');
    const std::string n = randomPresetName(longName, 0xFFFFFFFFFFULL);
    CHECK(n.size() == static_cast<std::size_t>(kPresetNameMax));
    CHECK(n.size() >= 18 && n.substr(n.size() - 18) == " (Random) ZZZZZZZZ");  // the seed is never truncated
    CHECK(n.substr(0, 10) == std::string(10, 'x'));
    // exactly at the limit: no truncation
    const std::string fits(kPresetNameMax - 18, 'y');
    const std::string m = randomPresetName(fits, 5);
    CHECK(m.size() == static_cast<std::size_t>(kPresetNameMax) && m.substr(0, fits.size()) == fits);
    // one over: source loses one byte
    const std::string over(kPresetNameMax - 17, 'y');
    const std::string o = randomPresetName(over, 5);
    CHECK(o.size() == static_cast<std::size_t>(kPresetNameMax) && o.substr(0, over.size() - 1) == over.substr(0, over.size() - 1));
    // UTF-8: never split a multi-byte character (3-byte U+4E2D repeated)
    std::string cjk;
    for (int i = 0; i < 80; ++i) cjk += "\xE4\xB8\xAD";
    const std::string u = randomPresetName(cjk, 9);
    CHECK(u.size() <= static_cast<std::size_t>(kPresetNameMax));
    const std::size_t srcLen = u.size() - 18;
    CHECK(srcLen % 3 == 0 && u.substr(srcLen) == " (Random) 00000009");
}

void testStructural() {
    Patch p;
    CHECK(!patchIsStructural(p));
    auto one = [](int idx) { Patch q; q.diffs.push_back({idx, 1.f}); return patchIsStructural(q); };
    CHECK(one(kGraphEnabled) && one(kRoutingAlgorithm) && one(kFmEnabled) && one(kFmAlgorithm) && one(kSourceEnabled) && one(kSourceEngine));
    for (int s = 0; s < kSlotCount; ++s) CHECK(one(nodeParam(s, 0)));
    for (int s = 0; s < kSlotCount; ++s) for (int f = 1; f < 11; ++f) CHECK(!one(nodeParam(s, f)));  // on/mix/level/pan/a-d/in/out are values
    CHECK(!one(kGraphFeedback) && !one(kGraphTrim) && !one(kFmIndex) && !one(lfoParam(0, 0)) && !one(modParam(0, 2)) && !one(opParam(0, 0)));
    Patch mixed;
    mixed.diffs.push_back({kGraphFeedback, 0.2f});
    mixed.diffs.push_back({nodeParam(3, 0), 4.f});
    CHECK(patchIsStructural(mixed));
}

LockSet someLocks() {
    LockSet l;
    for (int i : {0, 1, 3, 4, 38, 39, 200, 333, 494}) l.params.set(static_cast<std::size_t>(i));
    l.sections = kLockSlot0 | (kLockSlot0 << 5) | kLockFm | kLockSource | (kLockLfo0 << 2) | kLockMod;
    l.modRows = 0xA5C3;
    return l;
}
bool same(const LockSet& a, const LockSet& b) { return a.params == b.params && a.sections == b.sections && a.modRows == b.modRows; }

void testLocks() {
    // empty set
    {
        LockSet e, back;
        back.sections = 77;  // must be overwritten
        const std::string t = serializeLocks(e);
        CHECK(t.size() == 2 + 8 + 1 + 4 + 1 + 124);
        CHECK(t == "1:00000000:0000:" + std::string(124, '0'));
        CHECK(deserializeLocks(t, back) && same(back, e));
    }
    // round trip, and exact bit placement: bit i -> digit i/4, LSB first
    {
        const LockSet l = someLocks();
        const std::string t = serializeLocks(l);
        LockSet back;
        CHECK(deserializeLocks(t, back) && same(back, l));
        CHECK(serializeLocks(back) == t);
        CHECK(t[16] == 'b');  // indices 0,1,3 -> 0b1011
        CHECK(t[17] == '1');  // index 4
    }
    // every single bit round-trips
    for (int i = 0; i < kParamCount; ++i) {
        LockSet l;
        l.params.set(static_cast<std::size_t>(i));
        LockSet back;
        CHECK(deserializeLocks(serializeLocks(l), back) && same(back, l));
    }
    for (int b = 0; b < 19; ++b) {
        LockSet l;
        l.sections = 1u << b;
        LockSet back;
        CHECK(deserializeLocks(serializeLocks(l), back) && back.sections == l.sections);
    }
    // upper-case hex is accepted
    {
        LockSet l = someLocks(), back;
        std::string t = serializeLocks(l);
        for (auto& ch : t) if (ch >= 'a' && ch <= 'f') ch = static_cast<char>(ch - 'a' + 'A');
        CHECK(deserializeLocks(t, back) && same(back, l));
    }
    // invalid input never touches `out`
    {
        const LockSet keep = someLocks();
        const std::string good = serializeLocks(keep);
        std::string bad[] = {
            "", "1", "1:", good.substr(0, good.size() - 1), good + "0", "2" + good.substr(1), "0" + good.substr(1),
            good.substr(0, 1) + ";" + good.substr(2),               // wrong separator
            good.substr(0, 10) + ";" + good.substr(11),
            good.substr(0, 15) + ";" + good.substr(16),
            good.substr(0, 20) + "g" + good.substr(21),             // non-hex
            good.substr(0, 3) + "x" + good.substr(4),
            good.substr(0, 12) + " " + good.substr(13),
            std::string(good.size(), 'z'),
            "1:00080000:0000:" + std::string(124, '0'),             // sections bit 19 is undefined
            "1:ffffffff:0000:" + std::string(124, '0'),
            "1:00000000:0000:" + std::string(123, '0') + "8",       // padding bit (index 495) set
            "1:00000000:0000:" + std::string(123, '0') + "f",
            good.substr(0, good.size() / 2),
        };
        for (const auto& t : bad) {
            LockSet out = keep;
            CHECK(!deserializeLocks(t, out));
            CHECK(same(out, keep));
        }
        // the last digit may only use bits 0..2 (indices 492..494)
        LockSet out;
        CHECK(deserializeLocks("1:00000000:0000:" + std::string(123, '0') + "7", out) && out.params.test(494) && out.params.test(493) && out.params.test(492));
    }
}


// ---- engine-level: EditionContext as the processor really fills it -------------------------------------------------------
// The existing engine tests (RandTests.cpp baseState) build a Synth state as {isSynth=true, effectiveMode=0}; the processor
// reports effectiveMode()==2 for a Synth edition, so the real pair is {true, 2}. generate() looks at isSynth first
// (Rand.cpp mode_ = isSynth ? 2 : effectiveMode==1 ? 1 : 0), so both must give bit-identical patches. No behaviour change.
StateView synthState(bool isSynth, int effectiveMode) {
    StateView s;
    for (int i = 0; i < kParamCount; ++i) s.value[static_cast<std::size_t>(i)] = static_cast<float>(paramTable()[static_cast<std::size_t>(i)].defaultV);
    s.value[kGraphEnabled] = 1.f;
    s.value[kSourceEnabled] = 1.f; s.value[kSourceEngine] = 1.f; s.value[kFmEnabled] = 1.f;
    s.edition.isSynth = isSynth;
    s.edition.effectiveMode = effectiveMode;
    return s;
}
bool samePatch(const Patch& a, const Patch& b) {
    if (a.diffs.size() != b.diffs.size() || a.report.ok != b.report.ok || std::strcmp(a.report.reason, b.report.reason) != 0) return false;
    for (std::size_t i = 0; i < a.diffs.size(); ++i)
        if (a.diffs[i].specIndex != b.diffs[i].specIndex || std::memcmp(&a.diffs[i].value, &b.diffs[i].value, 4) != 0) return false;
    return true;
}
void testEditionPair() {
    LockSet none;
    int nonEmpty = 0;
    for (std::uint64_t seed = 1; seed <= 60; ++seed)
        for (Safety sf : {Safety::Safe, Safety::Normal, Safety::Wild})
            for (int m = 0; m < 2; ++m) {
                Config c;
                c.seed = seed; c.safety = sf; c.mode = m == 0 ? Mode::Full : Mode::Mutate; c.amount = 0.6f;
                const Patch legacy = generate(synthState(true, 0), c, none);   // what the old tests used
                const Patch real = generate(synthState(true, 2), c, none);     // what AetherAudioProcessor produces
                CHECK(samePatch(legacy, real));
                CHECK(real.report.ok);
                if (!real.diffs.empty()) ++nonEmpty;
                // FX pair behaves as FX and never writes FM/Source (documented current behaviour of {false, 2})
                const Patch fx = generate(synthState(false, 2), c, none);
                const Patch fx0 = generate(synthState(false, 0), c, none);
                CHECK(samePatch(fx, fx0));
            }
    CHECK(nonEmpty > 300);
}

}  // namespace

int main() {
    testRing();
    testHash();
    testSeed();
    testNames();
    testStructural();
    testLocks();
    testEditionPair();
    std::printf("aether_rand_state: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

// AETHER 2.3.0 randomization: state-side pure helpers (SnapshotRing, hashing, seed/preset names, lock text format).
// JUCE-free, no globals. Spec: docs/randomization-spec.md P.2 / P.3 / P.4.
#include "aether/RandState.h"

#include <cstring>
#include <utility>

namespace aether::rand {

// ---- SnapshotRing --------------------------------------------------------------------------------------------------
void SnapshotRing::push(RandomSnapshot snapshot) {
    slots_[static_cast<std::size_t>(head_)] = std::move(snapshot);
    head_ = (head_ + 1) % kCapacity;
    if (count_ < kCapacity) ++count_;  // full: the slot just written was the oldest one
}
bool SnapshotRing::pop(RandomSnapshot& out) {
    if (count_ == 0) return false;
    head_ = (head_ + kCapacity - 1) % kCapacity;
    out = std::move(slots_[static_cast<std::size_t>(head_)]);
    slots_[static_cast<std::size_t>(head_)] = RandomSnapshot{};  // drop moved-from strings
    --count_;
    return true;
}
const RandomSnapshot* SnapshotRing::peek() const noexcept {
    if (count_ == 0) return nullptr;
    return &slots_[static_cast<std::size_t>((head_ + kCapacity - 1) % kCapacity)];
}
bool SnapshotRing::canUndo() const noexcept { return count_ > 0; }
int SnapshotRing::size() const noexcept { return count_; }
void SnapshotRing::clear() noexcept {
    for (auto& s : slots_) s = RandomSnapshot{};
    head_ = 0;
    count_ = 0;
}

// ---- hashing / names -----------------------------------------------------------------------------------------------
std::uint64_t hashValues(const std::array<float, kParamCount>& values) noexcept {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const float v : values) {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        for (int k = 0; k < 4; ++k) {
            h ^= (bits >> (8 * k)) & 0xffu;
            h *= 0x100000001b3ULL;
        }
    }
    return h;
}

std::string seedToBase32(std::uint64_t seed40) {
    static constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  // Crockford: no I L O U
    seed40 &= (1ULL << 40) - 1;
    std::string out(8, '0');
    for (int i = 7; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kAlphabet[seed40 & 31u];
        seed40 >>= 5;
    }
    return out;
}

std::string randomPresetName(std::string_view sourcePreset, std::uint64_t seed40) {
    const std::string tag = "(Random) " + seedToBase32(seed40);
    if (sourcePreset.empty()) return tag;
    const std::size_t suffix = tag.size() + 1;  // " " + tag
    std::size_t keep = sourcePreset.size();
    if (keep + suffix > static_cast<std::size_t>(kPresetNameMax))
        keep = static_cast<std::size_t>(kPresetNameMax) > suffix ? static_cast<std::size_t>(kPresetNameMax) - suffix : 0;
    // never cut a UTF-8 sequence in half: step back over continuation bytes
    while (keep > 0 && keep < sourcePreset.size() && (static_cast<unsigned char>(sourcePreset[keep]) & 0xC0u) == 0x80u) --keep;
    if (keep == 0) return tag;
    return std::string(sourcePreset.substr(0, keep)) + " " + tag;
}

// ---- structural patches --------------------------------------------------------------------------------------------
bool patchIsStructural(const Patch& patch) noexcept {
    for (const auto& d : patch.diffs) {
        const int i = d.specIndex;
        if (i == kGraphEnabled || i == kRoutingAlgorithm || i == kFmEnabled || i == kFmAlgorithm || i == kSourceEnabled || i == kSourceEngine || i == kFmCustom)
            return true;
        for (int s = 0; s < kSlotCount; ++s)
            if (i == nodeParam(s, 0)) return true;  // node type
    }
    return false;
}

// ---- LockSet text format v1 ----------------------------------------------------------------------------------------
namespace {
constexpr char kHex[] = "0123456789abcdef";
constexpr int kParamHexDigits = (kParamCount + 3) / 4;            // 124
constexpr std::uint32_t kSectionsMask = (1u << 19) - 1u;          // bits 0..18 are defined (kLockMod = 1<<18)
void putHex(std::string& out, std::uint32_t v, int digits) {
    for (int i = digits - 1; i >= 0; --i) out.push_back(kHex[(v >> (4 * i)) & 0xfu]);
}
bool getHex(std::string_view text, std::uint32_t& out) noexcept {
    std::uint32_t v = 0;
    for (const char c : text) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | static_cast<std::uint32_t>(d);
    }
    out = v;
    return true;
}
}  // namespace

std::string serializeLocks(const LockSet& locks) {
    std::string out = "1:";
    out.reserve(2 + 8 + 1 + 4 + 1 + kParamHexDigits);
    putHex(out, locks.sections, 8);
    out.push_back(':');
    putHex(out, locks.modRows, 4);
    out.push_back(':');
    for (int d = 0; d < kParamHexDigits; ++d) {
        std::uint32_t nibble = 0;
        for (int b = 0; b < 4; ++b) {
            const int idx = d * 4 + b;
            if (idx < kParamCount && locks.params.test(static_cast<std::size_t>(idx))) nibble |= 1u << b;
        }
        out.push_back(kHex[nibble]);
    }
    return out;
}

bool deserializeLocks(std::string_view text, LockSet& out) noexcept {
    // "1:" + 8 hex + ":" + 4 hex + ":" + 124 hex
    constexpr std::size_t kLen = 2 + 8 + 1 + 4 + 1 + static_cast<std::size_t>(kParamHexDigits);
    if (text.size() != kLen || text[0] != '1' || text[1] != ':' || text[10] != ':' || text[15] != ':') return false;
    std::uint32_t sections = 0, rows = 0;
    if (!getHex(text.substr(2, 8), sections) || !getHex(text.substr(11, 4), rows)) return false;
    if ((sections & ~kSectionsMask) != 0) return false;
    LockSet tmp;
    tmp.sections = sections;
    tmp.modRows = static_cast<std::uint16_t>(rows);
    const std::string_view bits = text.substr(16);
    for (int d = 0; d < kParamHexDigits; ++d) {
        std::uint32_t nibble = 0;
        if (!getHex(bits.substr(static_cast<std::size_t>(d), 1), nibble)) return false;
        for (int b = 0; b < 4; ++b) {
            if ((nibble & (1u << b)) == 0) continue;
            const int idx = d * 4 + b;
            if (idx >= kParamCount) return false;  // padding bit set
            tmp.params.set(static_cast<std::size_t>(idx));
        }
    }
    out = tmp;
    return true;
}

}  // namespace aether::rand

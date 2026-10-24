#include <aether/PhaseGeometry.h>

namespace aether::source {
namespace {
int reverseBits6(int x) noexcept {
  int r = 0;
  for (int i = 0; i < 6; ++i) r |= ((x >> i) & 1) << (5 - i);
  return r;
}
int popcount(unsigned x) noexcept {
  int n = 0;
  for (; x; x &= x - 1) ++n;
  return n;
}
} // namespace

int walshHadamardIndex(int sequency) noexcept {
  const int s = std::clamp(sequency, 0, walshOrder - 1);
  return reverseBits6(s ^ (s >> 1));
}

WalshRow walshSequencyRow(int sequency) noexcept {
  const int h = walshHadamardIndex(sequency);
  WalshRow row{};
  for (int j = 0; j < walshOrder; ++j)
    row[static_cast<std::size_t>(j)] = static_cast<std::int8_t>((popcount(static_cast<unsigned>(h & j)) & 1) ? -1 : 1);
  return row;
}

const Tables& tables() noexcept {
  static const Tables built = [] {
    Tables t{};
    for (int i = 0; i <= cosTableSize; ++i)
      t.cosine[static_cast<std::size_t>(i)] = static_cast<float>(std::cos(2 * pi * i / cosTableSize));
    for (int s = 0; s < walshOrder; ++s) {
      const WalshRow row = walshSequencyRow(s);
      auto& dst = t.walsh[static_cast<std::size_t>(s)];
      for (int i = 0; i < walshTableSize; ++i)
        dst[static_cast<std::size_t>(i)] =
            static_cast<float>(row[static_cast<std::size_t>(i * walshOrder / walshTableSize)]);
      dst[walshTableSize] = dst[0]; // periodic guard point
    }
    return t;
  }();
  return built;
}
} // namespace aether::source
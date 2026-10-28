// STUB (step 1 of processor wiring): compiles the declaration-only headers so API drift breaks the build. It defines
// nothing and no code refers to the declared members. Step 2 replaces it with RandomizeController.cpp.
#include "RandomizeController.h"
#include <type_traits>

static_assert(aether::rand::SnapshotRing::capacity() == 16, "Undo Random depth is frozen at 16");
static_assert(std::is_default_constructible_v<aether::rand::SnapshotRing>);
static_assert(std::is_default_constructible_v<aether::rand::RandomRecipe>);
static_assert(!std::is_copy_constructible_v<aether::rand::RandomizeController>);

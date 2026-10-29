// golden_check.c's helper: the Mutable Instruments code (Plaits, Rings, Clouds) shares one random number generator for the
// whole process (stmlib::Random), which creating an instance also draws from. Each golden sound starts it from the same
// seed, so a sound's samples don't depend on what ran before it or on how many voices an instance creates.
#include "stmlib/utils/random.h"

extern "C" void golden_reseed(void) { stmlib::Random::Seed(0x21); }

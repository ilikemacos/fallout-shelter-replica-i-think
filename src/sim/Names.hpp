#pragma once
#include "core/Random.hpp"
#include <string>

namespace hv::sim {

/// Original name pools — nothing lifted from an existing franchise.
std::string randomGivenName(Rng& rng, bool female);
std::string randomFamilyName(Rng& rng);
std::string randomFullName(Rng& rng, bool female);
/// Names for surface locations, expeditions and quests.
std::string randomPlaceName(Rng& rng);
std::string randomFactionName(Rng& rng);

} // namespace hv::sim

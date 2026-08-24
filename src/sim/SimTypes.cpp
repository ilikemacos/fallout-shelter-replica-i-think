#include "sim/SimTypes.hpp"

namespace hv::sim {

const char* resourceName(Resource r) {
    switch (r) {
        case Resource::Power:     return "Power";
        case Resource::Water:     return "Water";
        case Resource::Food:      return "Food";
        case Resource::Medicine:  return "Medicine";
        case Resource::Materials: return "Materials";
        case Resource::Research:  return "Research";
        case Resource::Scrip:     return "Scrip";
        default: return "?";
    }
}

const char* resourceShortName(Resource r) {
    switch (r) {
        case Resource::Power:     return "PWR";
        case Resource::Water:     return "H2O";
        case Resource::Food:      return "FOOD";
        case Resource::Medicine:  return "MED";
        case Resource::Materials: return "MAT";
        case Resource::Research:  return "RSCH";
        case Resource::Scrip:     return "SCRP";
        default: return "?";
    }
}

const char* skillName(Skill s) {
    switch (s) {
        case Skill::Engineering: return "Engineering";
        case Skill::Hydrology:   return "Hydrology";
        case Skill::Agronomy:    return "Agronomy";
        case Skill::Medicine:    return "Medicine";
        case Skill::Security:    return "Security";
        case Skill::Science:     return "Science";
        case Skill::Logistics:   return "Logistics";
        case Skill::Presence:    return "Presence";
        default: return "?";
    }
}

const char* skillAbbrev(Skill s) {
    switch (s) {
        case Skill::Engineering: return "ENG";
        case Skill::Hydrology:   return "HYD";
        case Skill::Agronomy:    return "AGR";
        case Skill::Medicine:    return "MED";
        case Skill::Security:    return "SEC";
        case Skill::Science:     return "SCI";
        case Skill::Logistics:   return "LOG";
        case Skill::Presence:    return "PRS";
        default: return "?";
    }
}

} // namespace hv::sim

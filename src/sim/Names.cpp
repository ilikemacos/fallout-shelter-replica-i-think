#include "sim/Names.hpp"
#include <vector>

namespace hv::sim {
namespace {

const std::vector<std::string> kFemale = {
    "Alma","Bea","Cora","Dessa","Elin","Faye","Greta","Hollis","Imogen","Junia",
    "Kestrel","Lark","Mave","Nessa","Odile","Perrin","Quill","Rue","Saska","Tove",
    "Ursa","Vesna","Wren","Xanthe","Yara","Zelda","Bramble","Calla","Dorit","Edda",
    "Fen","Gilda","Harlow","Isla","Jory","Kit"
};
const std::vector<std::string> kMale = {
    "Abel","Boone","Cassius","Dorn","Ewan","Fisk","Gareth","Hollis","Ivor","Jonas",
    "Kade","Lem","Mattock","Niall","Osric","Pike","Quinn","Roald","Silas","Tam",
    "Ulric","Vance","Warrick","Xavier","Yance","Zeb","Ardal","Brint","Colm","Denz",
    "Ezra","Flint","Gunnar","Hale","Ivo","Jarl"
};
const std::vector<std::string> kFamily = {
    "Ashgrove","Bellwether","Calloway","Draysen","Ebbot","Farrowgate","Glasswell",
    "Hollowick","Ironhale","Jessup","Kestrelmoor","Lampwright","Marrowfield",
    "Northgate","Oakhelm","Pellerin","Quarrow","Redlantern","Stonecarve","Thackery",
    "Underhill","Varrick","Weatherly","Yarrowmere","Ziegler","Colderidge","Dunmark",
    "Fallowgate","Grimsby","Hartline","Kilburn","Mercer","Ostwick","Prentiss",
    "Ravensfield","Sedgewick","Trellis","Wexler"
};
const std::vector<std::string> kPlacePrefix = {
    "Rust","Ash","Cinder","Hollow","Grey","Salt","Iron","Bleak","Wither","Sable",
    "Dust","Copper","Slate","Bone","Storm","Coldwater","Ember","Rime"
};
const std::vector<std::string> kPlaceSuffix = {
    "Junction","Yards","Terminus","Overpass","Reservoir","Substation","Depot",
    "Interchange","Silo","Refinery","Crossing","Waterworks","Foundry","Sanatorium",
    "Relay","Warehouse","Quarry","Motel","Cannery","Trainshed"
};
const std::vector<std::string> kFactionA = {
    "Cinderline","Ashfall","Longwire","Rustcrown","Hollow Court","Saltwatch",
    "Ironvein","Greyhand","Deepwatch","Slagborne"
};
const std::vector<std::string> kFactionB = {
    "Collective","Cartel","Salvagers","Wardens","Congregation","Company",
    "Union","Riders","Reclaimers","Syndicate"
};

} // namespace

std::string randomGivenName(Rng& rng, bool female) {
    return rng.pick(female ? kFemale : kMale);
}
std::string randomFamilyName(Rng& rng) { return rng.pick(kFamily); }

std::string randomFullName(Rng& rng, bool female) {
    return randomGivenName(rng, female) + " " + randomFamilyName(rng);
}

std::string randomPlaceName(Rng& rng) {
    return rng.pick(kPlacePrefix) + " " + rng.pick(kPlaceSuffix);
}

std::string randomFactionName(Rng& rng) {
    return "The " + rng.pick(kFactionA) + " " + rng.pick(kFactionB);
}

} // namespace hv::sim

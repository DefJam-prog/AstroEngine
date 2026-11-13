#include "astro/prefabs/PrefabLibrary.hpp"

#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;
void check(bool condition, const char* text)
{
    if (!condition) { std::cerr << "FAIL: " << text << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro::prefabs;
    PrefabAsset valid{1, "Box", {{1, 0, "Root", {}, std::nullopt, 0}, {2, 1, "Child", {}, std::nullopt, 0}}};
    check(validatePrefab(valid).hasValue(), "accept one rooted prefab tree");
    auto duplicate = valid;
    duplicate.nodes[1].localId = 1;
    check(!validatePrefab(duplicate), "reject duplicate local IDs");
    auto missingParent = valid;
    missingParent.nodes[1].parentLocalId = 99;
    check(!validatePrefab(missingParent), "reject missing local parent");
    auto cycle = valid;
    cycle.nodes[0].parentLocalId = 2;
    cycle.nodes[1].parentLocalId = 1;
    check(!validatePrefab(cycle), "reject hierarchy cycle");
    auto multipleRoots = valid;
    multipleRoots.nodes[1].parentLocalId = 0;
    check(!validatePrefab(multipleRoots), "reject multiple prefab roots");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

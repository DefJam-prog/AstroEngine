#include "astro/scene/Scene.hpp"

#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main()
{
    astro::scene::Scene scene(1, "Основная сцена");
    astro::scene::Scene defaults(2, "Defaults");
    const auto defaultEntity = defaults.createEntity();
    check(defaultEntity && defaults.find(defaultEntity.value())->name == "Entity", "apply schema default on creation");
    const auto root = scene.createEntity("Игрок Ω");
    check(root.hasValue(), "create UTF-8 root entity");
    const auto child = scene.createEntity("Камера", root ? std::optional{root.value()} : std::nullopt);
    check(child.hasValue(), "create child entity");
    check(scene.entities().size() == 2 && scene.find(root ? root.value() : 0) != nullptr,
        "entities are indexed and retain insertion order");

    const auto other = scene.createEntity("Группа");
    check(other.hasValue(), "create second root");
    if (root && child && other)
    {
        check(scene.reparent(root.value(), other.value()).hasValue(), "reparent root under another entity");
        check(scene.find(root.value())->parent == other.value(), "parent link updated");
        check(!scene.reparent(other.value(), child.value()), "reject hierarchy cycle");
        check(!scene.reparent(child.value(), 999999), "reject missing parent");
        check(!scene.erase(other.value()), "reject non-recursive deletion with children");
        check(scene.entities().size() == 3, "failed operation leaves model unchanged");
        check(scene.erase(other.value(), true).hasValue(), "delete hierarchy recursively");
        check(scene.entities().empty(), "recursive delete removes descendants");
    }

    check(!scene.createEntity(" \t\r\n"), "reject blank entity names");
    check(!scene.createEntity(std::string(241, 'x')), "reject names beyond schema byte limit");
    check(!scene.createEntity(std::string("\xC0\xAF", 2)), "reject invalid UTF-8 entity names");
    check(!scene.erase(123456), "reject deletion of a missing entity");
    if (failures == 0)
    {
        std::cout << "All scene tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

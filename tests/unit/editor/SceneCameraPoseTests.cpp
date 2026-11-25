#include "SceneCameraPose.hpp"

#include <bx/math.h>

#include <cmath>
#include <iostream>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool approximatelyEqual(const float lhs, const float rhs)
{
    return std::abs(lhs - rhs) < 1.0e-4f;
}
}

int main()
{
    using namespace astro;
    scene::Scene document(17, "Camera hierarchy Ω");
    const auto root = document.createEntity("Parent");
    const auto cameraEntity = root ? document.createEntity("Active Camera", root.value())
        : core::Result<scene::EntityId>::failure({core::ErrorCode::InternalError, "Parent creation failed"});
    check(root && cameraEntity, "create parent and child camera entities");
    if (!root || !cameraEntity) return 1;

    scene::Transform parentTransform;
    parentTransform.position = {10.0, 2.0, 3.0};
    parentTransform.rotationDegrees = {0.0, 90.0, 0.0};
    parentTransform.scale = {2.0, 1.0, 3.0};
    scene::Transform childTransform;
    childTransform.position = {1.0, 0.0, 2.0};
    childTransform.rotationDegrees = {0.0, 15.0, 0.0};
    check(document.setTransform(root.value(), parentTransform).hasValue()
        && document.setTransform(cameraEntity.value(), childTransform).hasValue(),
        "set parent and child camera transforms");
    check(document.setCamera3D(cameraEntity.value(), 72.0, 0.25, 640.0).hasValue()
        && document.setCamera3DActive(cameraEntity.value(), true).hasValue(),
        "configure and activate a perspective camera");

    const auto world = editor::sceneEntityWorldMatrix(document, cameraEntity.value());
    const auto camera = editor::sceneCameraPose(document, cameraEntity.value());
    check(world && camera, "compose hierarchical Transform and derive Camera3D pose");
    if (world && camera)
    {
        const auto expectedPosition = bx::mul(bx::Vec3{0.0f, 0.0f, 0.0f}, world.value().data());
        const auto expectedForward = bx::mul(bx::Vec3{0.0f, 0.0f, -1.0f}, world.value().data());
        const auto expectedUp = bx::mul(bx::Vec3{0.0f, 1.0f, 0.0f}, world.value().data());
        check(approximatelyEqual(camera.value().position.x, expectedPosition.x)
            && approximatelyEqual(camera.value().position.y, expectedPosition.y)
            && approximatelyEqual(camera.value().position.z, expectedPosition.z),
            "camera position uses parent rotation, scale and child translation");
        check(approximatelyEqual(camera.value().target.x, expectedForward.x)
            && approximatelyEqual(camera.value().target.y, expectedForward.y)
            && approximatelyEqual(camera.value().target.z, expectedForward.z),
            "camera forward follows the composed local negative-Z axis");
        check(approximatelyEqual(camera.value().up.x, expectedUp.x - expectedPosition.x)
            && approximatelyEqual(camera.value().up.y, expectedUp.y - expectedPosition.y)
            && approximatelyEqual(camera.value().up.z, expectedUp.z - expectedPosition.z),
            "camera up follows the composed local positive-Y axis");
        check(approximatelyEqual(camera.value().verticalFovRadians, 72.0f * 3.14159265358979323846f / 180.0f)
            && approximatelyEqual(camera.value().nearPlane, 0.25f)
            && approximatelyEqual(camera.value().farPlane, 640.0f),
            "camera lens values are converted to render camera units");
        check(camera.value().viewMatrix().hasValue() && camera.value().projectionMatrix(16.0f / 9.0f),
            "derived camera produces valid view and perspective matrices");
    }

    check(document.setCamera3DActive(cameraEntity.value(), false).hasValue()
        && !editor::sceneCameraPose(document, cameraEntity.value()),
        "inactive camera cannot be resolved as the Game View camera");
    check(!editor::sceneCameraPose(document, root.value()), "entity without Camera3D is rejected");
    check(document.setCamera3DActive(cameraEntity.value(), true).hasValue(), "reactivate camera before invalid hierarchy test");
    auto singularTransform = document.transform(root.value())->value;
    singularTransform.scale.x = 0.0;
    check(document.setTransform(root.value(), singularTransform).hasValue()
        && !editor::sceneCameraPose(document, cameraEntity.value()),
        "zero-scale ancestor is rejected instead of yielding an invalid view basis");

    if (failures == 0) std::cout << "Scene camera pose checks passed\n";
    return failures == 0 ? 0 : 1;
}

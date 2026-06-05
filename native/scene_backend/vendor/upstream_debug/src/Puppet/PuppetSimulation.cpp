#include "Puppet/PuppetSimulation.hpp"

#include <algorithm>
#include <cmath>

using namespace wallpaper;

bool wallpaper::IsBoneEligibleForRuntimeSimulation(const PuppetSimulationBoneInput& bone,
                                                   bool hasAuthoredDelta)
{
    if (!bone.hasParent || !bone.metadata.valid ||
        !bone.metadata.physicsActive ||
        !bone.metadata.targetPointPresent) {
        return false;
    }

    return !hasAuthoredDelta;
}

void wallpaper::ApplyRuntimePuppetSimulationStep(
    double dt,
    std::span<const PuppetSimulationBoneInput> bones,
    const std::vector<bool>& authoredDeltaBones,
    std::vector<PuppetSimulationBoneState>& states,
    std::vector<Eigen::Affine3f>& worldAffines)
{
    const float step = std::clamp(static_cast<float>(dt), 0.0f, 1.0f / 30.0f);
    if (step <= 0.0f || states.size() != bones.size() ||
        worldAffines.size() != bones.size()) {
        return;
    }

    std::vector<Eigen::Affine3f> authoredLocalAffines(
        bones.size(), Eigen::Affine3f::Identity());
    std::vector<bool> validParentOrder(bones.size(), true);
    for (size_t i = 0; i < bones.size(); ++i) {
        const auto& bone = bones[i];
        if (!bone.hasParent) {
            authoredLocalAffines[i] = worldAffines[i];
            continue;
        }
        if (static_cast<size_t>(bone.parent) >= i) {
            validParentOrder[i] = false;
            continue;
        }
        authoredLocalAffines[i] =
            worldAffines[static_cast<size_t>(bone.parent)].inverse() * worldAffines[i];
    }

    for (size_t i = 0; i < bones.size(); ++i) {
        if (!validParentOrder[i]) {
            states[i] = {};
            continue;
        }

        const auto& bone = bones[i];
        const Eigen::Affine3f parentWorld =
            bone.hasParent ? worldAffines[static_cast<size_t>(bone.parent)]
                           : Eigen::Affine3f::Identity();
        const Eigen::Affine3f localAffine = authoredLocalAffines[i];

        const bool hasAuthoredDelta =
            i < authoredDeltaBones.size() && authoredDeltaBones[i];
        if (!IsBoneEligibleForRuntimeSimulation(bones[i], hasAuthoredDelta)) {
            states[i] = {};
            worldAffines[i] = bone.hasParent ? parentWorld * localAffine : localAffine;
            continue;
        }

        auto& state = states[i];
        const Eigen::Vector3f authoredLocal = localAffine.translation();
        const Eigen::Vector3f parentWorldPosition = parentWorld.translation();

        Eigen::Vector3f target(
            bone.metadata.targetPoint[0],
            bone.metadata.targetPoint[1],
            bone.metadata.targetPoint[2]);
        const float targetLength = target.norm();
        if (!std::isfinite(target.x()) || !std::isfinite(target.y()) ||
            !std::isfinite(target.z()) || targetLength <= 1.0e-4f) {
            states[i] = {};
            worldAffines[i] = bone.hasParent ? parentWorld * localAffine : localAffine;
            continue;
        }

        if (!state.initialized) {
            state.initialized = true;
            state.localPosition = authoredLocal;
            state.velocity = Eigen::Vector3f::Zero();
            state.previousParentWorldPosition = parentWorldPosition;
        }

        const float mass = std::clamp(bone.metadata.targetMass, 1.0f, 1000.0f);
        const float stiffness = std::clamp(1800.0f / mass, 4.0f, 45.0f);
        const float damping = 2.0f * std::sqrt(stiffness);
        const Eigen::Vector3f parentDeltaWorld =
            parentWorldPosition - state.previousParentWorldPosition;
        state.previousParentWorldPosition = parentWorldPosition;

        const Eigen::Vector3f parentDeltaLocal =
            parentWorld.linear().inverse() * parentDeltaWorld;
        const float targetScale = std::clamp(targetLength / 200.0f, 0.25f, 4.0f);
        const Eigen::Vector3f drivenTarget = authoredLocal - (parentDeltaLocal * targetScale);
        const Eigen::Vector3f displacement = state.localPosition - drivenTarget;
        const Eigen::Vector3f acceleration =
            (-stiffness * displacement) - (damping * state.velocity);

        state.velocity += acceleration * step;
        state.velocity = state.velocity.cwiseMax(Eigen::Vector3f::Constant(-120.0f))
                                      .cwiseMin(Eigen::Vector3f::Constant(120.0f));
        state.localPosition += state.velocity * step;

        Eigen::Affine3f simulatedLocal = localAffine;
        simulatedLocal.translation() = state.localPosition;
        worldAffines[i] = parentWorld * simulatedLocal;
    }
}

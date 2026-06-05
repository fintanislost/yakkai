#pragma once

#include <Eigen/Geometry>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace wallpaper
{

struct PuppetSimulationMetadata {
    bool valid { false };
    bool physicsActive { false };
    bool targetPointPresent { false };
    std::array<float, 3> targetPoint { 0.0f, 0.0f, 0.0f };
    float targetMass { 0.0f };
};

struct PuppetSimulationBoneInput {
    bool hasParent { false };
    uint32_t parent { 0xFFFFFFFFu };
    PuppetSimulationMetadata metadata;
};

struct PuppetSimulationBoneState {
    bool initialized { false };
    Eigen::Vector3f localPosition { Eigen::Vector3f::Zero() };
    Eigen::Vector3f velocity { Eigen::Vector3f::Zero() };
    Eigen::Vector3f previousParentWorldPosition { Eigen::Vector3f::Zero() };
};

bool IsBoneEligibleForRuntimeSimulation(const PuppetSimulationBoneInput& bone,
                                        bool hasAuthoredDelta);

void ApplyRuntimePuppetSimulationStep(
    double dt,
    std::span<const PuppetSimulationBoneInput> bones,
    const std::vector<bool>& authoredDeltaBones,
    std::vector<PuppetSimulationBoneState>& states,
    std::vector<Eigen::Affine3f>& worldAffines);

} // namespace wallpaper

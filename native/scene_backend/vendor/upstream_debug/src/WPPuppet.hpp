#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <span>
#include <Eigen/Geometry>

#include "Core/Literals.hpp"
#include "Puppet/PuppetSimulation.hpp"

namespace wallpaper
{

class WPPuppetLayer;

enum class PuppetSimulationMode
{
    Off,
    Diagnostic,
    Runtime
};

PuppetSimulationMode ParsePuppetSimulationMode(std::string_view value);

class WPPuppet {
public:
    enum class PlayMode
    {
        Loop,
        Mirror,
        Single
    };
    struct Bone {
        struct SimulationMetadata {
            bool present { false };
            bool valid { false };
            bool physicsActive { false };
            bool targetPointPresent { false };
            std::array<float, 3> targetPoint { 0.0f, 0.0f, 0.0f };
            bool targetMassPresent { false };
            float targetMass { 0.0f };
        };

        Eigen::Affine3f transform { Eigen::Affine3f::Identity() };
        uint32_t        parent { 0xFFFFFFFFu };
        std::string     name;
        std::string     simulationMetadata;
        SimulationMetadata parsedSimulationMetadata;

        bool noParent() const { return parent == 0xFFFFFFFFu; }
        // prepared
        Eigen::Affine3f offset_trans { Eigen::Affine3f::Identity() };
        /*
        Eigen::Vector3f world_axis_x;
        Eigen::Vector3f world_axis_y;
        Eigen::Vector3f world_axis_z;
        */
    };
    struct BoneFrame {
        Eigen::Vector3f position;
        Eigen::Vector3f angle;
        Eigen::Vector3f scale;

        // prepared
        Eigen::Quaterniond quaternion;
    };
    struct Animation {
        i32         id;
        double      fps;
        i32         length;
        PlayMode    mode;
        std::string name;

        struct BoneFrames {
            std::vector<BoneFrame> frames;
        };
        std::vector<BoneFrames> bframes_array;

        // prepared
        double max_time;
        double frame_time;
        struct InterpolationInfo {
            idx    frame_a;
            idx    frame_b;
            double t;
        };
        InterpolationInfo getInterpolationInfo(double* cur_time) const;
    };

public:
    std::vector<Bone>      bones;
    std::vector<Animation> anims;

    std::span<const Eigen::Affine3f> genFrame(WPPuppetLayer&, double time) noexcept;
    void                             prepared();

private:
    std::vector<Eigen::Affine3f> m_final_affines;
};

class WPPuppetLayer {
    friend class WPPuppet;

public:
    WPPuppetLayer();
    WPPuppetLayer(std::shared_ptr<WPPuppet>);
    ~WPPuppetLayer();

    bool hasPuppet() const { return (bool)m_puppet; };

    struct AnimationLayer {
        i32    id { 0 };
        std::string name;
        double rate { 1.0f };
        double blend { 1.0f };
        bool   visible { true };
        bool   paused { false };
        bool   additive { false };
        double cur_time { 0.0f };
    };

    void prepared(std::span<AnimationLayer>);

    std::span<const Eigen::Affine3f> genFrame(double time) noexcept;

    void updateInterpolation(double time) noexcept;

    void setSimulationMode(PuppetSimulationMode mode) {
        m_simulationMode = mode;
        m_simulationModeExplicit = true;
    }
    PuppetSimulationMode simulationMode() const { return m_simulationMode; }
    bool isBoneEligibleForSimulationForTests(size_t boneIndex) const;

private:
    struct Layer {
        AnimationLayer                         anim_layer;
        double                                 blend;
        const WPPuppet::Animation*             anim { nullptr };
        WPPuppet::Animation::InterpolationInfo interp_info {};
        std::vector<bool>                      authoredDeltaBones;

        operator bool() const noexcept { return anim != nullptr; };
    };

    double m_global_blend { 1.0 };
    double m_total_blend { 0.0 };
    PuppetSimulationMode m_simulationMode { PuppetSimulationMode::Off };
    bool m_simulationModeExplicit { false };

    void applySimulationForFrame(double dt,
                                 const std::vector<WPPuppet::Bone>& bones,
                                 std::vector<Eigen::Affine3f>& worldAffines);

    std::vector<Layer>                     m_layers;
    std::vector<PuppetSimulationBoneInput> m_simulationBones;
    std::vector<bool>                      m_simulationAuthoredDeltaBones;
    std::vector<PuppetSimulationBoneState> m_simulationStates;
    std::shared_ptr<WPPuppet>              m_puppet;
};

} // namespace wallpaper

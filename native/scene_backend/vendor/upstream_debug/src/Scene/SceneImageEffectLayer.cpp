#include "SceneImageEffectLayer.h"
#include "SceneNode.h"

#include "SpecTexs.hpp"
#include "Core/StringHelper.hpp"
#include "Utils/Logging.h"

using namespace wallpaper;

SceneImageEffectLayer::SceneImageEffectLayer(SceneNode* node, float w, float h,
                                             std::string_view pingpong_a,
                                             std::string_view pingpong_b)
    : m_worldNode(node),
      m_pingpong_a(pingpong_a),
      m_pingpong_b(pingpong_b),
      m_final_mesh(std::make_unique<SceneMesh>()),
      m_final_node(std::make_unique<SceneNode>()) {};

void SceneImageEffectLayer::ResolveEffect(const SceneMesh& default_mesh,
                                          std::string_view effect_cam) {
    std::string_view ppong_a = m_pingpong_a, ppong_b = m_pingpong_b;
    auto             swap_pp = [&ppong_a, &ppong_b]() {
        std::swap(ppong_a, ppong_b);
    };
    auto default_node = SceneNode();
    auto describe_mesh = [](const SceneMesh& mesh) {
        const usize vaCount = mesh.VertexCount();
        const usize iaCount = mesh.IndexCount();
        const usize verts = vaCount > 0 ? mesh.GetVertexArray(0).VertexCount() : 0;
        const usize inds = iaCount > 0 ? mesh.GetIndexArray(0).RenderDataCount() : 0;
        return std::array<usize, 4> { vaCount, iaCount, verts, inds };
    };
    const bool debugLongEffectChain = m_effects.size() >= 10;
    if (! m_final_parent_captured) {
        m_final_parent = m_worldNode ? m_worldNode->Parent() : nullptr;
        m_final_parent_captured = true;
    }

    if (m_worldNode != nullptr && ! fullscreen) {
        m_worldNode->SetVirtualParent(nullptr);
    }

    SceneImageEffectNode* last_output { nullptr };
    for (size_t effectIndex = 0; effectIndex < m_effects.size(); ++effectIndex) {
        auto& eff = m_effects[effectIndex];
        const bool isLastEffect = effectIndex + 1 == m_effects.size();
        for (auto& cmd : eff->commands) {
            if (sstart_with(cmd.src, WE_DEBUG_EFFECT_FINAL_OUTPUT_PREFIX)) {
                cmd.srcFinalEffectOutput = true;
            }
            if (cmd.srcFinalEffectOutput)
                cmd.src = ppong_b;
            else if (sstart_with(cmd.src, WE_EFFECT_PPONG_PREFIX_A))
                cmd.src = ppong_a;

            if (sstart_with(cmd.dst, WE_DEBUG_EFFECT_FINAL_OUTPUT_PREFIX)) {
                cmd.dstFinalEffectOutput = true;
            }
            if (cmd.dstFinalEffectOutput)
                cmd.dst = ppong_b;
            else if (sstart_with(cmd.dst, WE_EFFECT_PPONG_PREFIX_A))
                cmd.dst = ppong_a;
        }
        for (auto it = eff->nodes.begin(); it != eff->nodes.end(); it++) {
            if (sstart_with(it->output, WE_EFFECT_PPONG_PREFIX_B) ||
                it->output == SpecTex_Default) {
                it->output  = ppong_b;
                last_output = &(*it);
            }

            assert(it->sceneNode->HasMaterial());

            auto& material = *(it->sceneNode->Mesh()->Material());
            {
                material.blenmode = BlendMode::Normal;
                it->sceneNode->SetCamera(effect_cam.data());
                it->sceneNode->SetVirtualParent(nullptr);
                it->sceneNode->CopyTrans(default_node);
                if (! it->preserveMesh) {
                    it->sceneNode->Mesh()->ChangeMeshDataFrom(default_mesh);
                }
            }
            if (debugLongEffectChain) {
                const auto meshInfo = describe_mesh(*it->sceneNode->Mesh());
                LOG_INFO("effect stage node: effectIndex=%zu/%zu output=%s shader=%s preserveMesh=%d tex0=%s va=%zu ia=%zu verts=%zu inds=%zu",
                         effectIndex + 1,
                         m_effects.size(),
                         it->output.c_str(),
                         material.customShader.shader ? material.customShader.shader->name.c_str() : "",
                         it->preserveMesh ? 1 : 0,
                         material.textures.empty() ? "" : material.textures.front().c_str(),
                         meshInfo[0],
                         meshInfo[1],
                         meshInfo[2],
                         meshInfo[3]);
            }

            auto& texs = material.textures;
            std::replace_if(
                texs.begin(),
                texs.end(),
                [](auto& t) {
                    return sstart_with(t, WE_EFFECT_PPONG_PREFIX_A);
                },
                ppong_a);
        }
        swap_pp();

        if (! isLastEffect) {
            last_output = nullptr;
        }
    }
    if (last_output != nullptr && m_publish_final_output) {
        last_output->output = SpecTex_Default;
        auto& mesh          = *(last_output->sceneNode->Mesh());
        auto& material      = *mesh.Material();
        {
            material.blenmode = m_final_blend;
            if (fullscreen) {
                // Fullscreen/composelayers: keep the effect camera and default
                // mesh so the final output renders as a fullscreen blit. Using
                // the composelayer's mesh with empty camera produces wrong MVP.
                LOG_INFO("effect final output (fullscreen blit): totalEffects=%zu blend=%d tex0=%s",
                         m_effects.size(),
                         static_cast<int>(m_final_blend),
                         material.textures.empty() ? "" : material.textures.front().c_str());
            } else {
                last_output->sceneNode->SetCamera(std::string());
                last_output->sceneNode->SetVirtualParent(m_final_parent);
                const auto finalMeshInfo = describe_mesh(*m_final_mesh);
                LOG_INFO("effect final output: totalEffects=%zu blend=%d hasWorldNode=%d hasParent=%d finalMeshVerts=%zu tex0=%s",
                         m_effects.size(),
                         static_cast<int>(m_final_blend),
                         m_worldNode ? 1 : 0,
                         m_final_parent ? 1 : 0,
                         finalMeshInfo[2],
                         material.textures.empty() ? "" : material.textures.front().c_str());
                last_output->sceneNode->CopyTrans(*m_final_node);
                mesh.ChangeMeshDataFrom(*m_final_mesh);
            }
            if (debugLongEffectChain) {
                const auto meshInfo = describe_mesh(mesh);
                LOG_INFO("effect final published node: totalEffects=%zu shader=%s tex0=%s va=%zu ia=%zu verts=%zu inds=%zu",
                         m_effects.size(),
                         material.customShader.shader ? material.customShader.shader->name.c_str() : "",
                         material.textures.empty() ? "" : material.textures.front().c_str(),
                         meshInfo[0],
                         meshInfo[1],
                         meshInfo[2],
                         meshInfo[3]);
            }
        }
    } else if (last_output != nullptr && ! m_publish_final_output) {
        LOG_INFO("effect final output kept offscreen: totalEffects=%zu", m_effects.size());
    }
}

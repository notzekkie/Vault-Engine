#pragma once
#include <dllapi.hpp>
#include <glm/ext.hpp>
#include "Base.hpp"
#include "Engine/Components/ModelAnimator.hpp"
#include <Renderer/Mesh.hpp>
#include <Renderer/Shader.hpp>
#include <memory>
#include <Renderer/Texture.hpp>
#include <Engine/Model.hpp>
#include <unordered_map>

namespace Engine {
    namespace Components {
        enum MeshType {
            MESH_PLANE,
            MESH_PYRAMID,
            MESH_CUBE,
            MESH_SPHERE,
            MESH_CAPSULE,
            MESH_CUSTOM_MODEL,
            MESH_NONE
        };

        struct DLL_API MeshRenderer : Base {
            static inline const std::string display_name = "Mesh Renderer";
            static std::unordered_map<MeshType, ModelMesh *> ModelMeshes;

            MeshType mesh_type = MESH_NONE;
            int mesh_index = -1;
            std::string mesh_path = "";
            std::shared_ptr<VaultRenderer::Mesh> mesh = nullptr;
            std::shared_ptr<ModelMesh> model = nullptr;
            std::string material_path = "";
            std::string animation_path = "";
            ModelAnimator *animator_ref;
            std::unique_ptr<VaultRenderer::Shader> custom_shader;
            std::string custom_shader_path;
            // Model Animation

            MeshRenderer() = default;

            void SetMeshType(const MeshType &a_mesh_type);
            void LoadMaterial(const std::string &path);
            void SetCustomMeshType(std::vector<VaultRenderer::Vertex> &vertices, std::vector<uint32_t> &indices);
            void OnGUI() override;

            // void SetAnimation(const std::string &path);
            // void AnimateAndSetUniforms(VaultRenderer::Shader &shader);
            // void DeleteAnimation();
        };
    } // namespace Components
} // namespace Engine
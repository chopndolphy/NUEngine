#include "vk_imgui.h"
#include <imgui.h>
#include "vk_loader.h"
#include "vk_types.h"
#include "vk_engine.h"
#include <glm/gtx/euler_angles.hpp>

void gui::display_scene_tree(const LoadedGLTF& scene)
{
        for (auto& node : scene.nodes) {
            if (ImGui::TreeNode(node.first.c_str())) {
                static float pos[3] = {node.second->worldTransform[3][0], node.second->worldTransform[3][1], node.second->worldTransform[3][2]};
                static float rot[3];
                glm::extractEulerAngleYXZ(node.second->localTransform, rot[1], rot[0], rot[2]);
                ImGui::InputFloat3("Position", pos);
                ImGui::InputFloat3("Rotation", rot);

                ImGui::TreePop();
            }
        }
}
#include "interprocess.h"

Interprocess::Interprocess(const std::unordered_map<std::string, std::shared_ptr<Node>> &nodeMap)
{
    boost::interprocess::shared_memory_object::remove("ambfInterprocess");

    _segment = boost::interprocess::managed_shared_memory(
        boost::interprocess::create_only,
        "ambfInterprocess",
        65536
    );
    _map = _segment.construct<HashMap>("HashMap")(
        nodeMap.size(), boost::hash<ShmemString>(), std::equal_to<ShmemString>(),
        _segment.get_allocator<HashValueType>());

    for (auto node : nodeMap) {
        ShmemString name(node.first.c_str(), _segment.get_allocator<ShmemString>());
        Transform trans{};
        assert(sizeof(trans.array) == sizeof(node.second->worldTransform));
        memcpy(trans.array, glm::value_ptr(node.second->worldTransform), sizeof(node.second->worldTransform));
        HashValueType value(name, trans);
        _map->insert(value);
    }
    ShmemString cameraBasis("CameraBasis", _segment.get_allocator<ShmemString>());
    Transform cameraTransform{};
    glm::mat4 cameraMat{0.0f};
    cameraMat[0][0] = 1.0f;
    cameraMat[1][1] = 1.0f;
    cameraMat[2][2] = 1.0f;
    cameraMat[3][3] = 1.0f;
    memcpy(cameraTransform.array, glm::value_ptr(cameraMat), sizeof(cameraMat));
    HashValueType value(cameraBasis, cameraTransform);
    _map->insert(value);
}

void Interprocess::destroy()
{
    boost::interprocess::shared_memory_object::remove("ambfInterprocess");
}

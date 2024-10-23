#pragma once

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/unordered_map.hpp>
#include <memory>
#include <functional>
#include <utility>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <unordered_map>
#include "vk_types.h"

namespace bip = boost::interprocess;
typedef bip::allocator<char, bip::managed_shared_memory::segment_manager> CharAllocator;
typedef bip::basic_string<char, std::char_traits<char>, CharAllocator> ShmemString;
typedef ShmemString HashKeyType;

struct Transform {
    float array[16] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
};
typedef Transform HashMappedType;
typedef std::pair<const ShmemString, Transform> HashValueType;
typedef bip::allocator<HashValueType, bip::managed_shared_memory::segment_manager> HashMemAllocator;
typedef boost::unordered_map<HashKeyType, HashMappedType, boost::hash<HashKeyType>, std::equal_to<HashKeyType>, HashMemAllocator> HashMap;
class Interprocess {
    public:
        Interprocess(const std::unordered_map<std::string, std::shared_ptr<Node>> &nodeMap);
        void destroy();
        bip::managed_shared_memory _segment;
        bip::offset_ptr<HashMap> _map;
};
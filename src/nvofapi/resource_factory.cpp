#include "resource_factory.h"
#include "../util/util_string.h"
#include "../util/util_log.h"

namespace dxvk {
    ResourceFactory::ResourceFactory() = default;

    ResourceFactory::~ResourceFactory() = default;

    std::unique_ptr<Vulkan> ResourceFactory::CreateVulkan(const std::string& libraryName) {
        return std::make_unique<Vulkan>(libraryName);
    }
}

#pragma once

#include "../nvofapi_private.h"
#include "vulkan.h"

namespace dxvk {
    class ResourceFactory {

      public:
        ResourceFactory();
        virtual ~ResourceFactory();

        virtual std::unique_ptr<Vulkan> CreateVulkan(const std::string& libraryName);
    };
}

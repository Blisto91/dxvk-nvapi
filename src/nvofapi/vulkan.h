#pragma once

#include "../nvofapi_private.h"

namespace dxvk {
    class Vulkan {

      public:
        Vulkan();
        explicit Vulkan(const std::string& libraryName);
        virtual ~Vulkan();

        [[nodiscard]] virtual bool IsAvailable() const;

      private:
        HMODULE m_library{};
        PFN_vkGetInstanceProcAddr m_vkGetInstanceProcAddr{};
    };
}

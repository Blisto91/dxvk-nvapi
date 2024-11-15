#include "vulkan.h"

namespace dxvk {
    Vulkan::Vulkan() = default;

    Vulkan::Vulkan(const std::string& libraryName) {

        m_library = LoadLibraryA(libraryName.c_str());
        if (!m_library) {
            return;
        }

        m_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) reinterpret_cast<void*>(GetProcAddress(m_library, "vkGetInstanceProcAddr"));
    }

    Vulkan::~Vulkan() {
        if (m_library == nullptr)
            return;

        ::FreeLibrary(m_library);
        m_library = nullptr;
    };

    bool Vulkan::IsAvailable() const {
        return m_vkGetInstanceProcAddr != nullptr;
    }
}
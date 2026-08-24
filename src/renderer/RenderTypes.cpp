#include "renderer/RenderTypes.hpp"

namespace hv::gfx {

const char* backendName(Backend b) {
    switch (b) {
        case Backend::None:   return "None";
        case Backend::OpenGL: return "OpenGL 4.1";
        case Backend::Vulkan: return "Vulkan (MoltenVK)";
    }
    return "Unknown";
}

} // namespace hv::gfx

# Camera Components CMakeLists.txt

set(CAMERA_SOURCES
    "camera_resource_manager.cc"
    "enhanced_esp32_camera.cc"
    "vision_integration.cc"
    "mcp_camera_tools.cc"
    "camera_components.cc"
)

set(CAMERA_INCLUDES
    "camera_resource_manager.h"
    "enhanced_esp32_camera.h"
    "vision_integration.h"
    "mcp_camera_tools.h"
    "camera_components.h"
)

# Add camera sources to main component
target_sources(${COMPONENT_LIB} PRIVATE ${CAMERA_SOURCES})

# Add camera include directory
target_include_directories(${COMPONENT_LIB} PUBLIC .)
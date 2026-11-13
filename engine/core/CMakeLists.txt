add_library(astro_core
    src/CommandRegistry.cpp
    src/Logger.cpp
    src/ConfigurationService.cpp
    src/ModuleRegistry.cpp
    src/TimeService.cpp
    src/TransactionHistory.cpp
)
target_include_directories(astro_core PUBLIC include)
target_compile_features(astro_core PUBLIC cxx_std_20)
set_target_properties(astro_core PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(astro_core PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(astro_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

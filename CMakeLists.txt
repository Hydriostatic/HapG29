cmake_minimum_required(VERSION 3.20.2)
project(G29DeskHaptics VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Runtime estático: o .exe roda sem instalar o Visual C++ Redistributable.
# Precisa vir antes do FetchContent para o ViGEmClient usar o mesmo runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ViGEmClient (biblioteca cliente oficial do driver ViGEmBus), fixada num commit.
include(FetchContent)
FetchContent_Declare(
    ViGEmClient
    GIT_REPOSITORY https://github.com/nefarius/ViGEmClient.git
    GIT_TAG        b66d02d57e32cc8595369c53418b843e958649b4
)
FetchContent_MakeAvailable(ViGEmClient)

add_executable(g29haptic
    src/main.cpp
    src/ffb_device.cpp
    src/rumble_bridge.cpp
)

target_compile_definitions(g29haptic PRIVATE UNICODE _UNICODE NOMINMAX)

target_link_libraries(g29haptic PRIVATE
    ViGEmClient::ViGEmClient
    dinput8     # DirectInput 8 (Force Feedback)
    dxguid      # GUIDs do DirectInput (GUID_Sine, IID_IDirectInput8W...)
    winmm       # timeBeginPeriod (loop de 100 Hz com baixa latência)
    setupapi    # dependência do ViGEmClient estático
)

if(MSVC)
    target_compile_options(g29haptic PRIVATE /W4 /utf-8)
endif()

# Testes da camada de segurança (não precisam do volante): ctest ou safety_test.exe
enable_testing()
add_executable(safety_test tests/safety_test.cpp)
add_test(NAME safety COMMAND safety_test)

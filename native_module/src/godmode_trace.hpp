#pragma once
#include <string>

namespace openscumrcon::godmode_trace
{
    bool initialize();
    bool supported_build();
    void set_interface_hook_available(bool available);
    void observe_interface(void* owner, void* interface_class, void* result);
    void observe_effect(void* prisoner);
    std::string start();
    std::string stop();
    std::string status();
    void tick();
    void shutdown();
}

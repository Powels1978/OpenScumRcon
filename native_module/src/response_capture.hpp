#pragma once
#include "response_buffer.hpp"
#include <cstdint>

namespace openscumrcon::response_capture
{
bool initialize();
bool ready();
std::uintptr_t function_address();
void shutdown();
}

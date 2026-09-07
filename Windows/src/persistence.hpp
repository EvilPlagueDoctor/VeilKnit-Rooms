#pragma once

#include "core_types.hpp"
#include <filesystem>
#include <vector>

namespace vkrooms {

std::vector<Room> load_rooms(const std::filesystem::path& path);
void save_rooms(const std::filesystem::path& path, const std::vector<Room>& rooms);

} // namespace vkrooms

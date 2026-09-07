#pragma once

#include "core_types.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace vkrooms {

std::string serialize_rooms(const std::vector<Room>& rooms);
std::vector<Room> deserialize_rooms(const std::string& text);
std::vector<Room> load_rooms(const std::filesystem::path& path);
void save_rooms(const std::filesystem::path& path, const std::vector<Room>& rooms);

} // namespace vkrooms

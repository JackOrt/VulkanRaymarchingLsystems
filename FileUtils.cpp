#include "FileUtils.hpp"
#include <fstream>
#include <stdexcept>

/**
 * readFile:
 *   Loads a binary file (e.g. .spv) into a std::vector<char>.
 */
std::vector<char> readFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

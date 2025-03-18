#pragma once
#include <string>
#include <vector>

/**
 * Simple utility to read an entire file (SPIR-V, etc.) into a vector<char>.
 */
std::vector<char> readFile(const std::string& filename);

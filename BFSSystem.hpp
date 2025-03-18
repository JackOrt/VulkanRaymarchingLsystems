#pragma once
#include <vector>

/**
 * CPUBranch: BFS-based tree segment description.
 * Each branch has a parentIndex so that children can anchor
 * to the parent's partial end in the raymarch shader.
 */
struct CPUBranch
{
    float startX, startY, startZ;
    float endX, endY, endZ;
    float radius;
    float bfsDepth;   // BFS level
    int   parentIndex; // -1 if no parent
};

/**
 * Generate a random BFS tree of CPUBranch data.
 * Called periodically in the VulkanRaymarchApp.
 */
std::vector<CPUBranch> generateRandomBFSSystem();

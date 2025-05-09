#pragma once
#include "BFSSystem.hpp"
#include <vector>
#include <glm/glm.hpp>

/* --------------------------------------------------------------------------
   Very small 2-level BVH.

   • Build:  split branches into 64 buckets by longest axis of bounds.
   • Store:  internal nodes first, then leaves packed tightly.
   --------------------------------------------------------------------------*/
struct BvhNode {
    glm::vec3 mn, mx;
    uint32_t  lo;              /* child index  OR leaf start            */
    uint32_t  hi;              /* child index OR leaf count | 0x8000..  */
};

struct BuiltBVH {
    std::vector<BvhNode> nodes;
    std::vector<uint32_t> leafIdx;
};

BuiltBVH buildBVH(const std::vector<CPUBranch>& br);

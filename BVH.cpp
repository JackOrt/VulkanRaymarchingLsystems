/* --------------------------------------------------------------------------
   BVH.cpp  –  balanced binary BVH for branch cylinders
   Replaces the old two?level bucket partition.

   • Recursively splits the longest axis at object median
   • Stops when leaf size ? 8
   • Node layout / leaf flag identical to previous shader contract
   --------------------------------------------------------------------------*/
#include "BVH.hpp"
#include <algorithm>
#include <stack>

   /* ---------- helpers ---------------------------------------------------- */
static void branchBounds(const CPUBranch& b, glm::vec3& mn, glm::vec3& mx)
{
    mn = glm::min(glm::vec3(b.startX, b.startY, b.startZ),
        glm::vec3(b.endX, b.endY, b.endZ)) - b.radius;
    mx = glm::max(glm::vec3(b.startX, b.startY, b.startZ),
        glm::vec3(b.endX, b.endY, b.endZ)) + b.radius;
}

/* ---------- recursive builder ------------------------------------------ */
static uint32_t buildNode(BuiltBVH& out,
    const std::vector<CPUBranch>& br,
    const std::vector<glm::vec3>& bmn,
    const std::vector<glm::vec3>& bmx,
    std::vector<uint32_t>& indices,
    uint32_t first, uint32_t count)
{
    const uint32_t LEAF_SIZE = 8u;
    uint32_t myIndex = (uint32_t)out.nodes.size();
    out.nodes.emplace_back();              /* reserve slot */

    /* compute bounds of this set */
    glm::vec3 mn(1e9f), mx(-1e9f);
    for (uint32_t i = 0; i < count; ++i) {
        mn = glm::min(mn, bmn[indices[first + i]]);
        mx = glm::max(mx, bmx[indices[first + i]]);
    }

    /* leaf? ------------------------------------------------------------- */
    if (count <= LEAF_SIZE) {
        uint32_t start = (uint32_t)out.leafIdx.size();
        out.leafIdx.insert(out.leafIdx.end(),
            indices.begin() + first,
            indices.begin() + first + count);

        BvhNode n;
        n.mn = mn;  n.mx = mx;
        n.lo = start;
        n.hi = count | 0x80000000u;        /* hi bit marks leaf */
        out.nodes[myIndex] = n;
        return myIndex;
    }

    /* choose split axis: longest extent -------------------------------- */
    glm::vec3 size = mx - mn;
    int axis = (size.x > size.y)
        ? (size.x > size.z ? 0 : 2)
        : (size.y > size.z ? 1 : 2);

    /* partition indices by median centroid along axis ------------------ */
    uint32_t mid = first + count / 2;
    std::nth_element(indices.begin() + first,
        indices.begin() + mid,
        indices.begin() + first + count,
        [&](uint32_t a, uint32_t b) {
            float ca = 0.5f * (bmn[a][axis] + bmx[a][axis]);
            float cb = 0.5f * (bmn[b][axis] + bmx[b][axis]);
            return ca < cb;
        });

    uint32_t left = buildNode(out, br, bmn, bmx, indices, first, mid - first);
    uint32_t right = buildNode(out, br, bmn, bmx, indices, mid, count - (mid - first));

    /* fill this internal node ----------------------------------------- */
    BvhNode n;
    n.mn = mn;  n.mx = mx;
    n.lo = left;
    n.hi = right;               /* hi bit clear = internal */
    out.nodes[myIndex] = n;
    return myIndex;
}

/* ---------- public entry ----------------------------------------------- */
BuiltBVH buildBVH(const std::vector<CPUBranch>& br)
{
    BuiltBVH out;
    if (br.empty()) {
        /* dummy 1?node BVH so shader won’t crash */
        BvhNode n{};
        n.mn = glm::vec3(0);  n.mx = glm::vec3(0);
        n.lo = 0;  n.hi = 0x80000000u;
        out.nodes.push_back(n);
        return out;
    }

    /* pre?compute per?branch bounds */
    size_t N = br.size();
    std::vector<glm::vec3> bmn(N), bmx(N);
    for (size_t i = 0; i < N; ++i) branchBounds(br[i], bmn[i], bmx[i]);

    /* index array for partitioning */
    std::vector<uint32_t> idx(N);
    for (uint32_t i = 0; i < N; ++i) idx[i] = i;

    /* build recursively – root at index 0 */
    buildNode(out, br, bmn, bmx, idx, 0, (uint32_t)N);
    return out;
}

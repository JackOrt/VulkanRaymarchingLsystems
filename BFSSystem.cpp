#include "BFSSystem.hpp"
#include <random>
#include <cmath>

/**
 * generateRandomBFSSystem:
 *  Creates a random BFS tree of branches (CPUBranch).
 *  Each child references its parent's index in parentIndex.
 */
std::vector<CPUBranch> generateRandomBFSSystem()
{
    std::vector<CPUBranch> results;
    results.reserve(1024);

    static std::random_device rd;
    static std::mt19937 rng(rd());
    std::uniform_real_distribution<float> angleDist(0.3f, 1.0f);
    std::uniform_real_distribution<float> signDist(0.f, 1.f);
    std::uniform_real_distribution<float> branchProb(0.f, 1.f);

    struct Node {
        float sx, sy, sz;
        float ex, ey, ez;
        float radius;
        float bfsD;
        int parentId;
        int depth;
    };
    std::vector<Node> stack;
    stack.reserve(512);

    // trunk BFS=0, no parent => parentId=-1
    stack.push_back({
        0.f, -1.f, 0.f,    // start
        0.f,  0.f, 0.f,    // end
        0.06f,             // radius
        0.f,               // BFS depth
        -1,                // parentId
        5                  // recursion depth
        });

    auto rotZ = [&](float& x, float& y, float a) {
        float c = cos(a), s = sin(a);
        float rx = c * x - s * y;
        float ry = s * x + c * y;
        x = rx; y = ry;
        };
    auto rotX = [&](float& y, float& z, float a) {
        float c = cos(a), s = sin(a);
        float ry = c * y - s * z;
        float rz = s * y + c * z;
        y = ry; z = rz;
        };

    while (!stack.empty())
    {
        auto n = stack.back();
        stack.pop_back();

        // Insert this node as a CPUBranch in results:
        CPUBranch br{};
        br.startX = n.sx;
        br.startY = n.sy;
        br.startZ = n.sz;
        br.endX = n.ex;
        br.endY = n.ey;
        br.endZ = n.ez;
        br.radius = n.radius;
        br.bfsDepth = n.bfsD;
        br.parentIndex = n.parentId;

        int thisIndex = (int)results.size();
        results.push_back(br);

        // Possibly spawn children:
        if (n.depth > 1)
        {
            float p = branchProb(rng);
            int childCount = (p < 0.3f) ? 1 : 2;

            float vx = n.ex - n.sx;
            float vy = n.ey - n.sy;
            float vz = n.ez - n.sz;
            float length = std::sqrt(vx * vx + vy * vy + vz * vz);
            if (length < 1e-6f) continue;
            vx /= length; vy /= length; vz /= length;

            float childLen = 0.8f * length;
            float newRad = n.radius * 0.7f;

            for (int i = 0; i < childCount; i++)
            {
                float a1 = angleDist(rng);
                float a2 = angleDist(rng);
                if (signDist(rng) > 0.5f) a1 = -a1;
                if (signDist(rng) > 0.5f) a2 = -a2;

                float cx = vx, cy = vy, cz = vz;
                rotZ(cx, cy, a1);
                rotX(cy, cz, a2);

                Node child;
                child.sx = n.ex;
                child.sy = n.ey;
                child.sz = n.ez;
                child.ex = n.ex + cx * childLen;
                child.ey = n.ey + cy * childLen;
                child.ez = n.ez + cz * childLen;
                child.radius = newRad;
                child.bfsD = n.bfsD + 1.f;
                child.parentId = thisIndex;
                child.depth = n.depth - 1;

                stack.push_back(child);
            }
        }
    }

    return results;
}

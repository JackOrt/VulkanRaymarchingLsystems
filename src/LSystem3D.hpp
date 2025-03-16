#pragma once

#include "CommonHeader.hpp"
#include <string>
#include <vector>
#include <stack>
#include <unordered_map>
#include <random>
#include <glm/glm.hpp>

/// A vertex containing position and normal (for mesh usage)
struct MeshVertex {
    glm::vec3 pos;
    glm::vec3 normal;
};

class LSystem3D {
public:
    LSystem3D();

    // L-System definitions
    void setAxiom(const std::string& axiom);
    void addRule(char predecessor, const std::string& successor);
    void evolve();
    void commitGrowth();

    // Set the current growth interpolation factor (0.0 to 1.0)
    void setGrowthFactor(float f) { growthFactor = f; }

    // Generate a 3D mesh (using cylinders around each segment)
    std::vector<MeshVertex> generateMesh(float angleDeg,
        float baseRadius,
        float segmentLength,
        int segmentsPerCircle = 8,
        float minRadius = 0.01f,
        bool doCapEnds = true,
        float radiusDecayBranch = 0.7f);

    // Generate a debug skeleton as a list of line segment endpoints.
    // Every two consecutive glm::vec3 entries define one line.
    std::vector<glm::vec3> generateSkeleton();

private:
    std::string currentString;
    std::string nextString;
    float growthFactor; // Interpolation factor (0.0 to 1.0)
    std::unordered_map<char, std::string> rules;
    std::mt19937 rng;
};

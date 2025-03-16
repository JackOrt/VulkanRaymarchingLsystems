#include "LSystem3D.hpp"
#include <glm/gtx/quaternion.hpp>
#include <cmath>
#include <algorithm>

LSystem3D::LSystem3D()
    : currentString("F"), nextString("F"), growthFactor(0.0f)
{
    std::random_device rd;
    rng.seed(rd());
}

void LSystem3D::setAxiom(const std::string& axiom) {
    currentString = axiom;
    nextString = axiom;
}

void LSystem3D::addRule(char predecessor, const std::string& successor) {
    rules[predecessor] = successor;
}

void LSystem3D::evolve() {
    std::string next;
    for (char c : currentString) {
        if (rules.find(c) != rules.end())
            next += rules[c];
        else
            next += c;
    }
    nextString = next;
    // Reset the interpolation factor so that new segments grow from 0 to 1.
    growthFactor = 0.0f;
}

void LSystem3D::commitGrowth() {
    // When growthFactor reaches 1, update the current string.
    if (growthFactor >= 1.0f) {
        currentString = nextString;
    }
}

std::vector<MeshVertex> LSystem3D::generateMesh(float angleDeg,
    float baseRadius,
    float segmentLength,
    int segmentsPerCircle,
    float minRadius,
    bool doCapEnds,
    float radiusDecayBranch)
{
    struct Turtle {
        glm::vec3 pos;
        glm::quat orient;
        float radius;
    };

    std::vector<MeshVertex> mesh;
    std::stack<Turtle> stack;
    Turtle turtle{ glm::vec3(0.0f), glm::quat(1, 0, 0, 0), baseRadius };
    float rad = glm::radians(angleDeg);

    for (char c : currentString) {
        if (c == 'F') {
            glm::vec3 start = turtle.pos;
            glm::vec3 forward = turtle.orient * glm::vec3(0, 1, 0);
            // Multiply segment length by growthFactor for smooth growth
            glm::vec3 end = start + forward * segmentLength * growthFactor;

            float rStart = turtle.radius;
            float rEnd = std::max(minRadius, rStart * radiusDecayBranch);

            int slices = segmentsPerCircle;
            for (int i = 0; i < slices; i++) {
                float theta0 = (i / (float)slices) * 2.0f * glm::pi<float>();
                float theta1 = ((i + 1) / (float)slices) * 2.0f * glm::pi<float>();

                glm::vec3 p0 = start + turtle.orient * glm::vec3(rStart * cos(theta0), 0, rStart * sin(theta0));
                glm::vec3 p1 = start + turtle.orient * glm::vec3(rStart * cos(theta1), 0, rStart * sin(theta1));
                glm::vec3 p2 = end + turtle.orient * glm::vec3(rEnd * cos(theta0), 0, rEnd * sin(theta0));
                glm::vec3 p3 = end + turtle.orient * glm::vec3(rEnd * cos(theta1), 0, rEnd * sin(theta1));

                glm::vec3 n0 = glm::normalize(p0 - start);
                glm::vec3 n1 = glm::normalize(p1 - start);
                glm::vec3 n2 = glm::normalize(p2 - end);
                glm::vec3 n3 = glm::normalize(p3 - end);

                mesh.push_back({ p0, n0 });
                mesh.push_back({ p1, n1 });
                mesh.push_back({ p2, n2 });

                mesh.push_back({ p1, n1 });
                mesh.push_back({ p3, n3 });
                mesh.push_back({ p2, n2 });
            }

            turtle.pos = end;
            turtle.radius = rEnd;
        }
        else if (c == '+') {
            turtle.orient = glm::angleAxis(rad, glm::vec3(0, 0, 1)) * turtle.orient;
        }
        else if (c == '-') {
            turtle.orient = glm::angleAxis(-rad, glm::vec3(0, 0, 1)) * turtle.orient;
        }
        else if (c == '[') {
            stack.push(turtle);
        }
        else if (c == ']') {
            if (!stack.empty()) {
                turtle = stack.top();
                stack.pop();
            }
        }
    }
    return mesh;
}

std::vector<glm::vec3> LSystem3D::generateSkeleton() {
    std::vector<glm::vec3> lineVertices;
    struct Turtle {
        glm::vec3 pos;
        glm::quat orient;
    };
    Turtle turtle{ glm::vec3(0.0f), glm::quat(1, 0, 0, 0) };
    std::stack<Turtle> stack;
    float length = 0.1f;
    float angleDeg = 25.0f;
    float rad = glm::radians(angleDeg);

    for (char c : currentString) {
        if (c == 'F') {
            glm::vec3 start = turtle.pos;
            glm::vec3 forward = turtle.orient * glm::vec3(0, 1, 0);
            // Multiply the segment length by growthFactor
            glm::vec3 end = start + forward * length * growthFactor;
            lineVertices.push_back(start);
            lineVertices.push_back(end);
            turtle.pos = end;
        }
        else if (c == '+') {
            turtle.orient = glm::angleAxis(rad, glm::vec3(0, 0, 1)) * turtle.orient;
        }
        else if (c == '-') {
            turtle.orient = glm::angleAxis(-rad, glm::vec3(0, 0, 1)) * turtle.orient;
        }
        else if (c == '[') {
            stack.push(turtle);
        }
        else if (c == ']') {
            if (!stack.empty()) {
                turtle = stack.top();
                stack.pop();
            }
        }
    }
    return lineVertices;
}

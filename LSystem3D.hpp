#pragma once
/*???????????????????????????????????????????????????????????????????????????*/
/*  Dependencies                                                             */
/*???????????????????????????????????????????????????????????????????????????*/
#include "BFSSystem.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

/*???????????????????????????????????????????????????????????????????????????*/
/*  Symbol & rule data structures                                            */
/*???????????????????????????????????????????????????????????????????????????*/
struct Symbol {
    char               name;              // e.g. 'F'  '+'  '&'
    std::vector<float> params;            // may be empty
};

struct OutputSymbol {
    char                     name;
    std::vector<std::string> paramExprs;   // kept as raw strings
};

struct ParametricRule {
    char                      headName;    // e.g. 'A'
    std::vector<std::string>  headParams;  // ["d","s"]
    std::string               condition;   // "" ? always
    std::vector<OutputSymbol> successor;   // RHS symbols
};

/*???????????????????????????????????????????????????????????????????????????*/
/*  A full preset (= one �species�)                                          */
/*???????????????????????????????????????????????????????????????????????????*/
struct LSystemPreset
{
    /* ?? core L?system data ??????????????????????????????????????? */
    std::vector<Symbol>         axiom;
    std::vector<ParametricRule> rules;
    int   iterations = 6;      // expand() passes
    float baseRad = 0.04f;  // trunk radius scale

    /* ?? organic variation knobs (all optional / ranged) ????????? */
    bool  medialAxis = false;      // run medial?axis radii pass
    float radiusScaleMin = 1.0f;       // thickness noise (global)
    float radiusScaleMax = 1.0f;
    float depthTaperMin = 0.65f;      // depth?based taper
    float depthTaperMax = 0.65f;

    /* feature 1:   per?turn angle jitter (deg) */
    float angJitMinDeg = 0.0f;
    float angJitMaxDeg = 0.0f;

    /* feature 2:   length jitter multiplier (1�x�%) */
    float lenJitMinMul = 1.0f;
    float lenJitMaxMul = 1.0f;

    /* feature 3:   tropism strength 0..1  (towards +Y)              */
    float tropism = 0.0f;

    /* feature 7:   trunk wander � small random yaw/pitch per step   */
    float wanderMinDeg = 0.0f;
    float wanderMaxDeg = 0.0f;

    /* internal � set by loader when we inject randomness automatically */
    bool  autoRandomise = false;
};

/*???????????????????????????????????????????????????????????????????????????*/
/*  Public API                                                               */
/*???????????????????????????????????????????????????????????????????????????*/
std::vector<std::pair<std::string, LSystemPreset>>
loadParametricPresets(bool injectRandom = true);

std::vector<CPUBranch>  generateLSystem(const LSystemPreset&);

LSystemPreset           crossbreed(const LSystemPreset& A,
    const LSystemPreset& B,
    float alpha = 0.5f,
    uint32_t seed = 0xDEADBEEF);

LSystemPreset           randomHybrid(const std::vector<LSystemPreset>& pool,
    float alpha = 0.5f,
    uint32_t seed = 0);

void computeMedialAxisRadii(std::vector<CPUBranch>& br);

/*???????????????????????????????????????????????????????????????????????????*/
/*  Optional console helper                                                  */
/*???????????????????????????????????????????????????????????????????????????*/
#include <iomanip>
#include <iostream>

inline void debugPrintPreset(const std::string& title,
    const LSystemPreset& P)
{
    using std::cout;
    cout << "\n??????????????????????????????????????????????\n"
        << "Generating plant : " << title << '\n'
        << "Iterations       : " << P.iterations << '\n'
        << "Base radius      : " << P.baseRad << '\n'
        << "Medial axis      : " << (P.medialAxis ? "on" : "off") << '\n'
        << "Tropism          : " << P.tropism << '\n'
        << "Angle jitter     : [" << P.angJitMinDeg << ',' << P.angJitMaxDeg << "]�\n"
        << "Length jitter    : [" << P.lenJitMinMul << ',' << P.lenJitMaxMul << "]�\n"
        << "Radius noise     : [" << P.radiusScaleMin << ',' << P.radiusScaleMax << "]�\n"
        << "Depth taper      : [" << P.depthTaperMin << ',' << P.depthTaperMax << "]�\n"
        << "Trunk wander     : [" << P.wanderMinDeg << ',' << P.wanderMaxDeg << "]�\n"
        << "Axiom            : ";
    for (auto& s : P.axiom) cout << s.name << ' ';
    cout << "\nRules            : " << P.rules.size() << "\n";
    for (size_t i = 0; i < P.rules.size(); ++i) {
        const auto& r = P.rules[i];
        cout << "  [" << std::setw(2) << i << "] " << r.headName << '(';
        for (size_t k = 0; k < r.headParams.size(); ++k) {
            cout << r.headParams[k] << (k + 1 < r.headParams.size() ? ',' : ')');
        }
        cout << " -> ";
        for (auto& s2 : r.successor) cout << s2.name << ' ';
        cout << '\n';
    }
    cout << "??????????????????????????????????????????????\n";
}

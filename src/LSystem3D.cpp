/*  LSystem3D.cpp  – parametric L?system implementation with
 *  organic variation features 1?7  +  R?1 / R?2 / R?3
 *  Author: 2025
 *?????????????????????????????????????????????????????????????????????????*/
#include "LSystem3D.hpp"
#include "BFSSystem.hpp"

#include <nlohmann/json.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/rotate_vector.hpp>

#include <stack>
#include <sstream>
#include <fstream>
#include <random>
#include <cmath>
#include <cctype>
#include <memory>
#include <stdexcept>

using json = nlohmann::json;

/*=========================================================================*/
/*  RNG helpers                                                            */
/*=========================================================================*/
static std::mt19937& rng(uint32_t seed = 0)
{
    static thread_local std::mt19937 g{ 12345 };
    if (seed) g.seed(seed);
    return g;
}
inline float urand(float a = 0.f, float b = 1.f)
{
    return std::uniform_real_distribution<float>(a, b)(rng());
}

/*=========================================================================*/
/*  Tiny expression evaluator (unchanged)                                  */
/*=========================================================================*/
struct Expr {
    enum Kind { VAL, VAR, ADD, SUB, MUL, DIV }kind;
    float val{}; std::string var; Expr* left = nullptr, * right = nullptr;
    Expr(float v) :kind(VAL), val(v) {} Expr(const std::string& v) :kind(VAR), var(v) {}
    Expr(Kind k, Expr* l, Expr* r) :kind(k), left(l), right(r) {}
    float eval(const std::unordered_map<std::string, float>& env)const {
        switch (kind) {
        case VAL: return val;
        case VAR: {
            auto it = env.find(var); if (it == env.end())
                throw std::runtime_error("unknown var " + var); return it->second;
        }
        case ADD: return left->eval(env) + right->eval(env);
        case SUB: return left->eval(env) - right->eval(env);
        case MUL: return left->eval(env) * right->eval(env);
        case DIV: return left->eval(env) / right->eval(env);
        }
        return 0.f;
    }
};

static const char* pExpr;
static inline void skipWS()
{
    if (!pExpr)            // << NEW : bail out when parser
        return;            //          already marked “finished”
    while (*pExpr == ' ' || *pExpr == '\t')
        ++pExpr;
}static Expr* parseE();
static Expr* parseNumberOrVar() {
    skipWS();
    if (std::isalpha(*pExpr)) {
        std::string v; while (std::isalnum(*pExpr) || *pExpr == '_')v.push_back(*pExpr++);
        return new Expr(v);
    }
    else {
        char* end; float f = std::strtof(pExpr, &end); pExpr = end; return new Expr(f);
    }
}
static Expr* parseFactor() {
    skipWS();
    if (*pExpr == '(') { ++pExpr; Expr* e = parseE(); skipWS(); if (*pExpr == ')')++pExpr; return e; }
    return parseNumberOrVar();
}
static Expr* parseTerm() {
    Expr* l = parseFactor();
    while (true) {
        skipWS();
        if (*pExpr == '*') { ++pExpr; l = new Expr(Expr::MUL, l, parseFactor()); }
        else if (*pExpr == '/') { ++pExpr; l = new Expr(Expr::DIV, l, parseFactor()); }
        else break;
    }
    return l;
}
static Expr* parseE() {
    Expr* l = parseTerm();
    while (true) {
        skipWS();
        if (*pExpr == '+') { ++pExpr; l = new Expr(Expr::ADD, l, parseTerm()); }
        else if (*pExpr == '-') { ++pExpr; l = new Expr(Expr::SUB, l, parseTerm()); }
        else break;
    }
    return l;
}
static Expr* compileExpr(const std::string& s) { pExpr = s.c_str(); return parseE(); }

/*=========================================================================*/
/*  Tokeniser for symbol strings                                           */
/*=========================================================================*/
static bool isSymChar(char c) { return std::isalpha(c) || strchr("+-&^/\\|[]", c); }

static std::vector<Symbol> tokenize(const std::string& str)
{
    std::vector<Symbol> out; const char* p = str.c_str();
    auto eatWS = [&] {while (*p == ' ' || *p == '\t')++p; };
    while (*p) {
        if (isSymChar(*p)) {
            Symbol S{ *p++,{} };
            eatWS();
            if (*p == '(') {
                ++p; std::string num;
                while (*p && *p != ')') {
                    if (*p == ',') { S.params.push_back(std::stof(num)); num.clear(); ++p; }
                    else num.push_back(*p++);
                }
                if (!num.empty())S.params.push_back(std::stof(num));
                if (*p == ')')++p;
            }
            out.push_back(std::move(S));
        }
        else ++p;
    }
    return out;
}

/*=========================================================================*/
/*  Single expansion pass  (feature 4 : probabilistic pruning)             */
/*=========================================================================*/
static std::vector<Symbol> expandOnce(const std::vector<Symbol>& cur,
    const std::vector<ParametricRule>& rules)
{
    std::vector<Symbol> next;
    int depth = 0;                 // bracket?depth for pruning
    for (size_t idx = 0; idx < cur.size(); ++idx)
    {
        const auto& sym = cur[idx];
        if (sym.name == '[') { ++depth; next.push_back(sym); continue; }
        if (sym.name == ']') { --depth; next.push_back(sym); continue; }

        bool applied = false;
        for (const auto& R : rules)
        {
            if (R.headName != sym.name) continue;
            if (sym.params.size() != R.headParams.size()) continue;

            /* probabilistic pruning (feature?4) – the deeper we are,
               the higher the chance we drop this rule altogether.   */
            float pruneP = 0.03f * std::max(0, depth - 2);
            if (pruneP > 0 && urand() < pruneP) { applied = true; break; }

            std::unordered_map<std::string, float> env;
            for (size_t i = 0; i < sym.params.size(); ++i) env[R.headParams[i]] = sym.params[i];
            bool ok = true;
            if (!R.condition.empty()) {
                std::unique_ptr<Expr> cond(compileExpr(R.condition));
                ok = cond->eval(env) > 0.f;
            }
            if (!ok) continue;

            for (const auto& os : R.successor) {
                Symbol o{ os.name,{} };
                for (const auto& ex : os.paramExprs) {
                    std::unique_ptr<Expr> e(compileExpr(ex));
                    o.params.push_back(e->eval(env));
                }
                next.push_back(std::move(o));
            }
            applied = true; break;
        }
        if (!applied) next.push_back(sym);
    }
    return next;
}

/*=========================================================================*/
/*  generateLSystem  –  turtle + variation 1?3,5?7                         */
/*=========================================================================*/
std::vector<CPUBranch> generateLSystem(const LSystemPreset& P)
{
    /* stochastic knobs (R?1/2/3) ------------------------------------- */
    bool  useMedial = P.medialAxis;
    float thickScale = urand(P.radiusScaleMin, P.radiusScaleMax);
    float taperFactor = urand(P.depthTaperMin, P.depthTaperMax);

    if (P.autoRandomise) {
        useMedial = urand() < 0.5f;
        thickScale = urand(P.radiusScaleMin, P.radiusScaleMax);
        taperFactor = urand(P.depthTaperMin, P.depthTaperMax);
    }

    /* 1) expand ------------------------------------------------------- */
    std::vector<Symbol> cur = P.axiom;
    for (int i = 0; i < P.iterations; ++i) cur = expandOnce(cur, P.rules);

    /* 2) turtle pass -------------------------------------------------- */
    struct Turtle { glm::vec3 p, d, u; int parent; };
    std::stack<Turtle> st;
    st.push({ {0,-1,0},{0,1,0},{0,0,1},-1 });
    std::vector<CPUBranch> out;

    auto rot = [&](glm::vec3 v, float a, const glm::vec3& ax) {
        return glm::vec3(glm::rotate(glm::mat4(1), a, ax) * glm::vec4(v, 0)); };

    /* trunk wander initial heading (feature?7) */
    float initYaw = glm::radians(urand(P.wanderMinDeg, P.wanderMaxDeg));
    float initPitch = glm::radians(urand(P.wanderMinDeg, P.wanderMaxDeg));
    st.top().d = rot(st.top().d, initYaw, glm::vec3(0, 0, 1));
    st.top().d = rot(st.top().d, initPitch, glm::vec3(1, 0, 0));

    for (const auto& S : cur)
    {
        switch (S.name)
        {
        case 'F': {
            /* feature?2 : length jitter */
            float len = S.params.empty() ? 1.f : S.params[0];
            len *= urand(P.lenJitMinMul, P.lenJitMaxMul);

            /* small incremental wander each step (feature?7) */
            float wYaw = glm::radians(urand(P.wanderMinDeg, P.wanderMaxDeg));
            float wPit = glm::radians(urand(P.wanderMinDeg, P.wanderMaxDeg));
            st.top().d = rot(st.top().d, wYaw, glm::vec3(0, 0, 1));
            st.top().d = rot(st.top().d, wPit, glm::vec3(1, 0, 0));

            glm::vec3 a = st.top().p;
            glm::vec3 b = a + st.top().d * len;

            int depth = (st.top().parent < 0 ? 0 : out[st.top().parent].bfsDepth + 1);

            CPUBranch br{};
            br.startX = a.x; br.startY = a.y; br.startZ = a.z;
            br.endX = b.x; br.endY = b.y; br.endZ = b.z;
            br.bfsDepth = depth;
            br.parentIndex = st.top().parent;
            br.radius = len * P.baseRad * thickScale *
                std::pow(taperFactor, depth); /* features 5 & 6 */

            out.push_back(br);
            st.top().parent = int(out.size()) - 1;
            st.top().p = b;

            /* feature?3 : tropism (bend direction toward +Y world up) */
            if (P.tropism > 0.f) {
                glm::vec3 up(0, 1, 0);
                st.top().d = glm::normalize(glm::mix(st.top().d, up, P.tropism));
            }
        }break;

        case '+': case '-': {
            float ang = glm::radians(S.params[0]);
            if (S.name == '-') ang = -ang;
            /* feature?1 : angle jitter */
            ang += glm::radians(urand(P.angJitMinDeg, P.angJitMaxDeg));
            st.top().d = rot(st.top().d, ang, st.top().u);
        }break;

        case '&': case '^': {
            float ang = glm::radians(S.params[0]);
            if (S.name == '^') ang = -ang;
            ang += glm::radians(urand(P.angJitMinDeg, P.angJitMaxDeg));
            glm::vec3 L = glm::normalize(glm::cross(st.top().u, st.top().d));
            st.top().d = rot(st.top().d, ang, L);
            st.top().u = rot(st.top().u, ang, L);
        }break;

        case '[': st.push(st.top()); break;
        case ']': st.pop();          break;
        default: break;
        }
    }

    /* 3) optional medial?axis radii (R?1) ---------------------------- */
    if (useMedial) {
        computeMedialAxisRadii(out);
        for (auto& b : out) b.radius *= thickScale;   // keep noise
    }
    return out;
}

/*=========================================================================*/
/*  JSON loader  (injects random ranges if requested)                      */
/*=========================================================================*/
static std::vector<std::string> splitList(std::string_view s) {
    std::vector<std::string> v; std::string cur;
    for (char c : s) { if (c == ',') { v.push_back(std::move(cur)); cur.clear(); } else cur.push_back(c); }
    if (!cur.empty()) v.push_back(std::move(cur)); return v;
}

std::vector<std::pair<std::string, LSystemPreset>>
loadParametricPresets(bool injectRandom)
{
    std::ifstream in("presets.json");
    if (!in) throw std::runtime_error("Cannot open presets.json");
    json root; in >> root;
    std::vector<std::pair<std::string, LSystemPreset>> presets;

    auto arr2f = [&](const json& j, float& x, float& y) {x = j[0]; y = j[1]; };

    for (const json& E : root)
    {
        LSystemPreset P; std::string name = E.at("name");

        P.axiom = tokenize(E.at("axiom").get<std::string>());

        /* rules ------------------------------------------------------ */
        for (const json& RJ : E.at("rules")) {
            ParametricRule R;
            std::string head = RJ.at("head");
            size_t lp = head.find('('); R.headName = head[0];
            if (lp != std::string::npos) {
                size_t rp = head.find(')', lp);
                R.headParams = splitList(head.substr(lp + 1, rp - lp - 1));
            }
            if (RJ.contains("condition")) R.condition = RJ["condition"];

            for (const std::string& succStr : RJ.at("succ")) {
                const char* p = succStr.c_str();
                while (*p) {
                    if (isSymChar(*p)) {
                        OutputSymbol O; O.name = *p++;
                        skipWS();
                        if (*p == '(') {
                            ++p; std::string expr;
                            while (*p && *p != ')') { expr.push_back(*p++); }
                            if (*p == ')') ++p;
                            O.paramExprs = splitList(expr);
                        }
                        R.successor.push_back(std::move(O));
                    }
                    else ++p;
                }
            }
            P.rules.push_back(std::move(R));
        }

        /* optional organic fields directly in JSON ------------------- */
        if (E.contains("medialAxis"))        P.medialAxis = E["medialAxis"];
        if (E.contains("tropism"))        P.tropism = E["tropism"];
        if (E.contains("angleJitDeg"))       arr2f(E["angleJitDeg"], P.angJitMinDeg, P.angJitMaxDeg);
        if (E.contains("lengthJitMul"))      arr2f(E["lengthJitMul"], P.lenJitMinMul, P.lenJitMaxMul);
        if (E.contains("wanderDeg"))         arr2f(E["wanderDeg"], P.wanderMinDeg, P.wanderMaxDeg);
        if (E.contains("radiusScaleRange"))  arr2f(E["radiusScaleRange"], P.radiusScaleMin, P.radiusScaleMax);
        if (E.contains("depthTaperRange"))   arr2f(E["depthTaperRange"], P.depthTaperMin, P.depthTaperMax);

        /* automatic injection ---------------------------------------- */
        if (injectRandom && !E.contains("radiusScaleRange")) {
            P.autoRandomise = true;
            P.radiusScaleMin = 0.80f; P.radiusScaleMax = 1.25f;
            P.depthTaperMin = 0.55f; P.depthTaperMax = 0.75f;
            P.angJitMinDeg = 0.0f;  P.angJitMaxDeg = 7.5f;
            P.lenJitMinMul = 0.90f; P.lenJitMaxMul = 1.10f;
            P.tropism = 0.08f;
            P.wanderMinDeg = -50.0f; P.wanderMaxDeg = 50.0f;
        }

        debugPrintPreset(name, P);
        presets.emplace_back(name, std::move(P));
    }
    return presets;
}

/*=========================================================================*/
/*  Cross?breeding utilities                                               */
/*=========================================================================*/
LSystemPreset crossbreed(const LSystemPreset& A, const LSystemPreset& B,
    float alpha, uint32_t seed)
{
    std::mt19937& R = rng(seed);
    LSystemPreset H;

    H.iterations = int(std::round((1 - alpha) * A.iterations + alpha * B.iterations));
    H.baseRad = (1 - alpha) * A.baseRad + alpha * B.baseRad;

    H.axiom = (std::uniform_real_distribution<float>(0, 1)(R) < 0.5f) ? A.axiom : B.axiom;
    H.rules = A.rules; H.rules.insert(H.rules.end(), B.rules.begin(), B.rules.end());
    std::shuffle(H.rules.begin(), H.rules.end(), R);
    if (!H.rules.empty()) { size_t keep = H.rules.size() * 7 / 10; H.rules.resize(std::max<size_t>(1, keep)); }

    /* blend variation knobs linearly ---------------------------------- */
    auto lerp = [&](float a, float b) {return (1 - alpha) * a + alpha * b; };
    H.radiusScaleMin = lerp(A.radiusScaleMin, B.radiusScaleMin);
    H.radiusScaleMax = lerp(A.radiusScaleMax, B.radiusScaleMax);
    H.depthTaperMin = lerp(A.depthTaperMin, B.depthTaperMin);
    H.depthTaperMax = lerp(A.depthTaperMax, B.depthTaperMax);
    H.angJitMinDeg = lerp(A.angJitMinDeg, B.angJitMinDeg);
    H.angJitMaxDeg = lerp(A.angJitMaxDeg, B.angJitMaxDeg);
    H.lenJitMinMul = lerp(A.lenJitMinMul, B.lenJitMinMul);
    H.lenJitMaxMul = lerp(A.lenJitMaxMul, B.lenJitMaxMul);
    H.tropism = lerp(A.tropism, B.tropism);
    H.wanderMinDeg = lerp(A.wanderMinDeg, B.wanderMinDeg);
    H.wanderMaxDeg = lerp(A.wanderMaxDeg, B.wanderMaxDeg);
    H.medialAxis = (alpha < 0.5f) ? A.medialAxis : B.medialAxis;

    return H;
}

LSystemPreset randomHybrid(const std::vector<LSystemPreset>& pool,
    float alpha, uint32_t seed)
{
    if (pool.size() < 2) throw std::runtime_error("Need ?2 parents");
    std::mt19937& R = rng(seed);
    std::uniform_int_distribution<size_t>pick(0, pool.size() - 1);
    size_t i = pick(R), j; do { j = pick(R); } while (j == i);
    return crossbreed(pool[i], pool[j], alpha, R());
}

/*=========================================================================*/
/*  Medial?axis radius post?pass                                           */
/*=========================================================================*/
void computeMedialAxisRadii(std::vector<CPUBranch>& br)
{
    const size_t N = br.size();
    std::vector<std::vector<uint32_t>> children(N);
    for (uint32_t i = 0; i < N; ++i)
        if (br[i].parentIndex >= 0)
            children[br[i].parentIndex].push_back(i);

    std::vector<float> far(N, 0.f), tmp; tmp.reserve(N);

    for (uint32_t root = 0; root < N; ++root) {
        tmp.clear(); tmp.push_back(root);
        while (!tmp.empty()) {
            uint32_t idx = tmp.back(); tmp.pop_back();
            float dx = br[idx].endX - br[root].startX;
            float dy = br[idx].endY - br[root].startY;
            float dz = br[idx].endZ - br[root].startZ;
            far[root] = std::max(far[root], std::sqrt(dx * dx + dy * dy + dz * dz));
            for (uint32_t c : children[idx]) tmp.push_back(c);
        }
    }
    const float k = 1e-8f;
    for (uint32_t i = 0; i < N; ++i) br[i].radius = far[i] * k;
}

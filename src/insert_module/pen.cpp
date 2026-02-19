#include <cmath>
#include <utility>
#include <set>
#include <unordered_set>
#include <map>
#include <vector>

#include "constants.h"
#include "interface_for_py.h"
#include "leaf_graph.h"

// LLVM CmpInst Predicates
enum Predicate {
  ICMP_EQ = 32, ICMP_NE = 33, ICMP_UGT = 34, ICMP_UGE = 35, ICMP_ULT = 36, ICMP_ULE = 37,
  ICMP_SGT = 38, ICMP_SGE = 39, ICMP_SLT = 40, ICMP_SLE = 41,
  FCMP_FALSE = 0, FCMP_OEQ = 1, FCMP_OGT = 2, FCMP_OGE = 3, FCMP_OLT = 4, FCMP_OLE = 5,
  FCMP_ONE = 6, FCMP_ORD = 7, FCMP_UNO = 8, FCMP_UEQ = 9, FCMP_UGT = 10, FCMP_UGE = 11,
  FCMP_ULT = 12, FCMP_ULE = 13, FCMP_UNE = 14, FCMP_TRUE = 15
};

static inline bool getTruth(double LHS, double RHS, int cmpId) {
    bool isNan = std::isnan(LHS) || std::isnan(RHS);
    switch (cmpId) {
        case FCMP_FALSE: return false;
        case FCMP_TRUE:  return true;
        case ICMP_EQ: return LHS == RHS;
        case FCMP_OEQ: return !isNan && (LHS == RHS);
        case FCMP_UEQ: return isNan || (LHS == RHS);
        case ICMP_NE: return LHS != RHS;
        case FCMP_ONE: return !isNan && (LHS != RHS);
        case FCMP_UNE: return isNan || (LHS != RHS);
        case ICMP_SGT: case ICMP_UGT: return LHS > RHS;
        case FCMP_OGT: return !isNan && (LHS > RHS);
        case FCMP_UGT: return isNan || (LHS > RHS);
        case ICMP_SGE: case ICMP_UGE: return LHS >= RHS;
        case FCMP_OGE: return !isNan && (LHS >= RHS);
        case FCMP_UGE: return isNan || (LHS >= RHS);
        case ICMP_SLT: case ICMP_ULT: return LHS < RHS;
        case FCMP_OLT: return !isNan && (LHS < RHS);
        case FCMP_ULT: return isNan || (LHS < RHS);
        case ICMP_SLE: case ICMP_ULE: return LHS <= RHS;
        case FCMP_OLE: return !isNan && (LHS <= RHS);
        case FCMP_ULE: return isNan || (LHS <= RHS);
        case FCMP_ORD: return !isNan;
        case FCMP_UNO: return isNan;
        default: return false;
    }
}

static inline double calculate_distance(double LHS, double RHS, int cmpId, bool currentTruth, bool targetTruth, bool isSelf) {
    // 情况 1: 不满足目标条件 -> 返回正值（距离/惩罚）
    if (currentTruth != targetTruth) {
        bool isNan = std::isnan(LHS) || std::isnan(RHS);
        bool isInf = std::isinf(LHS) || std::isinf(RHS);

        // 处理 NaN/Inf 的惩罚
        if ((isNan || isInf) && (cmpId != FCMP_ORD && cmpId != FCMP_UNO)) {
            return CANNOT_CMP_PENALTY;
        }

        switch (cmpId) {
            case FCMP_FALSE: return targetTruth ? CANNOT_CMP_PENALTY : 0.0;
            case FCMP_TRUE:  return targetTruth ? 0.0 : CANNOT_CMP_PENALTY;

            case ICMP_EQ: case FCMP_OEQ: case FCMP_UEQ:
                // 当前为 !=, 目标为 == -> 距离 abs; 当前为 ==, 目标为 != -> 距离 EPS
                return targetTruth ? std::abs(LHS - RHS) : EPS;
            
            case ICMP_NE: case FCMP_ONE: case FCMP_UNE:
                // 当前为 ==, 目标为 != -> 距离 EPS; 当前为 !=, 目标为 == -> 距离 abs
                return targetTruth ? EPS : std::abs(LHS - RHS);

            case ICMP_SGT: case ICMP_UGT: case FCMP_OGT: case FCMP_UGT:
                // 目标 > (T): 距离 RHS-LHS+EPS; 目标 <= (F): 距离 LHS-RHS
                return targetTruth ? (RHS - LHS + EPS) : (LHS - RHS);

            case ICMP_SGE: case ICMP_UGE: case FCMP_OGE: case FCMP_UGE:
                // 目标 >= (T): 距离 RHS-LHS; 目标 < (F): 距离 LHS-RHS+EPS
                return targetTruth ? (RHS - LHS) : (LHS - RHS + EPS);

            case ICMP_SLT: case ICMP_ULT: case FCMP_OLT: case FCMP_ULT:
                // 目标 < (T): 距离 LHS-RHS+EPS; 目标 >= (F): 距离 RHS-LHS
                return targetTruth ? (LHS - RHS + EPS) : (RHS - LHS);

            case ICMP_SLE: case ICMP_ULE: case FCMP_OLE: case FCMP_ULE:
                // 目标 <= (T): 距离 LHS-RHS; 目标 > (F): 距离 RHS-LHS+EPS
                return targetTruth ? (LHS - RHS) : (RHS - LHS + EPS);

            case FCMP_ORD: case FCMP_UNO:
                return CANNOT_CMP_PENALTY;

            default: return CANNOT_CMP_PENALTY;
        }
    }

    // 情况 2: 满足目标条件
    if (isSelf) {
        return 0.0;
    } else {
        // 安全模式 (isSelf=false) -> 返回负值（安全性），返回值作为分母的时候注意除0的处理
        bool isNan = std::isnan(LHS) || std::isnan(RHS);
        bool isInf = std::isinf(LHS) || std::isinf(RHS);

        if (cmpId == FCMP_FALSE || cmpId == FCMP_TRUE || cmpId == FCMP_ORD || cmpId == FCMP_UNO || 
            ((isNan || isInf) && (cmpId != FCMP_ORD && cmpId != FCMP_UNO))) {
            return -1.0;
        }

        switch (cmpId) {
            case ICMP_EQ: case FCMP_OEQ: case FCMP_UEQ:
                // 目标 == (T): 仅一点满足，无安全性余量; 目标 != (F): 边界距离 abs-EPS
                return targetTruth ? 0.0 : -(std::abs(LHS - RHS) - EPS);
            
            case ICMP_NE: case FCMP_ONE: case FCMP_UNE:
                // 目标 != (T): 边界距离 abs-EPS; 目标 == (F): 仅一点满足，无安全性余量
                return targetTruth ? -(std::abs(LHS - RHS) - EPS) : 0.0;

            case ICMP_SGT: case ICMP_UGT: case FCMP_OGT: case FCMP_UGT:
                // 目标 > (T): 安全性 -(LHS-RHS-EPS); 目标 <= (F): 安全性 -(RHS-LHS)
                return targetTruth ? -(LHS - RHS - EPS) : -(RHS - LHS);

            case ICMP_SGE: case ICMP_UGE: case FCMP_OGE: case FCMP_UGE:
                // 目标 >= (T): 安全性 -(LHS-RHS); 目标 < (F): 安全性 -(RHS-LHS-EPS)
                return targetTruth ? -(LHS - RHS) : -(RHS - LHS - EPS);

            case ICMP_SLT: case ICMP_ULT: case FCMP_OLT: case FCMP_ULT:
                // 目标 < (T): 安全性 -(RHS-LHS-EPS); 目标 >= (F): 安全性 -(LHS-RHS)
                return targetTruth ? -(RHS - LHS - EPS) : -(LHS - RHS);

            case ICMP_SLE: case ICMP_ULE: case FCMP_OLE: case FCMP_ULE:
                // 目标 <= (T): 安全性 -(RHS-LHS); 目标 > (F): 安全性 -(LHS-RHS-EPS)
                return targetTruth ? -(RHS - LHS) : -(LHS - RHS - EPS);

            default: return -1.0;
        }
    }
}

extern std::unordered_set<int> explored; // 只记录叶结点，叶结点没有第二维，统计覆盖率的时候统一添加路径上所有
int target; // 当前待覆盖或者待判定的叶结点
bool isSuccess; //本次调用是否覆盖了目标
bool currentSeedId; // 当前种子ID
bool isSelfMode = true; // 初始值
extern std::vector<int> node_prefix[MAXN];
extern int deep[MAXN];

static inline std::pair<int, bool> isSatisfied(int currentId, bool currentTruth) {
    int first = -1;
    bool second = false;
    for(int i=0; i<node_prefix[target].size(); i++) {
        auto p = node_prefix[target][i];
        if(p == currentId) {
            first = i;
            second = currentTruth;
            break;
        }
    }
    return std::make_pair(first, second);
}

extern "C" {
    void __pen(double LHS, double RHS, int brId, int cmpId, bool isInt) {
        int currentId = brId;
        bool currentTruth = getTruth(LHS, RHS, cmpId);
        std::pair<int, bool> satisfiedInfo = isSatisfied(currentId, currentTruth);
        if(satisfiedInfo.first != -1) { // 当前分支在目标前缀路径上
            if (!satisfiedInfo.second) { // 当前分支取值不满足条件
                __r = min(__r, calculate_distance(LHS, RHS, cmpId, currentTruth, targetTruth, isSelfMode));
            } 
        }

        if(isLeaf[currentId]){
            if(currentId == target) {
                __r = 0.0; // 已经覆盖目标分支
            } 
            if(explored.find(currentId) == explored.end()) {
                explored.insert(currentId);
                leafToSeed[currentId] = currentSeedId;
                update_other_leaf(currentId);
            }
        }
    }
}

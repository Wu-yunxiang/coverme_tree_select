#include <cmath>
#include <utility>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "constants.h"
#include "prepare_for_update.h"

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

extern std::unordered_set<int> explored; 
extern std::unordered_set<int> unexplored;
int target; // 当前待覆盖/待检查的结点
bool isSelfMode = true; // 初始模式
bool isGetBase = false; // 是否在获取基准阶段
int conds_satisfied_max_seed; // 记录当前种子满足的最大条件数, 运行完待测函数更新一次，每个种子初始化一次
int conds_satisfied_max_sample; // 记录当前样本满足的最大条件数, 调用 __pen 更新一次，每个样本初始化一次
std::unordered_map<int, int> conds_satisfied_max_sample_for_unexplored; // 记录每个待覆盖节点当前样本满足的最大条件数, 调用 __pen 更新一次，每个样本初始化一次
std::unordered_map<int, std::unordered_map<int, double>> delta_r_for_unexplored; // 记录每个待覆盖节点的每个依赖，在当前样本的距离, 调用 __pen 更新一次，每个样本初始化一次
std::unordered_map<int, std::unordered_map<int, double>> base_r_for_unexplored; // 记录每个待覆盖节点的每个依赖，在基准下的距离
std::unordered_map<int, std::unordered_map<int, double>> temporary_r_for_unexplored; //每个样本初始化一次
std::unordered_map<int, int> temporary_start_for_unexplored; // 恢复时栈的开头，每个样本初始化一次
std::unordered_map<int, int> conds_satisfied_last; // 上一次满足的是第几个条件，每个样本初始化
extern std::vector<int> node_prefix[MAXN]; 
extern std::unordered_map<int, int> node_map[MAXN];
extern int brCount;
extern int nodeToSeed[MAXN]; // 记录每个结点对应的种子ID
extern std::vector<double> seeds[MAXN]; // 记录每个种子的输入组合, 每次运行待测函数根据is_efc更新
bool is_efc; // 本次待测函数运行是否覆盖了新分支，用于seedId更新
extern int efc_seed_count; //每次运行待测函数根据is_efc更新
extern double __r;

void handle_base(double LHS, double RHS, int cmpId, int unexploredNode, int current){
    auto it = node_map[unexploredNode].find(current);
    if(it != node_map[unexploredNode].end()) { 
        if(temporary_start_for_unexplored.find(unexploredNode) == temporary_start_for_unexplored.end()) {
            temporary_start_for_unexplored[unexploredNode] = 1;
        }
        int conds_satisfied = it->second + 1; // 当前满足的条件个数
        if(conds_satisfied > conds_satisfied_max_sample_for_unexplored[unexploredNode]) {
            conds_satisfied_max_sample_for_unexplored[unexploredNode] = conds_satisfied;
        if(conds_satisfied > conds_satisfied_last[unexploredNode]){
            conds_satisfied_last[unexploredNode] = conds_satisfied;
            double &temporary_r = temporary_r_for_unexplored[unexploredNode][conds_satisfied];
            temporary_r = calculate_distance(LHS, RHS, cmpId, current < brCount, current < brCount, isSelfMode);
        }else{ 
            int &temporary_start = temporary_start_for_unexplored[unexploredNode];
            temporary_start = min(temporary_start, conds_satisfied);
            conds_satisfied_last[unexploredNode] = conds_satisfied;
            temporary_r_for_unexplored[unexploredNode][conds_satisfied] = calculate_distance(LHS, RHS, cmpId, current < brCount, current < brCount, isSelfMode);
        }
    }else{
        int current_reverse = current < brCount ? (current + brCount) : (current - brCount); // 当前节点的反向节点
        auto it_reverse = node_map[unexploredNode].find(current_reverse);
        if(it_reverse != node_map[unexploredNode].end()) { // 当前节点的反向节点在目标前缀上，说明当前节点是第一个不满足的
            int conds_satisfied = it_reverse->second + 1; // 满足的条件个数
            if(conds_satisfied > conds_satisfied_max_sample_for_unexplored[unexploredNode]) {
                if(base_r_for_unexplored[unexploredNode].find(conds_satisfied) == base_r_for_unexplored[unexploredNode].end()) { 
                    double &base_r = base_r_for_unexplored[unexploredNode][conds_satisfied];
                    base_r = calculate_distance(LHS, RHS, cmpId, current < brCount, current_reverse < brCount, isSelfMode);
                    for(int i = temporary_start_for_unexplored[unexploredNode]; i < conds_satisfied; ++i) {
                        base_r_for_unexplored[unexploredNode][i] = temporary_r_for_unexplored[unexploredNode][i];
                    }
                    temporary_start_for_unexplored[unexploredNode] = conds_satisfied;
                }else{
                    double &base_r = base_r_for_unexplored[unexploredNode][conds_satisfied];
                    double distance = calculate_distance(LHS, RHS, cmpId, current < brCount, current_reverse < brCount, isSelfMode);
                    if(base_r > distance) {
                        base_r = distance;
                        for(int i = temporary_start_for_unexplored[unexploredNode]; i < conds_satisfied; ++i) {
                            base_r_for_unexplored[unexploredNode][i] = temporary_r_for_unexplored[unexploredNode][i];
                        }
                        temporary_start_for_unexplored[unexploredNode] = conds_satisfied;
                    }
                }
            }
        }
    }
}

void handle_delta(double LHS, double RHS, int cmpId, int unexploredNode, int current){
    auto it = node_map[unexploredNode].find(current);
    if(it != node_map[unexploredNode].end()) { 
        if(temporary_start_for_unexplored.find(unexploredNode) == temporary_start_for_unexplored.end()) {
            temporary_start_for_unexplored[unexploredNode] = 1;
        }
        int conds_satisfied = it->second + 1; // 当前满足的条件个数
        if(conds_satisfied > conds_satisfied_max_sample_for_unexplored[unexploredNode]) {
            conds_satisfied_max_sample_for_unexplored[unexploredNode] = conds_satisfied;
        if(conds_satisfied > conds_satisfied_last[unexploredNode]){
            conds_satisfied_last[unexploredNode] = conds_satisfied;
            double &temporary_r = temporary_r_for_unexplored[unexploredNode][conds_satisfied];
            temporary_r = calculate_distance(LHS, RHS, cmpId, current < brCount, current < brCount, isSelfMode);
        }else{ 
            int &temporary_start = temporary_start_for_unexplored[unexploredNode];
            temporary_start = min(temporary_start, conds_satisfied);
            conds_satisfied_last[unexploredNode] = conds_satisfied;
            temporary_r_for_unexplored[unexploredNode][conds_satisfied] = calculate_distance(LHS, RHS, cmpId, current < brCount, current < brCount, isSelfMode);
        }
    }else{
        int current_reverse = current < brCount ? (current + brCount) : (current - brCount); // 当前节点的反向节点
        auto it_reverse = node_map[unexploredNode].find(current_reverse);
        if(it_reverse != node_map[unexploredNode].end()) { // 当前节点的反向节点在目标前缀上，说明当前节点是第一个不满足的
            int conds_satisfied = it_reverse->second + 1; // 满足的条件个数
            if(conds_satisfied > conds_satisfied_max_sample_for_unexplored[unexploredNode]) {
                if(delta_r_for_unexplored[unexploredNode].find(conds_satisfied) == delta_r_for_unexplored[unexploredNode].end()) { 
                    double &delta_r = delta_r_for_unexplored[unexploredNode][conds_satisfied];
                    delta_r = calculate_distance(LHS, RHS, cmpId, current < brCount, current_reverse < brCount, isSelfMode);
                    for(int i = temporary_start_for_unexplored[unexploredNode]; i < conds_satisfied; ++i) {
                        delta_r_for_unexplored[unexploredNode][i] = temporary_r_for_unexplored[unexploredNode][i];
                    }
                    temporary_start_for_unexplored[unexploredNode] = conds_satisfied;
                }else{
                    double &delta_r = delta_r_for_unexplored[unexploredNode][conds_satisfied];
                    double distance = calculate_distance(LHS, RHS, cmpId, current < brCount, current_reverse < brCount, isSelfMode);
                    if(delta_r > distance) {
                        delta_r = distance;
                        for(int i = temporary_start_for_unexplored[unexploredNode]; i < conds_satisfied; ++i) {
                            delta_r_for_unexplored[unexploredNode][i] = temporary_r_for_unexplored[unexploredNode][i];
                        }
                        temporary_start_for_unexplored[unexploredNode] = conds_satisfied;
                    }
                }
            }
        }
    }
}

extern "C" {
    void __pen(double LHS, double RHS, int brId, int cmpId, bool isInt) {
        bool currentTruth = getTruth(LHS, RHS, cmpId);
        int current = currentTruth ? brId : (brId + brCount); // 当前进入的节点
        bool targetTruth = target < brCount ? true : false; // target < brCount 代表目标是 True 出口，否则是 False 出口

        if(explored.find(current) == explored.end()) {
            explored.insert(current);
            unexplored.erase(current);
            nodeToSeed[current] = efc_seed_count; 
            is_efc = true; // 标记本次运行覆盖了新分支
        }

        if(isSelfMode) {
            auto it = node_map[target].find(current);
            if(it != node_map[target].end()) { 
                int conds_satisfied = it->second + 1; // 当前满足的条件个数
                if(conds_satisfied > conds_satisfied_max_sample) {
                    conds_satisfied_max_sample = conds_satisfied;
                    __r = conds_satisfied_max_sample == node_prefix[target].size() ? 0.0 : INITIAL_R;
                }
            }else{
                int current_reverse = current < brCount ? (current + brCount) : (current - brCount); // 当前节点的反向节点
                auto it_reverse = node_map[target].find(current_reverse);
                if(it_reverse != node_map[target].end()) { // 当前节点的反向节点在目标前缀上，说明当前节点是第一个不满足的，需要计算距离
                    int conds_satisfied = it_reverse->second + 1; // 当前满足的条件个数
                    if(conds_satisfied > conds_satisfied_max_sample) { // 考虑到循环
                        __r = min(__r, calculate_distance(LHS, RHS, cmpId, currentTruth, targetTruth, isSelfMode));
                    }
                }
            }
        }else{ 
            for(auto &unexploredNode : unexplored) {
                if(isGetBase){
                    handle_base(LHS, RHS, cmpId, unexploredNode, current);
                }
                else{
                    handle_delta(LHS, RHS, cmpId, unexploredNode, current);
                }
            }
        }
    }
}

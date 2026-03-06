#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <random>
#include <iterator>

#include "branch_tree.h"
#include "prepare_for_update.h"
#include "interface_for_py.h"
#include "pen.h"
#include "select_priority.h"

double __r; // 调用待测函数得到的距离
std::unordered_set<int> explored; //已覆盖的节点
std::unordered_set<int> unexplored; //待覆盖的节点

int efc_seed_count;
int seedId_base; // 本次base时代入的ID
std::unordered_map<int, double> gradient_score_sum; // 节点，得分和

static std::mt19937 gen(std::random_device{}());

void initialize_for_py() {
    explored.clear();
    unexplored.clear();
    node_min_distance.clear(); // 初始化
    for (int i = 0; i < brCount * 2; ++i) {
        unexplored.insert(i);
        nodeToSeed[i] = -1;
    }
    efc_seed_count = 0;
    queue_for_select = std::priority_queue<priority_info>();
}

extern "C" void initialize_runtime() {
    apply_data_from_insert_module_for_tree();
    initialize();
    initialize_for_py();
}

extern "C" int get_br_count() {
    return brCount;
}

extern "C" int get_arg_count() {
    return argCount;
}

extern "C" void set_target(int targetId) {
    target = targetId;
    conds_satisfied_max_seed = 0;
    conds_satisfied_max_sample = 0;
}

extern "C" int get_target(int threshold) {
    int best_target = -1;
    int min_missing = 1000000;

    for (int node : unexplored) {
        // node_prefix[node].size() 是到达该节点所需的总条件数
        int total_conds = node_prefix[node].size();
        int satisfied = 0;
        auto it = conds_satisfied_max_sample_for_unexplored.find(node);
        if (it != conds_satisfied_max_sample_for_unexplored.end()) {
            satisfied = it->second;
        }
        int missing = total_conds - satisfied;
        if (missing < min_missing) {
            min_missing = missing;
            best_target = node;
        } else if (missing == min_missing) {
            // 如果欠缺条件数相同，可以随机选一个或保持现状，这里简单随机替换
            static std::uniform_real_distribution<> dis(0, 1);
            if (dis(gen) < 0.5) {
                best_target = node;
            }
        }
    }

    if (min_missing > threshold) {
        return -1;
    }
    return best_target;
}

extern "C" int nExplored(){
    return explored.size();
}

extern "C" int finish_sample() {
    if(isSelfMode) {
        if(conds_satisfied_max_sample < (int)node_prefix[target].size()) {
            double penalty_dist = std::fmin(COND_PENALTY - 1.0, __r);
            double remaining_conds = (double)node_prefix[target].size() - conds_satisfied_max_sample;
            __r = remaining_conds * COND_PENALTY + penalty_dist;
        } else {
            __r = 0.0;
        }
    }
    else if(!isGetBase) {
        update_sample();
    }
    
    int flags = 0;
    if (is_efc) { // py接收后更新python的seeds数组,seedId和py端对应因此不需要传递
        flags |= 1;
        efc_seed_count++;
    }
    if (explored.find(target) != explored.end()) {
        flags |= 2;
    }
    if (nExplored() >= brCount * 2) {
        flags |= 4;
    }

    return flags;
}

void initial_sample(){
    __r = INITIAL_R;
    is_efc = false;

    temporary_r_for_unexplored.clear();
    temporary_start_for_unexplored.clear();
    conds_satisfied_last.clear();
    conds_satisfied_max_sample_for_unexplored.clear();
    node_min_distance.clear(); // 每次样本开始前清理

    if (isSelfMode) {
        return;
    }
    if (isGetBase) {
        base_r_for_unexplored.clear();
    } else {
        delta_r_for_unexplored.clear();
    }
}

extern "C" void begin_self_phase() {
    isSelfMode = true;
    conds_satisfied_max_sample = 0;
    initial_sample();
}

extern "C" void begin_base_phase() {
    isSelfMode = false;
    isGetBase = true;
    gradient_score_sum.clear();
    seedId_base = efc_seed_count - 1;
    initial_sample();
}

extern "C" void begin_delta_phase() {
    isSelfMode = false;
    isGetBase = false;
    initial_sample();
}

void update_sample(){
    for(const int &node : unexplored){
        int base_size = base_r_for_unexplored[node].size();
        int delta_size = delta_r_for_unexplored[node].size();
        if(base_size <= 1){
            continue;
        }
        if(base_size > delta_size) {
            continue;
        }
        if(base_size < delta_size) {
            gradient_score_sum[node] += GRADIENT_REWARD; // 给予极大奖励
            continue;
        } // 剩下都是base_size == delta_size
        double base_r = base_r_for_unexplored[node][base_size];
        double delta_r = delta_r_for_unexplored[node][delta_size];
        if(base_r <= 0 || delta_r <= 0) {
            continue;
        }
        double k = base_r / (base_r - delta_r);
        if(std::isnan(k) || std::isinf(k)) {
            continue;
        }
        double ratio_max = -1.0;
        bool flag = false;
        for(int j = 1; j < base_size; ++j) {
            double base_rj = base_r_for_unexplored[node][j];
            double delta_rj = delta_r_for_unexplored[node][j];
            if(base_rj > 0 || delta_rj > 0){
                flag = false;
                break;
            }
            double ratio_j = (base_rj - delta_rj) / base_rj;
            double ratio = ratio_j * k;
            if(std::isnan(ratio) || std::isinf(ratio)){
                flag =false;
                break;
            }
            if(!flag) {
                flag = true;
                ratio_max = ratio;
            }else{
                ratio_max = std::fmax(ratio_max, ratio);
            }
        }
        if(flag && ratio_max < 1) {
            gradient_score_sum[node] += 1 - ratio_max; 
        }
    }
}

extern "C" double get_r() {
    return __r;
}

extern "C" int get_unexplored_at(int index) {
    if (index < 0 || index >= unexplored.size()) return -1;
    auto it = unexplored.begin();
    std::advance(it, index);
    return *it;
}

extern "C" int get_unexplored_count() {
    return unexplored.size();
}

extern "C" int get_satisfied_count(int nodeId) {
    auto it = conds_satisfied_max_sample_for_unexplored.find(nodeId);
    if (it != conds_satisfied_max_sample_for_unexplored.end()) {
        return it->second;
    }
    return 0;
}

extern "C" int get_total_prefix_count(int nodeId) {
    if (nodeId >= 0 && nodeId < MAXN) {
        return node_prefix[nodeId].size();
    }
    return 0;
}

extern "C" double get_min_node_distance(int nodeId) {
    auto it = node_min_distance.find(nodeId);
    if (it != node_min_distance.end()) {
        return it->second;
    }
    return -1.0;
}
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <random>

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
int last_covered_node = -1; // 记录最新被覆盖的节点
int newly_covered_count = 0; // 记录本次调用新覆盖的节点数
std::unordered_map<int, double> gradient_score_sum; // 节点，得分和

static std::mt19937 gen(std::random_device{}());

void initialize_for_py() {
    explored.clear();
    unexplored.clear();
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

extern "C" int set_target(int conds_diff_threshold) {
    if (unexplored.empty()) {
        target = -1;
        return -1;
    }

    int best_node = -1;
    int min_diff = 2147483647; // 较大的整数代表正无穷
    int min_total = 2147483647;

    for (int node : unexplored) {
        int total = node_prefix[node].size();
        int current_similarity = base_r_for_unexplored[node].size() - 1;
        int diff = total - current_similarity;

        if (diff < min_diff) {
            min_diff = diff;
            min_total = total;
            best_node = node;
        } else if (diff == min_diff) {
            if (total < min_total) {
                min_total = total;
                best_node = node;
            }
        }
    }

    if (best_node == -1 || min_diff > conds_diff_threshold) {
        target = -1;
        return -1;
    }

    target = best_node;
    conds_satisfied_max_seed = 0;
    conds_satisfied_max_sample = 0;
    return target;
}

extern "C" void set_random_target(int random_target) {
    target = random_target;
    conds_satisfied_max_seed = 0;
    conds_satisfied_max_sample = 0;
}

extern "C" TargetAndSeed pop_queue_target() { // 返回结果中的target=-1代表队列空了且没有target,seedId作为py初始值，也包含了每个seed的初始化
    TargetAndSeed t;
    t.targetId = -1;
    t.seedId = -1;
    while (!queue_for_select.empty()) {
        priority_info info = queue_for_select.top();
        queue_for_select.pop();
        if (explored.find(info.nodeId) == explored.end()) {
            target = info.nodeId;
            conds_satisfied_max_seed = 0;
            conds_satisfied_max_sample = 0;
            t.targetId = info.nodeId;
            t.seedId = info.seedId;
            break;
        }
    }
    return t;
}

extern "C" int get_target() {
    return target;
}

extern "C" int get_last_covered_node() {
    return last_covered_node;
}

extern "C" void set_target_direct(int val) {
    target = val;
    explored.erase(val); // 求解前必须从已探索中移除，否则 finish_sample 不会触发覆盖标志
}

extern "C" int nExplored(){
    return explored.size();
}

extern "C" int finish_sample() {
    if(isSelfMode) {
        /*if(conds_satisfied_max_sample < conds_satisfied_max_seed) {
            __r = INITIAL_R;
        }else{
            conds_satisfied_max_seed = conds_satisfied_max_sample;
        }*/
        //__r = INITIAL_R * (node_prefix[target].size() - conds_satisfied_max_sample) + std::fmin(INITIAL_R - 1, __r);
        __r = (node_prefix[target].size() - conds_satisfied_max_sample) + __r/(__r+1);
        //__r = INITIAL_R;
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
    newly_covered_count = 0; // 重置新覆盖计数
    initial_sample();
}

extern "C" void begin_base_phase() {
    isSelfMode = false;
    isGetBase = true;
    gradient_score_sum.clear();
    seedId_base = efc_seed_count;
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

extern "C" void update_queue(){
    for(auto &node : unexplored) {
        priority_info info;
        info.nodeId = node;
        info.similarity = base_r_for_unexplored[node].size() - 1;
        info.constraint_nb = node_prefix[node].size();
        info.gradient_score = gradient_score_sum[node];
        info.seedId = seedId_base;
        queue_for_select.push(info);
    }
}

extern "C" double get_r() {
    return __r;
}

extern "C" int get_node_status(double* last_dist, int* total_conds, int* newly_covered) {
    if (last_covered_node == -1) {
        *last_dist = -1.0;
        *total_conds = 0;
        *newly_covered = 0;
        return 0;
    }
    
    int nodeId = last_covered_node;
    *total_conds = (int)node_prefix[nodeId].size();
    *newly_covered = newly_covered_count;
    
    if (base_r_for_unexplored.find(nodeId) == base_r_for_unexplored.end() || base_r_for_unexplored[nodeId].empty()) {
        *last_dist = -1.0;
        return 0;
    }
    auto& v = base_r_for_unexplored[nodeId];
    *last_dist = v[v.size()]; 
    return (int)v.size() - 1;   
}
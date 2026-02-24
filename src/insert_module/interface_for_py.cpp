#include <unordered_set>
#include <unordered_map>
#include <cmath>

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

extern "C" void warmup_target(int targetNode) {
    target = targetNode;
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
            TargetAndSeed t;
            t.targetId = info.nodeId;
            t.seedId = info.seedId;
            break;
        }
    }
    return t;
}

extern "C" int nExplored(){
    return explored.size();
}

extern "C" FlagAndSeed finish_sample() {
    if(!isSelfMode && !isGetBase) {
        update_sample();
    }
    int seedId = -1;
    int flags = 0;
    if (is_efc) { // py接收后更新python的seeds数组,下标为seedId
        flags |= 1;
        seedId = efc_seed_count++;
    }
    if (explored.find(target) != explored.end()) {
        flags |= 2;
    }
    if (nExplored() >= brCount * 2) {
        flags |= 4;
    }
    FlagAndSeed info;
    info.flags = flags;
    info.seedId = seedId;
    return info;
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
    conds_satisfied_max_seed = std::max(conds_satisfied_max_seed, conds_satisfied_max_sample);
    conds_satisfied_max_sample = 0;
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
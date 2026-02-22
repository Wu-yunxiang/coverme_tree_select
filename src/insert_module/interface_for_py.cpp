#include <unordered_set>
#include <unordered_map>
#include <map>
#include <utility>
#include <queue>
#include <vector>
#include <cmath>

#include "interface_for_py.h"
#include "select_priority.h"
#include "config.h"

double __r; // 调用待测函数得到的距离
std::unordered_set<int> explored; //已覆盖的节点
std::unordered_set<int> unexplored; //待覆盖的节点
extern int brCount; 

// 引入外部数据结构
extern std::unordered_map<int, std::unordered_map<int, double>> delta_r_for_unexplored; 
extern std::unordered_map<int, std::unordered_map<int, double>> base_r_for_unexplored; 
extern std::unordered_map<int, int> conds_satisfied_max_sample_for_unexplored;
extern std::priority_queue<priority_info> queue_for_select;
extern std::vector<int> node_prefix[MAXN];
extern bool is_efc;
int efc_seed_count;
int seedId_base; // 本次base时代入的ID
std::unordered_map<int, double> gradient_score_sum; // 节点，得分和

void initialize_for_py() {
    for (int i = 0; i < brCount * 2; ++i) {
        unexplored.insert(i);
    }
}

extern "C" int nExplored(){
    return explored.size();
}

extern "C" void initial_sample(){
}

extern "C" void update_sample(){
    for(int &node : unexplored){
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
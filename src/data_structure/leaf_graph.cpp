#include <vector>
#include <utility>
#include <algorithm>
#include <queue>
#include <cstring>

#include "constants.h"
#include "select_priority.h"

// 记录叶节点之间的边关系，pair 中第一个元素为另一个叶节点的 ID，第二个元素为相似度
std::vector<std::pair<int, int> > leaf_edge[MAXN]; 

void add_leaf_edge(int u, int v, int weight) {
    leaf_edge[u].push_back(std::make_pair(v, weight));
    leaf_edge[v].push_back(std::make_pair(u, weight));
}

std::vector<std::pair<int, bool> > leaf_prefix[MAXN]; // 记录每个叶节点的前缀路径，pair 中第一个元素为父节点 ID，第二个元素为边的方向

void load_leaf_prefix() {
    for (int i = 0; i < tree_node_count; ++i) {
        if (is_leaf[i]) { // 叶节点
            int u = i;
            while (parent[u].first != u) {
                leaf_prefix[i].push_back(parent[u]);
                u = parent[u].first;
            }
            // 反转以得到从根节点到叶节点的路径
            std::reverse(leaf_prefix[i].begin(), leaf_prefix[i].end()); 
        }  
    }
}

std::priority_queue<distance_info>  priority_queue; //全局小根堆
int leafToSeed[MAXN]; // 记录每个叶结点对应的种子ID
std::vector<double> seeds[MAXN]; // 记录每个种子的输入组合

void initialize(){
    std::memset(leafToSeed, -1, sizeof(leafToSeed));
}

extern std::pair<int, bool> targetBranch;
extern bool isSelfMode;
double get_gradient_score(int seedId, int leafId, int similarity){
    // 获取第一个取值相反的分支
    targetBranch = leaf_prefix[leafId][similarity];
    isSelfMode = false; // 更新其它分支
    for(auto &var : seeds[seedId]){
        // to do (调用插桩后的函数名) var+delta
        // 根据全局变量的反馈计算得分
    }
    isSelfMode = true; // 恢复
}

void update_other_leaf(int u){
    for(auto &p : leaf_edge[u]) {
        int v = p.first;
        int weight = p.second;
        distance_info info;
        info.similarity = weight;
        info.constraint_nb = deep[v] - 1;
        info.gradient_score = get_gradient_score(leafToSeed[u], v, weight);
        info.seedId = leafToSeed[u];
        priority_queue.push(info);
    }
}



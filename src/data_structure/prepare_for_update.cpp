#include <vector>
#include <utility>
#include <algorithm>
#include <queue>
#include <cstring>
#include <unordered_map>

#include "constants.h"

// 记录树节点之间的边关系，pair 中第一个元素为另一个节点的 ID，第二个元素为相似度
std::vector<std::pair<int, int> > update_edge[MAXN]; 

void add_update_edge(int u, int v, int weight) {
    update_edge[u].push_back(std::make_pair(v, weight));
    update_edge[v].push_back(std::make_pair(u, weight));
}

extern int nodeToSeed[MAXN]; // 记录每个结点对应的种子ID
extern std::vector<double> seeds[MAXN]; // 记录每个种子的输入组合
std::vector<int> node_prefix[MAXN]; // 记录每个节点的前缀路径
std::unordered_map<int, int> node_map[MAXN]; // 对每个节点，保存其前缀中的元素到前缀序号的映射

void initialize() {
    std::memset(nodeToSeed, -1, sizeof(int) * 2 * brCount);
    for (int i = 0; i < 2 * brCount; ++i) {
        int now = i;
        while (parent[now] != now) {
            node_prefix[i].push_back(now);
            now = parent[now];
        }
        node_prefix[i].push_back(now);
        // 反转以得到从根节点到当前节点的前缀路径
        std::reverse(node_prefix[i].begin(), node_prefix[i].end()); 
        // 建立 node_map
        for(int j = 0; j < node_prefix[i].size(); ++j) {
            node_map[i][node_prefix[i][j]] = j;
        }
    }  
}

extern std::priority_queue<priority_info> queue_for_select;
extern int target;
extern bool isSelfMode;
double get_gradient_score(int seedId, int nodeId, int similarity){
    // 获取当前待满足的目标节点
    int target = node_prefix[nodeId][similarity];
    isSelfMode = false; 
    for(double &var : seeds[seedId]){
        // to do (调用插桩后的函数名) var+delta
        // 根据全局变量的反馈计算得分
    }
    isSelfMode = true; 
}

extern std::unordered_set<int> explored;
void update_other_node(int u){ // 用来进行更新的节点
    for(auto &p : update_edge[u]) {
        int v = p.first;
        int similarity = p.second;
        if(explored.find(v) != explored.end()) continue; // 已经覆盖的节点不更新
        priority_info info;
        info.nodeId = v;
        info.similarity = similarity;
        info.constraint_nb = deep[v];
        info.gradient_score = get_gradient_score(nodeToSeed[u], v, similarity);
        info.seedId = nodeToSeed[u];
        queue_for_select.push(info);
    }
}



#include <utility>
#include <queue>
#include <fstream>
#include <vector>

#include "prepare_for_update.h"
#include "config.h"

int brCount = 0; // 分支计数
std::vector<int> tree_edge[MAXN]; // 邻接表
int parent[MAXN]; // 记录每个节点的父节点,根节点的父节点为自身

void add_edge(int u, int v) {
    tree_edge[u].push_back(v);
    parent[v] = u; 
} // 单向边 父节点 

void load_edges() {
    std::ifstream edgeInfo("output/edges.txt"); // 读取边信息
    int u, v;
    while (edgeInfo >> u >> v) {
        add_edge(u, v); 
    }
}

void apply_data_from_insert_module_for_tree(){
    for (int i = 0; i < brCount * 2; ++i) {
        parent[i] = i; // 初始化父节点为自身
    }
    load_edges(); // 加载边信息
}




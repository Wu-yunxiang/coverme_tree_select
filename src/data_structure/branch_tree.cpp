#include <fstream>

#include "branch_tree.h"

int brCount; // 分支计数
int argCount; // 目标函数参数个数
std::vector<int> tree_edge[MAXN]; // 邻接表
int parent[MAXN]; // 记录每个节点的父节点,根节点的父节点为自身

void add_edge(int u, int v) {
    tree_edge[u].push_back(v);
    parent[v] = u; 
} // 单向边 父节点 

bool load_instrumentation_meta() {
    std::ifstream metaInfo("output/instrumentation_meta.txt");
    metaInfo >> brCount >> argCount;
}

void load_edges() {
    std::ifstream edgeInfo("output/edges.txt"); // 读取边信息
    int u, v;
    while (edgeInfo >> u >> v) {
        add_edge(u, v); 
    }
}

void apply_data_from_insert_module_for_tree(){
    load_instrumentation_meta();
    for (int i = 0; i < brCount * 2; ++i) {
        tree_edge[i].clear();
        parent[i] = i; // 初始化父节点为自身
    }
    load_edges(); // 加载边信息
}




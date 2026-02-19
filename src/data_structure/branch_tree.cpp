#include <utility>
#include <queue>
#include <fstream>
#include <vector>

#include "prepare_for_update.h"
#include "constants.h"

int brCount = 0; // 分支计数
std::vector<int> roots; // 根节点列表
std::vector<int> tree_edge[MAXN]; // 邻接表
int parent[MAXN]; // 记录每个节点的父节点,根节点的父节点为自身
int root[MAXN]; // 记录每个节点的根节点
int deep[MAXN]; // 记录每个节点所在其树的深度，从1开始
std::vector<int> root_nodes[MAXN]; // 记录每棵树的所有节点

void add_edge(int u, int v) {
    tree_edge[u].push_back(v);
    parent[v] = u; 
} // 单向边 父节点 

void load_edges() {
    std::ifstream edgeInfo("to do (by configs)"); // 读取边信息
    int u, v;
    while (edgeInfo >> u >> v) {
        add_edge(u, v); 
    }
}

void get_deep(int u,int fa, int root_id) {
    root[u] = root_id; // 记录根节点ID
    deep[u] = deep[fa] + 1; // 深度为父节点深度加1
    root_nodes[root_id].push_back(u); // 将节点加入其根节点对应的列表
    for (int &v : tree_edge[u]) {
        get_deep(v, u, root_id);
    }
}

int get_lca_deep(int u, int v) {
    while (deep[u] > deep[v]) u = parent[u];
    while (deep[v] > deep[u]) v = parent[v];
    while (u != v) {
        u = parent[u];
        v = parent[v];
    }
    return deep[u];
}

void apply_data_from_insert_module_for_tree(){
    for (int i = 0; i < brCount * 2; ++i) {
        parent[i] = i; // 初始化父节点为自身
    }
    load_edges(); // 加载边信息
    for(int i = 0; i < brCount * 2; ++i) {
        if (parent[i] == i) { // 根节点
            roots.push_back(i);
            get_deep(i, i, i);
        }
    }

    for(int &root_id : roots) {
        std::vector<int> &nodes = root_nodes[root_id];
        for(int i = 0; i < nodes.size(); ++i) {
            for(int j = i + 1; j < nodes.size(); ++j) {
                add_update_edge(nodes[i], nodes[j], 
                    get_lca_deep(nodes[i], nodes[j]));
            }
        }
    }
}




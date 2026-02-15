#include <cstring>
#include <utility>
#include <queue>
#include <fstream>
#include <vector>

#include "leaf_graph.h"
#include "constants.h"

int tree_node_count = 0; // 节点数量
std::vector<int> roots; // 根节点列表
std::vector<std::pair<int,bool> > tree_edge[MAXN]; // 邻接表
std::pair<int, bool> parent[MAXN]; // 记录每个节点的父节点,根节点的父节点为自身
int root[MAXN]; // 记录每个节点的根节点
int deep[MAXN]; // 记录每个节点所在其树的深度，从1开始
bool is_leaf[MAXN]; // 记录每个节点是否为叶节点
std::vector<int> root_leaf[MAXN]; // 记录每棵树的叶节点列表

void add_edge(int u, int v, bool direction) {
    tree_edge[u].push_back(std::make_pair(v, direction));
    parent[v] = std::make_pair(u, direction); 
} // 单向边 父节点 

void load_edges() {
    std::ifstream edgeInfo("to do (by configs)"); // 读取边信息
    int u, v;
    bool weight;
    while (edgeInfo >> u >> v >> weight) {
        add_edge(u, v, weight);
    }
}

void get_deep_and_leaf(int u,int fa, int root_id) {
    root[u] = root_id; // 记录根节点ID
    deep[u] = deep[fa] + 1; // 深度为父节点深度加1
    bool has_child = false;
    for (auto &p : tree_edge[u]) {
        has_child = true;
        int v = p.first;
        get_deep_and_leaf(v, u, root_id);
    }
    if (!has_child) {
        is_leaf[u] = true;
        root_leaf[root_id].push_back(u);
    }
}

std::pair<int, bool> get_lca_info(int u, int v) {
    if (root[u] != root[v]) return std::make_pair(-1, false);

    while (deep[u] > deep[v]) u = parent[u].first;
    while (deep[v] > deep[u]) v = parent[v].first;
    bool latest_u_edge, latest_v_edge;
    while (u != v) {
        latest_u_edge = parent[u].second;
        latest_v_edge = parent[v].second;
        u = parent[u].first;
        v = parent[v].first;
    }

    return std::make_pair(u, latest_u_edge != latest_v_edge);
}

void apply_data_from_insert_module_for_tree(){
    initialize_trees();
    load_edges(); // 加载边信息
    for(int i = 0; i < tree_node_count; ++i) {
        if (root[i] == i) { // 根节点
            roots.push_back(i);
            get_deep_and_leaf(i, i, i); // 从根节点开始计算深度和叶节点
        }
    }

    for(auto &root_id : roots) {
        std::vector<int> &leaves = root_leaf[root_id];
        for(size_t i = 0; i < leaves.size(); ++i) {
            for(size_t j = i + 1; j < leaves.size(); ++j) {
                std::pair<int, bool> lca_info = get_lca_info(leaves[i], leaves[j]);
                if (lca_info.second == false) continue; // 边权相同则跳过
                int similarity = deep[lca_info.first] - 1; // i和j相同的前缀个数
                if (similarity <= 0) continue; // 没有公共前缀则跳过
                add_leaf_edge(leaves[i], leaves[j], similarity);
            }
        }
    }
}

void initialize_trees() {
    for (int i = 0; i < tree_node_count; ++i) {
        root[i] = i; 
        parent[i] = std::make_pair(i, false);
        tree_edge[i].clear();
        root_leaf[i].clear();
    }
    std::memset(deep, 0, sizeof(int) * tree_node_count);
    std::memset(is_leaf, false, sizeof(bool) * tree_node_count);
}




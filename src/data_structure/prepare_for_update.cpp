#include <vector>
#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "config.h"

extern int brCount;
extern int parent[MAXN]; 
std::vector<int> node_prefix[MAXN]; // 记录每个节点的前缀路径
std::unordered_map<int, int> node_map[MAXN]; // 对每个节点，保存其前缀中的元素到前缀序号的映射

void initialize() {
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



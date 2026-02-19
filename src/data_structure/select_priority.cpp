#include <queue>

#include "select_priority.h"

struct priority_info{
    int nodeId; // 节点ID
    int similarity; // 相似度
    int constraint_nb; // 约束数量 (deep)
    double gradient_score; // 梯度得分
    int seedId; // 用于满足当前节点的初始种子ID
    
    bool operator < (const priority_info &other) const {
        bool flag;
        int me = constraint_nb * (constraint_nb - similarity);
        int ot = other.constraint_nb * (other.constraint_nb - other.similarity);
        if (me < ot) {
            flag = true;
        } else if (me == ot) {
            flag = gradient_score < other.gradient_score;
        } else {
            flag = false;
        }
        return !flag;
    }
}; 
std::priority_queue<priority_info> queue_for_select; //全局小根堆



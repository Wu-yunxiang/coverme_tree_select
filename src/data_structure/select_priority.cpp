#include <queue>
#include <utility>

#include "constants.h"
#include "select_priority.h"

struct distance_info{
    int branchId; // 分支ID
    int similarity; // 相似度
    int constraint_nb; // 约束数量 (deep - 1)
    double gradient_score; // 梯度得分
    int seedId; // 该距离对应的种子ID  
    
    bool operator < (const distance_info &other) const {
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



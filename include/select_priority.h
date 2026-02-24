#ifndef SELECT_PRIORITY_H
#define SELECT_PRIORITY_H

#include <queue>

struct priority_info {
    int nodeId;
    int similarity;
    int constraint_nb;
    double gradient_score;
    int seedId;

    bool operator<(const priority_info &other) const {
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

extern std::priority_queue<priority_info> queue_for_select;

#endif

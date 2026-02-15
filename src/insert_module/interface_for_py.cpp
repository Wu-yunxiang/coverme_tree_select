#include <set>
#include <map>
#include <utility>

#include "interface_for_py.h"

double __r; // 调用待测函数得到的
std::set<std::pair<int, bool> > explored; //已经覆盖的分支

struct seed_branch_info {
    int seedId;
    int branchId;
    bool direction;

    bool operator < (const seed_branch_info& other) const{
        if (seedId != other.seedId) return seedId < other.seedId;
        if (branchId != other.branchId) return branchId < other.branchId;
        return direction < other.direction;
    }
};
// 记录每个seed,branch组合作为拦路虎的次数，刻画了当前seed解决branch的难度
std::map<seed_branch_info, int> seed_branch_block_count; 

extern "C" int nExplored(){
  return explored.size();
}


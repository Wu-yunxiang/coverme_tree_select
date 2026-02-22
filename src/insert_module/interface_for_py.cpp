#include <unordered_set>
#include <map>
#include <utility>

#include "interface_for_py.h"

double __r; // 调用待测函数得到的距离
std::unordered_set<int> explored; //已覆盖的节点
std::unordered_set<int> unexplored; //待覆盖的节点
extern int brCount; 
void initialize_for_py() {
    for (int i = 0; i < brCount * 2; ++i) {
        unexplored.insert(i);
    }
}

extern "C" int nExplored(){
  return explored.size();
}





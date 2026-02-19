#include <unordered_set>
#include <map>
#include <utility>

#include "interface_for_py.h"

double __r; // 调用待测函数得到的距离
std::unordered_set<int> explored; //已覆盖的节点

extern "C" int nExplored(){
  return explored.size();
}


#ifndef PREPARE_FOR_UPDATE_H
#define PREPARE_FOR_UPDATE_H

#include <unordered_map>
#include <vector>

#include "config.h"

extern std::vector<int> node_prefix[MAXN];
extern std::unordered_map<int, int> node_map[MAXN];

void initialize();

#endif

#ifndef BRANCH_TREE_H
#define BRANCH_TREE_H

#include <vector>

#include "config.h"

extern int brCount;
extern int argCount;
extern std::vector<int> tree_edge[MAXN];
extern int parent[MAXN];

void add_edge(int u, int v);
bool load_instrumentation_meta();
void load_edges();
void apply_data_from_insert_module_for_tree();

#endif

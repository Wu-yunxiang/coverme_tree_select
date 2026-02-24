#ifndef PEN_H
#define PEN_H

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config.h"

extern std::unordered_set<int> explored;
extern std::unordered_set<int> unexplored;

extern int target;
extern bool isSelfMode;
extern bool isGetBase;
extern int conds_satisfied_max_seed;
extern int conds_satisfied_max_sample;
extern std::unordered_map<int, int> conds_satisfied_max_sample_for_unexplored;
extern std::unordered_map<int, std::unordered_map<int, double>> delta_r_for_unexplored;
extern std::unordered_map<int, std::unordered_map<int, double>> base_r_for_unexplored;
extern std::unordered_map<int, std::unordered_map<int, double>> temporary_r_for_unexplored;
extern std::unordered_map<int, int> temporary_start_for_unexplored;
extern std::unordered_map<int, int> conds_satisfied_last;

extern int nodeToSeed[MAXN];
extern bool is_efc;
extern int efc_seed_count;
extern double __r;
extern int seedId_base;

extern "C" {
void __pen(double LHS, double RHS, int brId, int cmpId, bool isInt);
}

#endif

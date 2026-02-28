/**
 * Unit tests for coverme_tree_select core components.
 *
 * Tests branch_tree, prepare_for_update, select_priority, pen (distance
 * calculation), and interface_for_py modules.
 */

#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <queue>

#include "config.h"
#include "branch_tree.h"
#include "prepare_for_update.h"
#include "select_priority.h"
#include "pen.h"
#include "interface_for_py.h"

// ============================================================
// Tests for branch_tree module
// ============================================================

class BranchTreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear state
        for (int i = 0; i < MAXN; ++i) {
            tree_edge[i].clear();
            parent[i] = i;
        }
        brCount = 0;
        argCount = 0;
    }
};

TEST_F(BranchTreeTest, AddEdgeCreatesParentChild) {
    add_edge(0, 1);
    EXPECT_EQ(parent[1], 0);
    EXPECT_EQ(tree_edge[0].size(), 1u);
    EXPECT_EQ(tree_edge[0][0], 1);
}

TEST_F(BranchTreeTest, AddMultipleEdges) {
    add_edge(0, 1);
    add_edge(0, 2);
    add_edge(1, 3);
    EXPECT_EQ(parent[1], 0);
    EXPECT_EQ(parent[2], 0);
    EXPECT_EQ(parent[3], 1);
    EXPECT_EQ(tree_edge[0].size(), 2u);
    EXPECT_EQ(tree_edge[1].size(), 1u);
}

TEST_F(BranchTreeTest, RootParentIsSelf) {
    // Root node has parent pointing to itself
    EXPECT_EQ(parent[0], 0);
}

// ============================================================
// Tests for prepare_for_update module
// ============================================================

class PrepareForUpdateTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int i = 0; i < MAXN; ++i) {
            tree_edge[i].clear();
            parent[i] = i;
        }
        brCount = 3;
        argCount = 1;
    }
};

TEST_F(PrepareForUpdateTest, InitializeBuildsPrefixPaths) {
    // Build a simple tree:
    //   0 (root)
    //   ├── 1
    //   │   └── 3
    //   └── 2
    add_edge(0, 1);
    add_edge(0, 2);
    add_edge(1, 3);

    initialize();

    // Node 0 prefix: [0]
    EXPECT_EQ(node_prefix[0].size(), 1u);
    EXPECT_EQ(node_prefix[0][0], 0);

    // Node 1 prefix: [0, 1]
    EXPECT_EQ(node_prefix[1].size(), 2u);
    EXPECT_EQ(node_prefix[1][0], 0);
    EXPECT_EQ(node_prefix[1][1], 1);

    // Node 3 prefix: [0, 1, 3]
    EXPECT_EQ(node_prefix[3].size(), 3u);
    EXPECT_EQ(node_prefix[3][0], 0);
    EXPECT_EQ(node_prefix[3][1], 1);
    EXPECT_EQ(node_prefix[3][2], 3);
}

TEST_F(PrepareForUpdateTest, InitializeBuildsNodeMap) {
    add_edge(0, 1);
    add_edge(1, 3);

    initialize();

    // Node 3 prefix is [0, 1, 3]
    // node_map[3] should map: 0->0, 1->1, 3->2
    EXPECT_EQ(node_map[3][0], 0);
    EXPECT_EQ(node_map[3][1], 1);
    EXPECT_EQ(node_map[3][3], 2);
}

// ============================================================
// Tests for select_priority module
// ============================================================

TEST(SelectPriorityTest, PriorityQueueOrdering) {
    std::priority_queue<priority_info> pq;

    // Lower cost = higher priority (min-heap via inverted operator<)
    priority_info a = {0, 1, 2, 0.5, 0};  // cost = 2*(2-1) = 2
    priority_info b = {1, 0, 3, 0.3, 1};  // cost = 3*(3-0) = 9
    priority_info c = {2, 2, 2, 0.8, 2};  // cost = 2*(2-2) = 0

    pq.push(a);
    pq.push(b);
    pq.push(c);

    // c has lowest cost (0), should come first
    auto top = pq.top(); pq.pop();
    EXPECT_EQ(top.nodeId, 2);

    // a has cost 2, next
    top = pq.top(); pq.pop();
    EXPECT_EQ(top.nodeId, 0);

    // b has cost 9, last
    top = pq.top(); pq.pop();
    EXPECT_EQ(top.nodeId, 1);
}

TEST(SelectPriorityTest, TieBreakByGradientScore) {
    std::priority_queue<priority_info> pq;

    // Same cost, different gradient scores
    priority_info a = {0, 1, 2, 0.5, 0};  // cost = 2
    priority_info b = {1, 1, 2, 0.8, 1};  // cost = 2

    pq.push(a);
    pq.push(b);

    // a has lower gradient score, gets higher priority in this heap
    auto top = pq.top(); pq.pop();
    EXPECT_EQ(top.nodeId, 0);
}

// ============================================================
// Tests for pen module (getTruth, calculate_distance via __pen)
// ============================================================

class PenTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up minimal state for pen testing
        for (int i = 0; i < MAXN; ++i) {
            tree_edge[i].clear();
            parent[i] = i;
            nodeToSeed[i] = -1;
        }
        brCount = 2;
        argCount = 1;
        explored.clear();
        unexplored.clear();
        for (int i = 0; i < brCount * 2; ++i) {
            unexplored.insert(i);
        }
        efc_seed_count = 0;
        is_efc = false;
    }
};

TEST_F(PenTest, PenMarksExploredBranch) {
    // Set up tree
    initialize();

    target = 0;
    isSelfMode = true;
    conds_satisfied_max_sample = 0;
    __r = INITIAL_R;

    // Call __pen: LHS=5.0 > RHS=0.0, brId=0, cmpId=2 (FCMP_OGT), isInt=false
    // This should mark node 0 (true branch of br 0) as explored
    __pen(5.0, 0.0, 0, 2, false);

    EXPECT_TRUE(explored.find(0) != explored.end());
    EXPECT_TRUE(unexplored.find(0) == unexplored.end());
    EXPECT_TRUE(is_efc);
}

TEST_F(PenTest, PenSelfModeComputesDistance) {
    add_edge(0, 1);  // root -> true branch 1
    initialize();

    target = 1;  // target is true branch of br 1
    isSelfMode = true;
    conds_satisfied_max_sample = 0;
    __r = INITIAL_R;

    // First pen call: LHS=5.0 > RHS=0.0, br0, FCMP_OGT
    // Truth = true, so enters node 0 (true of br0)
    __pen(5.0, 0.0, 0, 2, false);

    // node 0 is in prefix of target 1, so conds_satisfied increases
    EXPECT_GE(conds_satisfied_max_sample, 1);
}

// ============================================================
// Tests for interface_for_py module
// ============================================================

class InterfaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Manually set up state instead of calling initialize_runtime()
        // which requires file I/O
        for (int i = 0; i < MAXN; ++i) {
            tree_edge[i].clear();
            parent[i] = i;
            nodeToSeed[i] = -1;
        }
        brCount = 2;
        argCount = 1;

        // Build a simple tree
        add_edge(0, 1);
        add_edge(0, 2);
        add_edge(2, 3);

        initialize();

        // Set up explored/unexplored
        explored.clear();
        unexplored.clear();
        for (int i = 0; i < brCount * 2; ++i) {
            unexplored.insert(i);
        }
        efc_seed_count = 0;
        queue_for_select = std::priority_queue<priority_info>();
    }
};

TEST_F(InterfaceTest, GetBrCount) {
    EXPECT_EQ(get_br_count(), 2);
}

TEST_F(InterfaceTest, GetArgCount) {
    EXPECT_EQ(get_arg_count(), 1);
}

TEST_F(InterfaceTest, WarmupTarget) {
    warmup_target(1);
    EXPECT_EQ(target, 1);
}

TEST_F(InterfaceTest, NExploredInitiallyZero) {
    EXPECT_EQ(nExplored(), 0);
}

TEST_F(InterfaceTest, PopQueueTargetEmptyQueue) {
    TargetAndSeed result = pop_queue_target();
    EXPECT_EQ(result.targetId, -1);
    EXPECT_EQ(result.seedId, -1);
}

TEST_F(InterfaceTest, PopQueueTargetWithItems) {
    // Push an item to the queue
    priority_info info = {1, 0, 2, 0.5, 0};
    queue_for_select.push(info);

    TargetAndSeed result = pop_queue_target();
    EXPECT_EQ(result.targetId, 1);
    EXPECT_EQ(result.seedId, 0);
}

TEST_F(InterfaceTest, PopQueueSkipsExploredNodes) {
    // Mark node 1 as explored
    explored.insert(1);
    unexplored.erase(1);

    // Push explored node 1 and unexplored node 2
    priority_info info1 = {1, 0, 2, 0.9, 0};
    priority_info info2 = {2, 0, 2, 0.5, 1};
    queue_for_select.push(info1);
    queue_for_select.push(info2);

    TargetAndSeed result = pop_queue_target();
    // Should skip node 1 (explored) and return node 2
    EXPECT_EQ(result.targetId, 2);
}

TEST_F(InterfaceTest, BeginSelfPhase) {
    begin_self_phase();
    EXPECT_TRUE(isSelfMode);
    EXPECT_EQ(conds_satisfied_max_sample, 0);
    EXPECT_DOUBLE_EQ(__r, INITIAL_R);
}

TEST_F(InterfaceTest, BeginBasePhase) {
    efc_seed_count = 5;
    begin_base_phase();
    EXPECT_FALSE(isSelfMode);
    EXPECT_TRUE(isGetBase);
    EXPECT_EQ(seedId_base, 4);
}

TEST_F(InterfaceTest, BeginDeltaPhase) {
    begin_delta_phase();
    EXPECT_FALSE(isSelfMode);
    EXPECT_FALSE(isGetBase);
}

TEST_F(InterfaceTest, GetR) {
    __r = 42.0;
    EXPECT_DOUBLE_EQ(get_r(), 42.0);
}

TEST_F(InterfaceTest, GetNodeSeed) {
    nodeToSeed[0] = 5;
    EXPECT_EQ(get_node_seed(0), 5);
    EXPECT_EQ(get_node_seed(1), -1);
}

TEST_F(InterfaceTest, GetNodeSeedBoundsCheck) {
    EXPECT_EQ(get_node_seed(-1), -1);
    EXPECT_EQ(get_node_seed(MAXN), -1);
}

TEST_F(InterfaceTest, GetTreeParent) {
    EXPECT_EQ(get_tree_parent(1), 0);
    EXPECT_EQ(get_tree_parent(0), 0);  // root
}

TEST_F(InterfaceTest, GetTreeParentBoundsCheck) {
    EXPECT_EQ(get_tree_parent(-1), -1);
    EXPECT_EQ(get_tree_parent(MAXN), MAXN);
}

TEST_F(InterfaceTest, GetTreeChildrenCount) {
    EXPECT_EQ(get_tree_children_count(0), 2);  // node 0 has children 1, 2
    EXPECT_EQ(get_tree_children_count(2), 1);  // node 2 has child 3
    EXPECT_EQ(get_tree_children_count(3), 0);  // node 3 is leaf
}

TEST_F(InterfaceTest, GetTreeChildrenCountBoundsCheck) {
    EXPECT_EQ(get_tree_children_count(-1), 0);
    EXPECT_EQ(get_tree_children_count(MAXN), 0);
}

TEST_F(InterfaceTest, GetTreeChild) {
    EXPECT_EQ(get_tree_child(0, 0), 1);
    EXPECT_EQ(get_tree_child(0, 1), 2);
    EXPECT_EQ(get_tree_child(2, 0), 3);
}

TEST_F(InterfaceTest, GetTreeChildBoundsCheck) {
    EXPECT_EQ(get_tree_child(-1, 0), -1);
    EXPECT_EQ(get_tree_child(0, -1), -1);
    EXPECT_EQ(get_tree_child(0, 5), -1);
    EXPECT_EQ(get_tree_child(MAXN, 0), -1);
}

TEST_F(InterfaceTest, FinishSampleSelfModeNoProgress) {
    isSelfMode = true;
    conds_satisfied_max_sample = 0;
    conds_satisfied_max_seed = 1;
    __r = 5.0;
    is_efc = false;
    target = 0;

    int flags = finish_sample();
    // Since conds_satisfied_max_sample < conds_satisfied_max_seed, __r should reset
    EXPECT_DOUBLE_EQ(__r, INITIAL_R);
    EXPECT_EQ(flags & 1, 0);  // no new coverage
}

TEST_F(InterfaceTest, FinishSampleWithNewCoverage) {
    isSelfMode = true;
    conds_satisfied_max_sample = 2;
    conds_satisfied_max_seed = 1;
    is_efc = true;
    target = 0;
    int initial_seed_count = efc_seed_count;

    int flags = finish_sample();
    EXPECT_TRUE(flags & 1);  // FLAG_NEW_COVERAGE
    EXPECT_EQ(efc_seed_count, initial_seed_count + 1);
}

TEST_F(InterfaceTest, FinishSampleTargetCovered) {
    isSelfMode = true;
    conds_satisfied_max_sample = 0;
    conds_satisfied_max_seed = 0;
    is_efc = false;
    target = 0;

    // Mark target as explored
    explored.insert(0);
    unexplored.erase(0);

    int flags = finish_sample();
    EXPECT_TRUE(flags & 2);  // FLAG_TARGET_COVERED
}

TEST_F(InterfaceTest, FinishSampleAllCovered) {
    isSelfMode = true;
    conds_satisfied_max_sample = 0;
    conds_satisfied_max_seed = 0;
    is_efc = false;
    target = 0;

    // Mark all nodes as explored
    for (int i = 0; i < brCount * 2; ++i) {
        explored.insert(i);
        unexplored.erase(i);
    }

    int flags = finish_sample();
    EXPECT_TRUE(flags & 4);  // FLAG_ALL_COVERED
}

// ============================================================
// Tests for path_helper.py via simple validation
// ============================================================

// (Python tests are in a separate file)

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

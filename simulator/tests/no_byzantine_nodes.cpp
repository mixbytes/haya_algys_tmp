#include <gtest/gtest.h>

#include <string>
#include <iostream>

#include <simulator.hpp>
#include <cstdlib>
#include <ctime>
#include <random>
#include <grandpa.hpp>

using namespace std;

using std::string;

TEST(honest_nodes, eos_finality) {
    auto runner = TestRunner(3);
    vector<pair<int, int> > v0{{1, 2}, {2, 10}};
    graph_type g{v0};
    runner.load_graph(g);
    runner.add_stop_task(4 * TestRunner::SLOT_MS);
    runner.run<Node>();
    EXPECT_EQ(get_block_height(runner.get_db(0).last_irreversible_block_id()), 1);
    EXPECT_EQ(get_block_height(runner.get_db(1).last_irreversible_block_id()), 1);
    EXPECT_EQ(get_block_height(runner.get_db(2).last_irreversible_block_id()), 1);
}

TEST(honest_nodes, eos_master_chain_height) {
    auto runner = TestRunner(3);
    vector<pair<int, int> > v0{{1, 2}, {2, TestRunner::SLOT_MS}};
    graph_type g{v0};
    runner.load_graph(g);
    runner.add_stop_task(TestRunner::SLOT_MS);
    runner.run<Node>();
    EXPECT_EQ(get_block_height(runner.get_db(0).get_master_block_id()), 1);
    EXPECT_EQ(get_block_height(runner.get_db(1).get_master_block_id()), 1);
    EXPECT_EQ(get_block_height(runner.get_db(2).get_master_block_id()), 0);
}

TEST(honest_nodes, grandpa_finality) {
    auto runner = TestRunner(3);
    vector<pair<int, int> > v0{{1, 2}, {2, 10}};
    graph_type g;
    g.push_back(v0);
    runner.load_graph(g);
    runner.add_stop_task(3 * TestRunner::SLOT_MS);
    runner.run<GrandpaNode>();
    EXPECT_EQ(get_block_height(runner.get_db(0).last_irreversible_block_id()), 3);
    EXPECT_EQ(get_block_height(runner.get_db(1).last_irreversible_block_id()), 3);
    EXPECT_EQ(get_block_height(runner.get_db(2).last_irreversible_block_id()), 3);
}
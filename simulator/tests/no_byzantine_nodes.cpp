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

TEST(honest_nodes, eos_finality_many_nodes) {
    size_t nodes_amount = 21;
    auto runner = TestRunner(nodes_amount);
    vector<pair<int, int> > v0{{1, 20}, {2, 10}, {3, 50}, {4, 30}, {5, 100}};
    vector<pair<int, int> > v5{{6, 10}, {7, 30}, {8, 20}, {9, 10}, {10, 100}};
    vector<pair<int, int> > v10{{11, 10}, {12, 10}, {13, 10}, {14, 10}, {15, 100}};
    vector<pair<int, int> > v15{{16, 10}, {17, 10}, {18, 10}, {19, 10}, {20, 10}};
    graph_type g(nodes_amount);
    g[0] = v0;
    g[5] = v5;
    g[10] = v10;
    g[15] = v15;
    runner.load_graph(g);
    runner.add_stop_task(16 * TestRunner::SLOT_MS);
    runner.run<Node>();
    EXPECT_EQ(get_block_height(runner.get_db(0).last_irreversible_block_id()), 1);
    EXPECT_EQ(get_block_height(runner.get_db(5).last_irreversible_block_id()), 1);
    EXPECT_EQ(get_block_height(runner.get_db(10).last_irreversible_block_id()), 1);
    EXPECT_EQ(get_block_height(runner.get_db(15).last_irreversible_block_id()), 1);
}

TEST(honest_nodes, grandpa_finality_many_nodes) {
    size_t nodes_amount = 21;
    auto runner = TestRunner(nodes_amount);
    vector<pair<int, int> > v0{{1, 20}, {2, 10}, {3, 50}, {4, 30}, {5, 100}};
    vector<pair<int, int> > v5{{6, 10}, {7, 30}, {8, 20}, {9, 10}, {10, 100}};
    vector<pair<int, int> > v10{{11, 10}, {12, 10}, {13, 10}, {14, 10}, {15, 100}};
    vector<pair<int, int> > v15{{16, 10}, {17, 10}, {18, 10}, {19, 10}, {20, 10}};
    graph_type g(nodes_amount);
    g[0] = v0;
    g[5] = v5;
    g[10] = v10;
    g[15] = v15;
    runner.load_graph(g);
    runner.add_stop_task(16 * TestRunner::SLOT_MS);
    runner.run<GrandpaNode>();
//    cout << get_block_height(runner.get_db(0).last_irreversible_block_id()) << endl;
    EXPECT_EQ(get_block_height(runner.get_db(0).last_irreversible_block_id()), 16);
    EXPECT_EQ(get_block_height(runner.get_db(5).last_irreversible_block_id()), 16);
    EXPECT_EQ(get_block_height(runner.get_db(10).last_irreversible_block_id()), 16);
    EXPECT_EQ(get_block_height(runner.get_db(15).last_irreversible_block_id()), 16);
}
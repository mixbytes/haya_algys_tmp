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

//TEST(honest_nodes, eos_finality) {
//    auto t = TestRunner(3);
//    vector<pair<int, int> > v0{{1, 2}, {2, 10}};
//    graph_type g;
//    g.push_back(v0);
//    t.load_graph(g);
//    t.add_stop_task(2102);
//    t.run<Node>();
//    for (int i = 0; i < 3; i++) {
//        auto& db = t.get_db(i);
//        EXPECT_EQ(get_block_height(db.last_irreversible_block_id()), 2);
//    }
//}

TEST(honest_nodes, grandpa_finality) {
    auto t = TestRunner(3);
    vector<pair<int, int> > v0{{1, 2}, {2, 10}};
    graph_type g;
    g.push_back(v0);
    t.load_graph(g);
    t.add_stop_task(1503);
    t.run<GrandpaNode>();
    for (int i = 0; i < 3; i++) {
        auto& db = t.get_db(i);
        cout << "Checking node " << i << endl;
        EXPECT_EQ(get_block_height(db.last_irreversible_block_id()), 2);
    }
}

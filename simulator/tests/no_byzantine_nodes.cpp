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

TEST(A, B) {
    auto t = std::make_shared<TestRunner>(3);
    vector<pair<int, int> > v0{{1, 2}, {2, 10}};
    graph_type g;
    g.push_back(v0);
    t->load_graph(g);
    assert(t->get_delay_matrix()[0][1] == 2);
    t->run<GrandpaNode>();
}

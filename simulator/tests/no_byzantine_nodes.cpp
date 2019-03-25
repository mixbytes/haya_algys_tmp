#include <gtest/gtest.h>

#include <string>
#include <iostream>

#include <simulator.hpp>
#include <cstdlib>
#include <ctime>
#include <random>

using namespace std;

using std::string;

TEST(A, B) {
    srand(66);
    TestRunner t(3);
    vector<pair<int, int> > v0{{1, 2}, {2, 5}};
    graph_type g;
    g.push_back(v0);
    t.load_graph(g);
    t.run<>();
}

#include <gtest/gtest.h>

#include <string>
#include <iostream>

#include <simulator.hpp>

using namespace std;

using std::string;

const char *actualValTrue  = "hello gtest";
const char *expectVal      = "hello gtest";

TEST(A, B) {
    TestRunner<> t(3);
    vector<pair<int, int> > v0{{1, 2}, {2, 10}};
    graph_type g;
    g.push_back(v0);
    t.load_graph(g);
    assert(delay_matrix[0][1] == 2);
    t.run();
}

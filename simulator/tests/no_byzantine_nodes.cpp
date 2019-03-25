#include <gtest/gtest.h>

#include <string>
#include <iostream>

#include <simulator.hpp>
#include <cstdlib>
#include <ctime>
#include <random>

using namespace std;

using std::string;

const char *actualValTrue  = "hello gtest";
const char *expectVal      = "hello gtest";

//struct Gen {
//    size_t operator()(size_t n)
//    {
//        return rand() % n;
//    }
//};

TEST(A, B) {
    srand(66);
    TestRunner t(3);
    vector<pair<int, int> > v0{{1, 2}, {2, 5}};
    graph_type g;
    g.push_back(v0);
    t.load_graph(g);
//    vector<int> g{1, 2, 3};
//    for (int i =0; i < 5; i++) {
//        random_shuffle(g.begin(), g.end(), [](size_t n) { return rand() % n; });
//        for (int x : g) {
//            cout << x << " ";
//        }
//        cout << endl;
//    }
//    assert(t.get_dist_matrix()[0][2] == 5);
//    assert(t.get_delay_matrix()[0][1] == 2);
    t.run<>();
}

#pragma once

#include <vector>
#include <functional>
#include <queue>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>

using namespace std;

class Clock {
public:
    Clock(): now_(0) {}
    explicit Clock(uint32_t now): now_(now) {}
    uint32_t now() const {
        return now_;
    }

    void set(uint32_t now) {
        now_ = now;
    }

    void update(uint32_t delta) {
        now_ += delta;
    }
private:
    uint32_t now_;
};

class Node;

struct Task {
    uint32_t from;
    uint32_t to;
    uint32_t at;
    function<void(Node&)> cb;

    bool operator<(const Task& m) const {
        return at < m.at;
    }
};

using matrix_type = vector<vector<int> >;
using graph_type = vector<vector<pair<int, int>>>;

static Clock tester_clock(0);
static vector<vector<int> > delay_matrix;
static priority_queue<Task> timeline;


class Node {
public:
    Node() = default;
    explicit Node(int id): id(id) {}

    template<class T>
    void send(uint32_t to, const T& msg) {
        assert(delay_matrix[id][to] != -1);
        uint32_t delay = static_cast<uint32_t>(delay_matrix[id][to]);
        Task action{id, to};
        action.cb = [&](Node& n) {
            n.on_receive(msg);
        };
        action.at = tester_clock.now() + delay;
        timeline.push(action);
    }

    template<class T>
    void on_receive(const T& msg) {
        std::cout << "Received by " << id << " " << msg << std::endl;
    };

    void on_new_peer_event(uint32_t from) {
        std::cout << "On new peer event handled by " << id << " at " << tester_clock.now() << endl;
    }

    uint32_t id;
    bool is_producer = true;
};

template<class TNode = Node>
class TestRunner {
public:
    TestRunner() = default;
    explicit TestRunner(int instances) {
        init_runner_data(instances);
    }

    explicit TestRunner(const matrix_type& matrix) {
        // TODO check that it's square matrix
        init_runner_data(matrix.size());
        delay_matrix = matrix;
    }

    void load_graph(const graph_type& graph) {
        for (int i = 0; i < graph.size(); i++) {
            for (auto& val : graph[i]) {
                int j = val.first;
                int delay = val.second;
                delay_matrix[i][j] = delay_matrix[j][i] = delay;
            }
        }
    }

    void load_graph_from_file(const char* filename) {
        int instances;
        int from, to, delay;
        ifstream in(filename);
        if (!in) {
            cerr << "Failed to open file";
            exit(1);
        }
        in >> instances;
        init_runner_data(instances);

        while (in >> from >> to >> delay) {
            if (delay != -1) {
                // assume graph is bidirectional
                delay_matrix[from][to] = delay_matrix[to][from] = delay;
            }
        }
    }

    void load_matrix_from_file(const char* filename) {
        ifstream in(filename);
        int instances;
        in >> instances;
        init_runner_data(instances);

        for (int i = 0; i < instances; i++) {
            for (int j = 0; j < instances; j++) {
                in >> delay_matrix[i][j];
            }
        }
    }

    void load_matrix(const matrix_type& matrix) {
        delay_matrix = matrix;
    }

    void run() {
        init_connections();
        while (!timeline.empty()) {
            auto task = timeline.top();
            timeline.pop();
            tester_clock.set(task.at);
            task.cb(nodes[task.to]);
        }
    }

    static int get_instances() {
        return delay_matrix.size();
    }

private:
    void init_connections() {
        for (uint32_t from = 0; from < get_instances(); from++) {
            for (uint32_t to = 0; to < get_instances(); to++) {
                int delay = delay_matrix[from][to];
                if (from != to && delay != -1) {
                    timeline.push(Task{from, to, static_cast<uint32_t>(delay),
                                       [from](Node& n){ n.on_new_peer_event(from); }});
                }
            }
        }
    }

    void init_runner_data(int instances) {
        nodes.resize(instances);
        delay_matrix.resize(instances);

        for (int i = 0; i < instances; i++) {
            delay_matrix[i] = vector<int>(instances, -1);
            delay_matrix[i][i] = 0;
            nodes[i].id = i;
        }
    }
    vector<TNode> nodes;
};


#pragma once

#include <vector>
#include <functional>
#include <queue>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <thread>

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

class TestRunner;
class Node;

using NodePtr = std::shared_ptr<Node>;


struct Task {
    uint32_t from;
    uint32_t to;
    uint32_t at;
    function<void(NodePtr)> cb;

    bool operator<(const Task& m) const {
        return at < m.at;
    }
};

using matrix_type = vector<vector<int> >;
using graph_type = vector<vector<pair<int, int>>>;

static Clock tester_clock(0);


class Network {
public:
    Network() = delete;
    explicit Network(uint32_t node_id, TestRunner* runner):
        node_id(node_id),
        runner(runner)
    { }

    template <typename T>
    void send(uint32_t to, const T&);

    template <typename T>
    void bcast(const T&);

private:
    uint32_t node_id;
    TestRunner* runner;
};


class Node {
public:
    Node() = default;
    explicit Node(int id, Network && net): id(id), net(net) {}

    template <typename T>
    void send(uint32_t to, const T& msg) {
        net.send(to, msg);
    }

    virtual void on_receive(void *) {
        std::cout << "Received by " << id << std::endl;
    }

    virtual void on_new_peer_event(uint32_t from)  {
        std::cout << "On new peer event handled by " << id << " at " << tester_clock.now() << endl;
    }

    uint32_t id;
    bool is_producer = true;
    Network net;
};


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

    template <typename TNode = Node>
    void run() {
        init_nodes<TNode>(get_instances());
        init_connections();
        while (!timeline.empty()) {
            auto task = timeline.top();
            timeline.pop();
            tester_clock.set(task.at);
            task.cb(nodes[task.to]);
        }
    }

    uint32_t get_instances() {
        return delay_matrix.size();
    }

    const matrix_type& get_delay_matrix() const {
        return delay_matrix;
    }

    void add_task(Task && task) {
        timeline.push(task);
    }

private:
    template <typename TNode>
    void init_nodes(uint32_t count) {
        nodes.clear();
        for (auto i = 0; i < count; ++i) {
            auto node = std::make_shared<TNode>(i, Network(i, this));
            nodes.push_back(std::static_pointer_cast<Node>(node));
        }
    }

    void init_connections() {
        for (uint32_t from = 0; from < get_instances(); from++) {
            for (uint32_t to = 0; to < get_instances(); to++) {
                int delay = delay_matrix[from][to];
                if (from != to && delay != -1) {
                    add_task(Task{from, to, static_cast<uint32_t>(delay),
                                       [from](NodePtr n){ n->on_new_peer_event(from); }});
                }
            }
        }
    }

    void init_runner_data(int instances) {
        delay_matrix.resize(instances);

        for (int i = 0; i < instances; i++) {
            delay_matrix[i] = vector<int>(instances, -1);
            delay_matrix[i][i] = 0;
        }
    }

    vector<NodePtr> nodes;
    vector<vector<int> > delay_matrix;
    priority_queue<Task> timeline;
};


template <typename T>
void Network::send(uint32_t to, const T& msg) {
    auto matrix = runner->get_delay_matrix();
    assert(matrix[node_id][to] != -1);

    runner->add_task(Task {
        node_id,
        to,
        tester_clock.now() + matrix[node_id][to],
        [msg](NodePtr n) {
            n->on_receive(msg);
        }
    });
}

template <typename T>
void Network::bcast(const T& msg) {
    //TODO bcast to all nodes with calculate routes
}
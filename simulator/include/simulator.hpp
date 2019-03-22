#pragma once

#include <vector>
#include <functional>
#include <queue>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <thread>
#include <numeric>
#include <chrono>
#include <fc/bitutil.hpp>

#include <database.hpp>

using namespace std;

ostream& operator<<(ostream& os, const block_id_type& block) {
    os << block.str().substr(16, 4);
    return os;
}

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
        return at > m.at;
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
    Network(Network&&) = default;
private:
    uint32_t node_id;
    TestRunner* runner;
};


class Node {
public:
    Node() = default;
    explicit Node(int id, Network && net, fork_db&& db): id(id), net(std::move(net)), db(std::move(db)) {}

    template <typename T>
    void send(uint32_t to, const T& msg) {
        net.send(to, msg);
    }

    void apply_chain(const fork_db_chain_type& chain) {
        stringstream ss;
        ss << "[Node] #" << id << " ";
        auto node_id = ss.str();
        cout << node_id << "Received " << chain.blocks.size() << " blocks " << endl;
        cout << node_id << "Ids [ " << chain.base_block << " -> ";
        for (auto& block_id : chain.blocks) {
            cout << block_id << ", ";
        }
        cout << "]" << endl;
        db.insert(chain);
    }

    virtual void on_receive(void *) {
        std::cout << "Received by " << id << std::endl;
    }

    virtual void on_new_peer_event(uint32_t from)  {
        std::cout << "On new peer event handled by " << id << " at " << tester_clock.now() << endl;
    }

    virtual ~Node() = default;

    uint32_t id;
    bool is_producer = true;
    Network net;
    fork_db db;
};


class TestRunner {
public:
    TestRunner() = default;
    explicit TestRunner(int instances) {
        init_runner_data(instances);
    }

    explicit TestRunner(const matrix_type& matrix) {
        // TODO check that it's square matrix
        delay_matrix = matrix;
        count_dist_matrix();
    }

    void load_graph(const graph_type& graph) {
        for (int i = 0; i < graph.size(); i++) {
            for (auto& val : graph[i]) {
                int j = val.first;
                int delay = val.second;
                delay_matrix[i][j] = delay_matrix[j][i] = delay;
            }
        }
        count_dist_matrix();
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
        count_dist_matrix();
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
        count_dist_matrix();
    }

    void load_matrix(const matrix_type& matrix) {
        delay_matrix = matrix;
        count_dist_matrix();
    }

    fork_db_chain_type create_blocks(NodePtr node) {
        auto& db = node->db;
        stringstream ss;
        ss << "[Node] #" << node->id << " ";
        auto node_id = ss.str();
        cout << node_id << "Generating blocks node_id=" << " at " << tester_clock.now() << endl;
        cout << node_id << "LIB " << db.last_irreversible_block_id() << endl;
        auto head = db.get_master_head();
        auto head_block_height = fc::endian_reverse_u32(head->block_id._hash[0]);
        cout << node_id << "Head block height: " << head_block_height << endl;

        cout << node_id << "Building on top of " << head->block_id << endl;
        cout << node_id << "New blocks: ";

        vector<block_id_type> blocks(blocks_per_slot);
        for (int i = 0; i < blocks_per_slot; i++) {
            auto block_height = head_block_height + i + 1;
            blocks[i] = generate_block(block_height);
            cout << blocks[i] << ", ";
        }
        db.insert(head, blocks);
        cout << endl;
        return fork_db_chain_type{head->block_id, blocks};
    }

    vector<int> get_ordering() {
        vector<int> permutation(get_instances());
        iota(permutation.begin(), permutation.end(), 0);
        random_shuffle(permutation.begin(), permutation.end());
        return permutation;
    }

    void schedule_producers() {
        auto ordering = get_ordering();
        auto now = tester_clock.now();
        auto instances = get_instances();

        for (int i = 0; i < instances; i++) {
            int producer_id = ordering[i];
            Task task;
            task.at = now + i * SLOT_MS;
            task.to = producer_id;
            task.cb = [&](NodePtr node) {
                auto chain = create_blocks(node);
                relay_blocks(node, chain);
                tester_clock.update(SLOT_MS);
            };
            add_task(std::move(task));
        }

        schedule_time = now + instances * SLOT_MS;
    }

    void relay_blocks(NodePtr node, const fork_db_chain_type& chain) {
        uint32_t from = node->id;
        for (uint32_t to = 0; to < get_instances(); to++) {
            if (from != to && dist_matrix[from][to] != -1) {
                Task task{from, to, tester_clock.now() + dist_matrix[from][to]};
                task.cb = [chain=chain](NodePtr node) {
                    node->apply_chain(chain);
                };
                add_task(std::move(task));
            }
        }
    }

    template <typename TNode = Node>
    void run() {
        init_nodes<TNode>(get_instances());
        init_connections();

        schedule_producers();
        while (!timeline.empty()) {
            auto task = timeline.top();
            cout << "[TaskRunner] " << task.at << " " << schedule_time << endl;

            cout << "[TaskRunner] Executing task for " << task.to << endl;
            timeline.pop();
            tester_clock.set(task.at);
            task.cb(nodes[task.to]);

            if (tester_clock.now() == schedule_time) {
                cout << "[TaskRunner] Scheduling PRODUCERS " << endl;
                schedule_producers();
            }

            this_thread::sleep_for(chrono::milliseconds(3000));
        }

//        while (!timeline.empty()) {
//            auto task = timeline.top();
//            timeline.pop();
//            tester_clock.set(task.at);
//            task.cb(nodes[task.to]);
//        }
    }

    uint32_t get_instances() {
        return delay_matrix.size();
    }

    const matrix_type& get_delay_matrix() const {
        return delay_matrix;
    }

    const matrix_type& get_dist_matrix() const {
        return dist_matrix;
    }

    void add_task(Task && task) {
        timeline.push(task);
    }

    const block_id_type genesys_block;
    const int blocks_per_slot = 1;
    const int SLOT_MS = 500;
    const int DELAY_MS = 10;

private:
    block_id_type generate_block(uint32_t block_height) {
        auto block_id = digest_type::hash(fc::crypto::private_key::generate());
        block_id._hash[0] = fc::endian_reverse_u32(block_height);
        return block_id;
    }

    template <typename TNode>
    void init_nodes(uint32_t count) {
        nodes.clear();
        for (auto i = 0; i < count; ++i) {
            auto conf_number = blocks_per_slot * get_instances();
            auto node = std::make_shared<TNode>(i, Network(i, this), fork_db(genesys_block, conf_number));
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

        dist_matrix = delay_matrix;
    }

    void count_dist_matrix() {
        int n = get_instances();
        dist_matrix = delay_matrix;

        for (int k = 0; k < n; ++k) {
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    if (dist_matrix[i][k] != -1 && dist_matrix[k][j] != -1) {
                        auto new_dist = dist_matrix[i][k] + dist_matrix[k][j];
                        auto& cur_dist = dist_matrix[i][j];
                        if (cur_dist == -1) {
                            cur_dist = new_dist;
                        } else {
                            cur_dist = min(cur_dist, new_dist);
                        }
                    }
                }
            }
        }
    }

    vector<NodePtr> nodes;
    matrix_type delay_matrix;
    matrix_type dist_matrix;
    priority_queue<Task> timeline;
    uint32_t schedule_time = 0;
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
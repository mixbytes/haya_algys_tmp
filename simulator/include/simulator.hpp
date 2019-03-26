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
#include <boost/optional.hpp>

#include <database.hpp>

using namespace std;

ostream& operator<<(ostream& os, const block_id_type& block) {
    os << block.str().substr(16, 4);
    return os;
}

uint32_t get_block_height(const block_id_type& id) {
    return fc::endian_reverse_u32(id._hash[0]);
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
    TestRunner* get_runner() const {
        return runner;
    }
private:
    uint32_t node_id;
    TestRunner* runner;
};


class Node {
public:
    Node() = default;
    explicit Node(int id, Network && net, fork_db&& db): id(id), net(std::move(net)), db(std::move(db)) {}
    virtual ~Node() = default;

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
        for (auto& block_id : chain.blocks) {
            on_accepted_block_event(block_id);
        }
    }

    Clock get_clock() const;

    virtual void on_receive(uint32_t from, void *) {
        std::cout << "Received from " << from << std::endl;
    }

    virtual void on_new_peer_event(uint32_t from) {
        std::cout << "On new peer event handled by " << id << " at " << get_clock().now() << endl;
    }

    virtual void on_accepted_block_event(block_id_type id) {
        std::cout << "On accepted block event handled by " << id << " at " << get_clock().now() << endl;
    }

    uint32_t id;
    bool is_producer = true;
    Network net;
    fork_db db;
};

class TestRunner {
public:
    TestRunner() = default;
    explicit TestRunner(int instances, size_t blocks_per_slot_ = 1) :
        blocks_per_slot(blocks_per_slot_) {
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
        cout << node_id << "Generating blocks node_id=" << " at " << clock.now() << endl;
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
        for (auto& block_id : blocks) {
            node->on_accepted_block_event(block_id);
        }
        cout << endl;
        return fork_db_chain_type{head->block_id, blocks};
    }

    vector<int> get_ordering() {
        vector<int> permutation(get_instances());
        iota(permutation.begin(), permutation.end(), 0);
        random_shuffle(permutation.begin(), permutation.end(), [](size_t n) { return rand() % n; });
        return permutation;
    }

    void add_schedule_task(uint32_t at) {
        Task task{RUNNER_ID, RUNNER_ID, at,
                  [&](NodePtr n) { schedule_producers(); }
                  };
        add_task(std::move(task));
    }

    void add_stop_task(uint32_t at) {
        Task task{RUNNER_ID, RUNNER_ID, DELAY_MS + at,
                  [&](NodePtr n) { should_stop = true; }
                 };
        add_task(std::move(task));
    }

    void add_update_delay_task(uint32_t at, size_t row, size_t col, int delay) {
        Task task{RUNNER_ID, RUNNER_ID, DELAY_MS + at,
                  [&](NodePtr n) { delay_matrix[row][col] = delay_matrix[col][row] = delay; }
        };
        add_task(std::move(task));
    }

    void schedule_producers() {
        cout << "[TaskRunner] Scheduling PRODUCERS " << endl;
        auto ordering = get_ordering();
        auto now = clock.now();
        auto instances = get_instances();

        for (int i = 0; i < instances; i++) {
            int producer_id = ordering[i];
            Task task;
            task.at = now + i * SLOT_MS;
            task.to = producer_id;
            task.cb = [&](NodePtr node) {
                auto chain = create_blocks(node);
                relay_blocks(node, chain);
            };
            add_task(std::move(task));
        }

        schedule_time = now + instances * SLOT_MS;
        add_schedule_task(schedule_time);
    }

    void relay_blocks(NodePtr node, const fork_db_chain_type& chain) {
        uint32_t from = node->id;
        for (uint32_t to = 0; to < get_instances(); to++) {
            if (from != to && dist_matrix[from][to] != -1) {
                Task task{from, to, clock.now() + dist_matrix[from][to]};
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

        add_schedule_task(schedule_time);

        while (!should_stop) {
            auto task = timeline.top();
            cout << "[TaskRunner] " << "current_time=" << task.at << " schedule_time=" << schedule_time << endl;
            timeline.pop();
            clock.set(task.at);
            if (task.to == RUNNER_ID) {
                cout << "[TaskRunner] Executing task for " << "TaskRunner" << endl;
                task.cb(nullptr);
            } else {
                cout << "[TaskRunner] Executing task for " << task.to << endl;
                task.cb(nodes[task.to]);
            }

            this_thread::sleep_for(chrono::milliseconds(0));
        }
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

    const vector<NodePtr> get_nodes() const {
        return nodes;
    }

    NodePtr get_node(size_t index) {
        return nodes[index];
    }

    const fork_db& get_db(size_t index) {
        return nodes[index]->db;
    }

    const Clock& get_clock() {
        return clock;
    }

    void add_task(Task && task) {
        timeline.push(task);
    }

    size_t bft_threshold() {
        return 2 * get_instances() / 3 + 1;
    }

    const block_id_type genesys_block;
    const uint32_t RUNNER_ID = 10000000;

    static const uint32_t SLOT_MS = 500;
    static const uint32_t DELAY_MS = 500;

    size_t blocks_per_slot;
    bool should_stop = false;


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
            auto conf_number = blocks_per_slot * bft_threshold();
            auto node = std::make_shared<TNode>(i, Network(i, this), fork_db(genesys_block, conf_number));
            nodes.push_back(std::static_pointer_cast<Node>(node));
        }
    }

    void init_connections() {
        for (uint32_t from = 0; from < get_instances(); from++) {
            for (uint32_t to = 0; to < get_instances(); to++) {
                int delay = delay_matrix[from][to];
                if (from != to && delay != -1) {
                    add_task(Task{from, to, static_cast<uint32_t>(0),
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
    uint32_t schedule_time = DELAY_MS;
    Clock clock;
};


template <typename T>
void Network::send(uint32_t to, const T& msg) {
    auto matrix = runner->get_delay_matrix();
    assert(matrix[node_id][to] != -1);

    runner->add_task(Task {
        node_id,
        to,
        get_runner()->get_clock().now() + matrix[node_id][to],
        [node_id = node_id, msg = msg](NodePtr n) {
            n->on_receive(node_id, (void*)&msg);
        }
    });
}

template <typename T>
void Network::bcast(const T& msg) {
    //TODO bcast to all nodes with calculate routes
}

Clock Node::get_clock() const {
    return net.get_runner()->get_clock();
}
#pragma once

#include "types.hpp"
#include <memory>
#include <vector>
#include <map>
#include <set>

using std::vector;
using std::shared_ptr;
using std::pair;
using std::make_pair;
using std::set;

template <typename ConfType>
class prefix_node {
public:
    using conf_type = ConfType;
private:
    using node_ptr = shared_ptr<prefix_node<conf_type>>;

public:
    block_id_type block_id;
    std::map<public_key_type, shared_ptr<conf_type>> confirmation_data;
    vector<node_ptr> adjacent_nodes;
    node_ptr parent;
    public_key_type creator_key;
    set<public_key_type> active_bp_keys;

    size_t confirmation_number() const {
        return confirmation_data.size();
    }

    node_ptr get_matching_node(block_id_type block_id) {
        for (const auto& node : adjacent_nodes) {
            if (node->block_id == block_id) {
                return node;
            }
        }
        return nullptr;
    }

    bool has_confirmation(const public_key_type& pub_key ) {
        return confirmation_data.find(pub_key) != confirmation_data.end();
    }
};

struct chain_type {
    block_id_type base_block;
    vector<block_id_type> blocks;
};

class NodeNotFoundError : public std::exception {};

template <typename NodeType>
class prefix_chain_tree {
private:
    using node_ptr = shared_ptr<NodeType>;
    using conf_ptr = shared_ptr<typename NodeType::conf_type>;

    struct node_info {
        node_ptr node;
        size_t height;
    };

public:
    explicit prefix_chain_tree(node_ptr root_): root(std::move(root_)) {}
    prefix_chain_tree() = delete;
    prefix_chain_tree(const prefix_chain_tree&) = delete;

    auto find(const block_id_type& block_id) const {
        return find_node(block_id, root);
    }

    node_ptr add_confirmations(const chain_type& chain, const public_key_type& sender_key, const conf_ptr& conf) {
        node_ptr node = nullptr;
        vector<block_id_type> blocks;
        std::tie(node, blocks) = get_tree_node(chain);
        if (!node) {
            dlog("Cannot find base block");
            return nullptr;
        }
        return _add_confirmations(node, blocks, sender_key, conf);
    }

    void insert(const chain_type& chain, const public_key_type& creator_key, const set<public_key_type>& active_bp_keys) {
        node_ptr node = nullptr;
        vector<block_id_type> blocks;
        std::tie(node, blocks) = get_tree_node(chain);

        if (!node) {
            throw NodeNotFoundError();
        }

        insert_blocks(node, blocks, creator_key, active_bp_keys);
    }

    auto get_final_chain_head(size_t confirmation_number) const {
        auto head = get_chain_head(root, confirmation_number, 0).node;
        return head != root ? head : nullptr;
    }

    auto get_root() const {
        return root;
    }

    auto set_root(const node_ptr& new_root) {
        root = new_root;
        if (new_root->parent) {
            new_root->parent.reset();
        }
    }

private:
    node_ptr root;

    pair<node_ptr, vector<block_id_type> > get_tree_node(const chain_type& chain) {
        auto node = find(chain.base_block);
        const auto& blocks = chain.blocks;

        if (node) {
            return {node, std::move(chain.blocks)};
        }

        auto block_itr = std::find_if(blocks.begin(), blocks.end(), [&](const auto& block) {
            return (bool) find(block);
        });

        if (block_itr != blocks.end()) {
            dlog("Found node: ${id}", ("id", *block_itr));
            return { find(*block_itr), vector<block_id_type>(block_itr + 1, blocks.end()) };
        }
        return {nullptr, {} };
    }

    node_info get_chain_head(const node_ptr& node, size_t confirmation_number, size_t depth) const {
        auto result = node_info{node, depth};
        for (const auto& adjacent_node : node->adjacent_nodes) {
            if (adjacent_node->confirmation_number() < confirmation_number) {
                continue;
            }
            const auto head_node = get_chain_head(adjacent_node, confirmation_number, depth + 1);
            if (head_node.height > result.height) {
                result = head_node;
            }
        }
        return result;
    }

    node_ptr find_node(const block_id_type& block_id, node_ptr node) const {
        if (block_id == node->block_id) {
            return node;
        }
        for (const auto& adjacent_node : node->adjacent_nodes) {
            auto result = find_node(block_id, adjacent_node);
            if (result) {
                return result;
            }
        }
        return nullptr;
    }

    void insert_blocks(node_ptr node, const vector<block_id_type>& blocks, const public_key_type& creator_key,
            const set<public_key_type>& active_bp_keys) {
        for (const auto& block_id : blocks) {
            dlog("Block, id: ${id}", ("id", block_id));
            auto next_node = node->get_matching_node(block_id);
            if (!next_node) {
                next_node = std::make_shared<NodeType>(NodeType{block_id,
                                                                      {},
                                                                      {},
                                                                      node,
                                                                      creator_key,
                                                                      active_bp_keys});
                node->adjacent_nodes.push_back(next_node);
            }
            node = next_node;
        }
    }

    node_ptr _add_confirmations(node_ptr node, const vector<block_id_type>& blocks, const public_key_type& sender_key,
                           const conf_ptr& conf) {
        auto max_conf_node = node;
        node->confirmation_data[sender_key] = conf;
        dlog("Base block confirmations, id: ${id}, count: ${count}", ("id", node->block_id)("count", node->confirmation_data.size()));

        for (const auto& block_id : blocks) {
            dlog("Block, id: ${id}", ("id", block_id));
            node = node->get_matching_node(block_id);
            if (!node) {
                break;
            }
            dlog("Confirmations, count: ${count}", ("count", node->confirmation_data.size()));
            node->confirmation_data[sender_key] = conf;
            if (max_conf_node->confirmation_data.size() <= node->confirmation_data.size()) {
                max_conf_node = node;
            }
        }

        return max_conf_node;
    }
};
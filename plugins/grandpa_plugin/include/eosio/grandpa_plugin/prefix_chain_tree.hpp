#pragma once

#include "types.hpp"
#include <memory>
#include <vector>
#include <map>


using std::vector;
using std::shared_ptr;
using std::pair;
using std::make_pair;


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

    node_ptr insert(const chain_type& chain, const public_key_type& pub_key, const conf_ptr& conf) {
        auto node = find(chain.base_block);
        vector<block_id_type> blocks;

        if (!node) {
            auto block_itr = std::find_if(chain.blocks.begin(), chain.blocks.end(), [&](const auto& block) {
                return (bool) find(block);
            });

            if (block_itr != chain.blocks.end()) {
                dlog("Found node: ${id}", ("id", *block_itr));
                blocks.insert(blocks.end(), block_itr + 1, chain.blocks.end());
                node = find(*block_itr);
            }
        }
        else {
            blocks = chain.blocks;
        }

        if (!node) {
            throw NodeNotFoundError();
        }

        return insert_blocks(node, blocks, pub_key, conf);
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

    node_ptr insert_blocks(node_ptr node, const vector<block_id_type>& blocks, const public_key_type& pub_key, const conf_ptr& conf) {
        auto max_conf_node = node;
        node->confirmation_data[pub_key] = conf;
        dlog("Confirmations, id: ${id}, count: ${count}", ("id", node->block_id)("count", node->confirmation_data.size()));

        for (const auto& block_id : blocks) {
            auto next_node = node->get_matching_node(block_id);
            if (next_node) {
                dlog("Confirmations, id: ${id}, count: ${count}", ("id", next_node->block_id)("count", next_node->confirmation_data.size()));
                next_node->confirmation_data[pub_key] = conf;
                if (max_conf_node->confirmation_data.size() <= next_node->confirmation_data.size()) {
                    max_conf_node = next_node;
                }
            } else {
                next_node = std::make_shared<NodeType>(NodeType{block_id,
                                                                      {{pub_key, conf}},
                                                                      {},
                                                                      node});
                node->adjacent_nodes.push_back(next_node);
            }
            node = next_node;
        }

        return max_conf_node;
    }
};
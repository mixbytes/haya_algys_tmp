#pragma once

#include "types.hpp"
#include <memory>
#include <vector>
#include <map>

struct prefix_node;
struct chain_type;

using std::vector;
using std::shared_ptr;
using std::pair;
using std::make_pair;

using prefix_node_ptr = shared_ptr<prefix_node>;
using chain_type_ptr = shared_ptr<chain_type>;

struct prefix_node {
    block_id_type block_id;
    std::map<public_key_type, chain_type_ptr> confirmation_data;
    vector<prefix_node_ptr> adjacent_nodes;
    prefix_node_ptr parent;

    size_t confirmation_number() const {
        return confirmation_data.size();
    }

    prefix_node_ptr get_matching_node(block_id_type block_id) {
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

struct node_info {
    prefix_node_ptr node;
    size_t height;
};

struct chain_type {
    block_id_type base_block;
    vector<block_id_type> blocks;
    signature_type signature;
};

class NodeNotFoundError : public std::exception {};

class prefix_chain_tree {
public:
    explicit prefix_chain_tree(prefix_node_ptr root_): root(std::move(root_)) {}
    prefix_chain_tree() = delete;
    prefix_chain_tree(const prefix_chain_tree&) = delete;

    auto find(const block_id_type& block_id) const {
        return find_node(block_id, root);
    }

    prefix_node_ptr insert(const chain_type_ptr& chain, const public_key_type& pub_key) {
        auto node = find(chain->base_block);
        vector<block_id_type> blocks;

        if (!node) {
            auto block_itr = std::find_if(chain->blocks.begin(), chain->blocks.end(), [&](const auto& block) {
                return (bool) find(block);
            });

            if (block_itr != chain->blocks.end()) {
                dlog("Found node: ${id}", ("id", *block_itr));
                blocks.insert(blocks.end(), block_itr + 1, chain->blocks.end());
                node = find(*block_itr);
            }
        }
        else {
            blocks = chain->blocks;
        }

        if (!node) {
            throw NodeNotFoundError();
        }

        return insert_blocks(node, chain, blocks, pub_key);
    }

    auto get_final_chain_head(size_t confirmation_number) const {
        auto head = get_chain_head(root, confirmation_number, 0).node;
        return head != root ? head : nullptr;
    }

    auto get_root() const {
        return root;
    }

    auto set_root(const prefix_node_ptr& new_root) {
        root = new_root;
        if (new_root->parent) {
            new_root->parent.reset();
        }
    }

private:
    prefix_node_ptr root;

    node_info get_chain_head(const prefix_node_ptr& node, size_t confirmation_number, size_t depth) const {
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

    vector<prefix_node_ptr> construct_chain(prefix_node_ptr node) const {
        vector<prefix_node_ptr> result;
        while (node != root) {
            result.push_back(node);
            node = node->parent;
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

    prefix_node_ptr find_node(const block_id_type& block_id, prefix_node_ptr node) const {
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

    prefix_node_ptr insert_blocks(prefix_node_ptr node, const chain_type_ptr& chain, const vector<block_id_type>& blocks, const public_key_type& pub_key) {
        auto max_conf_node = node;
        node->confirmation_data[pub_key] = chain;
        dlog("Confirmations, id: ${id}, count: ${count}", ("id", node->block_id)("count", node->confirmation_data.size()));

        for (const auto& block_id : blocks) {
            auto next_node = node->get_matching_node(block_id);
            if (next_node) {
                dlog("Confirmations, id: ${id}, count: ${count}", ("id", next_node->block_id)("count", next_node->confirmation_data.size()));
                next_node->confirmation_data[pub_key] = chain;
                if (max_conf_node->confirmation_data.size() <= next_node->confirmation_data.size()) {
                    max_conf_node = next_node;
                }
            } else {
                next_node = std::make_shared<prefix_node>(prefix_node{block_id,
                                                                      {{pub_key, chain}},
                                                                      {},
                                                                      node});
                node->adjacent_nodes.push_back(next_node);
            }
            node = next_node;
        }

        return max_conf_node;
    }
};
#pragma once
#include "types.hpp"
#include "prefix_chain_tree.hpp"
#include "network_messages.hpp"

using tree_node = prefix_node<prevote_msg>;
using prefix_tree = prefix_chain_tree<tree_node>;

using tree_node_ptr = std::shared_ptr<tree_node>;
using prefix_tree_ptr = std::shared_ptr<prefix_tree>;

using grandpa_round_ptr = std::shared_ptr<class grandpa_round>;

class grandpa_round {
public:
    enum class state {
        prevote, // prevote state (init state)
        ready_to_precommit, // ready to precommit (prevote -> ready_to_precommit)
        precommit, // precommit stage (ready_to_precommit -> precommit)
        done,   // we have supermajority (precommit -> done)
        fail,   // we failed (precommit -> fail | prevote -> fail)
    };

    struct proof {
        uint32_t round_num;
        std::vector<prevote_msg> prevotes;
        std::vector<precommit_msg> precommites;
    };

public:
    grandpa_round(uint32_t num, const public_key_type& primary, prefix_tree_ptr tree):
        num(num),
        primary(primary),
        tree(tree)
    {
        dlog("Grandpa round started, num: ${n}, primary: ${p}",
            ("n", num)
            ("p", primary)
        );

        prevote();
    }

    uint32_t get_num() const {
        return num;
    }

    state get_state() const {
        return state;
    }

    proof get_proof() {
        FC_ASSERT(state == state::done, "state should be `done`");

        return proof;
    }

    void on(const prevote_msg& msg) {
        dlog("Received prevote: msg: ${m}", ("m", msg));

        if (state >= state::precommit) {
            return;
        }

        if (!validate_prevote(msg)) {
            return;
        }

        auto max_prevote_node = tree.add_confirmations({ msg.data.base_block, msg.data.blocks },
                                msg.public_key(), std::make_shared<prevote_msg>(msg));

        FC_ASSERT(max_prevote_node, "confirmation should be insertable after validate");

        prevoted_keys.insert(msg.public_key());

        if (has_threshold_prevotes(max_prevote_node)) {
            state = state::ready_to_precommit;
            best_node = max_prevote_node;
            return;
        }
    }

    void on(const precommit_msg& msg) {
        dlog("Received precommit, msg: ${m}", ("m", msg));

        if (state != state::precommit) {
            return;
        }

        if (!validate_precommit(msg)) {
            return;
        }

        precommited_keys.insert(msg.public_key());
        proof.precommites.push_back(msg);

        if (proof.precommites.size() > 2 * best_node->active_bp_keys.size() / 3) {
            state = state::done;
        }
    }

    void end_prevote() {
        if (state != state::ready_to_precommit) {
            dlog("Round failed, num: ${n}, state: ${s}",
                ("n", num)
                ("s", static_cast<uint32_t>(state))
            );
            state = state::fail;
            return;
        }

        std::transform(best_node->confirmation_data.begin(), best_node->confirmation_data.end(),
            std::back_inserter(proof.prevotes), [](const auto& item) -> prevote_msg { return *item.second; });

        precommit();
    }

    void finish() {
        if (state != state::done) {
            dlog("Round failed, num: ${n}, state: ${s}",
                ("n", num)
                ("s", static_cast<uint32_t>(state))
            );
            state = state::fail;
            return;
        }
    }

private:
    void prevote() {
        dlog("Round sending prevote, num: ${n}", ("n", num));
        //TODO find best chain and bcast prevote
    }

    void precommit() {
        dlog("Round sending precommit, num: ${n}", ("n", num));
        //TODO do precommit for founded prevote
    }

    bool validate_prevote(const prevote_msg& msg) {
        if (num != msg.data.round_num) {
            dlog("Grandpa received prevote for wrong round, received for: ${rr}, expected: ${er}",
                ("rr", msg.data.round_num)
                ("er", num)
            );
            return false;
        }

        if (prevoted_keys.count(msg.public_key())) {
            dlog("Grandpa received prevote second time for key");
            return false;
        }

        auto node = find_last_node(msg.data.blocks);

        if (!node) {
            dlog("Grandpa received prevote for unknown blocks");
            return false;
        }

        if (!node->active_bp_keys.count(msg.public_key())) {
            dlog("Grandpa received prevote for block from not active producer, id : ${id}",
                ("id", node->block_id)
            );
            return false;
        }

        return true;
    }

    bool validate_precommit(const precommit_msg& msg) {
        if (num != msg.data.round_num) {
            dlog("Grandpa received precommit for wrong round, received for: ${rr}, expected: ${er}",
                ("rr", msg.data.round_num)
                ("er", num)
            );
            return false;
        }

        if (precommited_keys.count(msg.public_key())) {
            dlog("Grandpa received precommit second time for key");
            return false;
        }

        if (msg.data.block_id != best_node->block_id) {
            dlog("Grandpa received precommit for not best block, id: ${id}, best_id: ${best_id}",
                ("id", msg.data.block_id)
                ("best_id", best_node->block_id)
            );
            return false;
        }

        if (!best_node->has_confirmation(msg.public_key())) {
            dlog("Grandpa received precommit from not prevoted peer");
            return false;
        }

        return true;
    }

    tree_node_ptr find_last_node(const vector<block_id_type>& blocks) {
        auto block_itr = std::find_if(blocks.rend(), blocks.rbegin(),
        [&](const auto& block_id) {
            return (bool) tree.find(block_id);
        });

        if (block_itr == blocks.rend()) {
            return nullptr;
        }

        return tree.find(*block_itr);
    }

    bool has_threshold_prevotes(const tree_node_ptr& node) {
        return node->confirmation_number() > 2 * node->active_bp_keys.size() / 3;
    }

    uint32_t num { 0 };
    public_key_type primary;
    prefix_tree_ptr tree;
    state state { state::prevote };
    proof proof;
    tree_node_ptr best_node;

    std::set<public_key_type> prevoted_keys;
    std::set<public_key_type> precommited_keys;
};

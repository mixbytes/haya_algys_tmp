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
        precommit, // precommit stage (prevote -> precommit)
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
        if (!validate_prevote(msg)) {
            return;
        }

        dlog("Received prevote: msg: ${m}", ("m", msg));

        //TODO add prevote and maybe go to precommit
    }

    void on(const precommit_msg& msg) {
        if (!validate_precommit(msg)) {
            return;
        }

        dlog("Received precommit, msg: ${m}", ("m", msg));

        //TODO add precommit and maybe go to done
    }

    void end_prevote() {
        if (state < state::precommit) {
            dlog("Round failed, num: ${n}, state: ${s}",
                ("n", num)
                ("s", static_cast<uint32_t>(state))
            );
            state = state::fail;
            return;
        }
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

        //TODO contruct proof
    }

private:
    void prevote() {
        dlog("Round sending prevote, num: ${n}", ("n", num));
        //TODO find best chain and bcast prevote
    }

    bool validate_prevote(const prevote_msg& msg) {
        if (num != msg.data.round_num) {
            dlog("Grandpa received prevote for wrong round, received for: ${rr}, expected: ${er}",
                ("rr", msg.data.round_num)
                ("er", num)
            );
            return false;
        }

        //TODO validate public key

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

        return true;
    }

    uint32_t num { 0 };
    public_key_type primary;
    prefix_tree_ptr tree;
    state state { state::prevote };
    proof proof;
};

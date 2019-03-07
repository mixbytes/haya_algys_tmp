#pragma once

#include <prefix_chain_tree.hpp>
#include <eosio/chain/types.hpp>

namespace eosio {

using namespace chain;

struct chain_confirmation_type : public chain_type {
    signature_type signature;
};

struct block_confirmation_type {
    block_id_type block_id;
};

struct handshake_type {
    block_id_type lib;
    signature_type signature;
};

}
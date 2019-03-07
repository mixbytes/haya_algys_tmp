#pragma once

#include <eosio/grandpa_plugin/prefix_chain_tree.hpp>
#include <eosio/chain/types.hpp>
#include <fc/reflect/reflect.hpp>

namespace eosio {

using namespace chain;


struct chain_conf_msg : public chain_type {
    signature_type signature;
};

struct block_get_conf_msg {
    block_id_type block_id;
};

struct handshake_msg {
    block_id_type lib;
    signature_type signature;
};

}

FC_REFLECT(eosio::chain_type, (base_block)(blocks))
FC_REFLECT_DERIVED(eosio::chain_conf_msg, (eosio::chain_type), (signature));
FC_REFLECT(eosio::block_get_conf_msg,  (block_id));
FC_REFLECT(eosio::handshake_msg, (lib)(signature));

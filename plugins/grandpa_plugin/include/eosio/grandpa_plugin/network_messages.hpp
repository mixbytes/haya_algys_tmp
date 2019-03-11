#pragma once

#include <eosio/grandpa_plugin/prefix_chain_tree.hpp>
#include <eosio/chain/types.hpp>
#include <fc/reflect/reflect.hpp>

namespace eosio {

using namespace chain;


struct chain_conf_msg : public chain_type {
    chain_conf_msg() = default;
    chain_conf_msg(const chain_type& chain, const signature_type& sign) {
        base_block = chain.base_block;
        blocks = chain.blocks;
        signature = sign;
    }

    signature_type signature;
};

struct block_get_conf_msg {
    block_id_type block_id;
};

struct handshake_msg {
    block_id_type lib;
    signature_type signature;
};


using chain_conf_msg_ptr = shared_ptr<chain_conf_msg>;

chain_conf_msg_ptr make_confirmation(const chain_type& chain, const private_key_type& priv_key) {
    auto hash = chain::digest_type::hash(chain);
    auto conf_ptr = std::make_shared<chain_conf_msg>(chain, priv_key.sign(hash));
    return conf_ptr;
}

bool validate_confirmation(const chain_conf_msg& chain_conf, const public_key_type& pub_key) {
    auto chain = chain_type {chain_conf.base_block, chain_conf.blocks};
    auto hash = chain::digest_type::hash(chain);
    return public_key_type(chain_conf.signature, hash) == pub_key;
}

}

FC_REFLECT(eosio::chain_type, (base_block)(blocks))
FC_REFLECT_DERIVED(eosio::chain_conf_msg, (eosio::chain_type), (signature));
FC_REFLECT(eosio::block_get_conf_msg,  (block_id));
FC_REFLECT(eosio::handshake_msg, (lib)(signature));

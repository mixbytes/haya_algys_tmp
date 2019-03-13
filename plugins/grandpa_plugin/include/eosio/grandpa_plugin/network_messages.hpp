#pragma once

#include <eosio/grandpa_plugin/prefix_chain_tree.hpp>
#include <eosio/chain/types.hpp>
#include <fc/reflect/reflect.hpp>

namespace eosio {

using namespace chain;

template<class T>
class network_msg {
public:
    T data;
    signature_type signature;
    network_msg() = default;
    network_msg(const T& data_, const signature_type& signature_): data(data_), signature(signature_) {}
    network_msg(const T& data_, signature_type&& signature_): data(data_), signature(signature_) {}
};


struct block_get_conf_type {
    block_id_type block_id;
};

struct handshake_type {
    block_id_type lib;
};

struct confirmation_type {
    block_id_type base_block;
    vector<block_id_type> blocks;
};

using handshake_msg = network_msg<handshake_type>;
using block_get_conf_msg = network_msg<block_get_conf_type>;
using chain_conf_msg = network_msg<confirmation_type>;


using chain_conf_msg_ptr = shared_ptr<chain_conf_msg>;

template<class T>
auto get_public_key(const T& msg) {
    auto hash = chain::digest_type::hash(msg.data);
    return public_key_type(msg.signature, hash);
}

template<class T>
auto make_network_msg(const T& data, const private_key_type& priv_key) {
    auto hash = chain::digest_type::hash(data);
    return network_msg<T>(data, priv_key.sign(hash));
}

template<class T>
bool validate_network_msg(const T& msg, const public_key_type& pub_key) {
    auto hash = chain::digest_type::hash(msg.data);
    return public_key_type(msg.signature, hash) == pub_key;
}

}

FC_REFLECT(eosio::confirmation_type, (base_block)(blocks))
FC_REFLECT(eosio::block_get_conf_type, (block_id))
FC_REFLECT(eosio::handshake_type, (lib))
FC_REFLECT_TEMPLATE((typename T), eosio::network_msg<T>, (data)(signature))


#pragma once

#include <eosio/grandpa_plugin/types.hpp>
#include <eosio/grandpa_plugin/prefix_chain_tree.hpp>
#include <eosio/chain/types.hpp>
#include <fc/reflect/reflect.hpp>

using std::vector;


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

struct handshake_ans_type {
    block_id_type lib;
};

struct confirmation_type {
    block_id_type base_block;
    vector<block_id_type> blocks;
};

using handshake_msg = network_msg<handshake_type>;
using handshake_ans_msg = network_msg<handshake_ans_type>;
using block_get_conf_msg = network_msg<block_get_conf_type>;
using chain_conf_msg = network_msg<confirmation_type>;


using chain_conf_msg_ptr = shared_ptr<chain_conf_msg>;

template<class T>
auto get_public_key(const T& msg) {
    auto hash = digest_type::hash(msg.data);
    return public_key_type(msg.signature, hash);
}

template<class T>
auto make_network_msg(const T& data, const private_key_type& priv_key) {
    auto hash = digest_type::hash(data);
    return network_msg<T>(data, priv_key.sign(hash));
}

template<class T>
bool validate_network_msg(const T& msg, const public_key_type& pub_key) {
    auto hash = digest_type::hash(msg.data);
    return public_key_type(msg.signature, hash) == pub_key;
}


FC_REFLECT(confirmation_type, (base_block)(blocks))
FC_REFLECT(block_get_conf_type, (block_id))
FC_REFLECT(handshake_type, (lib))
FC_REFLECT(handshake_ans_type, (lib))
FC_REFLECT_TEMPLATE((typename T), network_msg<T>, (data)(signature))


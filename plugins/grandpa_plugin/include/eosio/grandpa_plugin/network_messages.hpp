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
    network_msg(const T& data_, const private_key_type& priv_key) {
        data = data_;
        signature = priv_key.sign(hash());
    }

    digest_type hash() const {
        return digest_type::hash(data);
    }

    public_key_type public_key() const {
        return public_key_type(signature, hash());
    }

    bool validate(const public_key_type& pub_key) const {
        return public_key() == pub_key;
    }
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


FC_REFLECT(confirmation_type, (base_block)(blocks))
FC_REFLECT(block_get_conf_type, (block_id))
FC_REFLECT(handshake_type, (lib))
FC_REFLECT(handshake_ans_type, (lib))
FC_REFLECT_TEMPLATE((typename T), network_msg<T>, (data)(signature))


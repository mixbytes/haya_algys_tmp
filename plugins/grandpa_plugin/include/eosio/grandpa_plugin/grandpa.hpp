#pragma once
#include "network_messages.hpp"
#include "prefix_chain_tree.hpp"
#include <fc/exception/exception.hpp>
#include <fc/io/json.hpp>
#include <fc/bitutil.hpp>
#include <queue>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <appbase/application.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

using appbase::app;
using eosio::chain_plugin;

using ::fc::static_variant;
using std::shared_ptr;
using std::unique_ptr;
using std::pair;

using mutex_guard = std::lock_guard<std::mutex>;

template <typename message_type>
class message_queue {
public:
    using message_ptr = std::shared_ptr<message_type>;

public:
    template <typename T>
    void push_message(const T& msg) {
        mutex_guard lock(_message_queue_mutex);

        auto grandpa_msg = std::make_shared<message_type>(msg);
        _message_queue.push(grandpa_msg);

        if (_need_notify) {
            _new_msg_cond.notify_one();
        }
    }

    message_ptr get_next_msg() {
        mutex_guard lock(_message_queue_mutex);

        if (!_message_queue.size()) {
            _need_notify = true;
            return nullptr;
        } else {
            _need_notify = false;
        }

        auto msg = _message_queue.front();
        _message_queue.pop();

        return msg;
    }

    message_ptr get_next_msg_wait() {
        while (true) {
            if (_need_notify) {
                std::unique_lock<std::mutex> lk(_message_queue_mutex);

                _new_msg_cond.wait(lk, [this](){
                    return (bool)_message_queue.size() || _done;
                });
            }

            if (_done)
                return nullptr;

            auto msg = get_next_msg();

            if (msg) {
                return msg;
            }
        }
    }

    void terminate() {
        _done = true;
        _new_msg_cond.notify_one();
    }

private:
    std::mutex _message_queue_mutex;
    bool _need_notify = true;
    std::condition_variable _new_msg_cond;
    std::queue<message_ptr> _message_queue;
    std::atomic<bool> _done { false };
};


template <typename ...Args>
class channel {
public:
    using cb_type = std::function<void(Args...)>;

    void subscribe(cb_type && cb) {
        cbs.emplace_back(cb);
    }

    void send(Args... args) {
        for (auto && cb: cbs) {
            cb(std::forward<Args>(args)...);
        }
    }

protected:
    std::vector<cb_type> cbs;
};


template <typename T, typename ...Args>
class provider {
public:
    using cb_type = std::function<T(Args...)>;

public:
    provider(cb_type && _cb) {
        cb = _cb;
    }

    T get(Args... args) {
        return cb(args...);
    }

protected:
    cb_type cb;
};

using prefix_chain_tree_ptr = unique_ptr<prefix_chain_tree>;
using grandpa_net_msg_data = static_variant<chain_conf_msg, block_get_conf_msg, handshake_msg, handshake_ans_msg>;

struct grandpa_net_msg {
    uint32_t ses_id;
    grandpa_net_msg_data data;
};

struct on_accepted_block_event {
    block_id_type block_id;
    block_id_type prev_block_id;
    public_key_type creator_key;
    std::set<public_key_type> active_bp_keys;
};

struct on_irreversible_event {
    block_id_type block_id;
};

struct on_new_peer_event {
    uint32_t ses_id;
};

using grandpa_event_data = static_variant<on_accepted_block_event, on_irreversible_event, on_new_peer_event>;
struct grandpa_event {
    grandpa_event_data data;
};

using grandpa_message = static_variant<grandpa_net_msg, grandpa_event>;
using grandpa_message_ptr = shared_ptr<grandpa_message>;


struct peer_info {
    public_key_type public_key;
    block_id_type lib_id;
    block_id_type last_known_block_id;
};


using net_channel = channel<const grandpa_net_msg&>;
using net_channel_ptr = std::shared_ptr<net_channel>;

using event_channel = channel<const grandpa_event&>;
using event_channel_ptr = std::shared_ptr<event_channel>;

using prev_block_prodiver = provider<fc::optional<block_id_type>, block_id_type>; // return prev block id or null if block absent
using prev_block_prodiver_ptr = std::shared_ptr<prev_block_prodiver>;

using finality_channel = channel<const block_id_type&>;
using finality_channel_ptr = std::shared_ptr<finality_channel>;

using lib_prodiver = provider<block_id_type>; // return current lib block id
using lib_prodiver_ptr = std::shared_ptr<lib_prodiver>;

using prods_provider = provider<vector<public_key_type>>; // return current bp public keys vector
using prods_provider_ptr = std::shared_ptr<prods_provider>;


class grandpa {
public:
    grandpa() {}

    grandpa& set_in_net_channel(const net_channel_ptr& ptr) {
        _in_net_channel = ptr;
        return *this;
    }

    grandpa& set_out_net_channel(const net_channel_ptr& ptr) {
        _out_net_channel = ptr;
        return *this;
    }

    grandpa& set_event_channel(const event_channel_ptr& ptr) {
        _in_event_channel = ptr;
        return *this;
    }

    grandpa& set_finality_channel(const finality_channel_ptr& ptr) {
        _finality_channel = ptr;
        return *this;
    }

    grandpa& set_prev_block_provider(const prev_block_prodiver_ptr& ptr) {
        _prev_block_provider = ptr;
        return *this;
    }

    grandpa& set_lib_provider(const lib_prodiver_ptr& ptr) {
        _lib_provider = ptr;
        return *this;
    }

    grandpa& set_prods_provider(const prods_provider_ptr& ptr) {
        _prods_provider = ptr;
        return *this;
    }

    grandpa& set_private_key(const private_key_type& key) {
        _private_key = key;
        return *this;
    }

    void start(chain_type_ptr chain) {
        FC_ASSERT(_in_net_channel && _in_event_channel, "in channels should be inited");
        FC_ASSERT(_out_net_channel, "out channels should be inited");
        FC_ASSERT(_finality_channel, "finality channel should be inited");
        FC_ASSERT(_prev_block_provider, "prev block provider should be inited");
        FC_ASSERT(_lib_provider, "LIB provider should be inited");
        FC_ASSERT(_prods_provider, "producer provider should be inited");

        auto lib_id = get_lib();
        _prefix_tree_ptr.reset(
            new prefix_chain_tree(std::make_shared<prefix_node>(prefix_node { lib_id }))
        );
        update_lib(lib_id);
        _prefix_tree_ptr->insert(chain, _private_key.get_public_key());

#ifndef SYNC_GRANDPA
        _thread_ptr.reset(new std::thread([this]() {
            wlog("Grandpa thread started");
            loop();
            wlog("Grandpa thread terminated");
        }));
#endif

        subscribe();
    }

    void stop() {
#ifndef SYNC_GRANDPA
        _done = true;
        _message_queue.terminate();
        _thread_ptr->join();
#endif
    }

private:
    std::unique_ptr<std::thread> _thread_ptr;
    std::atomic<bool> _done { false };
    private_key_type _private_key;
    prefix_chain_tree_ptr _prefix_tree_ptr;
    std::map<uint32_t, peer_info> _peers;

#ifndef SYNC_GRANDPA
    message_queue<grandpa_message> _message_queue;
#endif

    net_channel_ptr _in_net_channel;
    net_channel_ptr _out_net_channel;
    event_channel_ptr _in_event_channel;
    finality_channel_ptr _finality_channel;
    prev_block_prodiver_ptr _prev_block_provider;
    lib_prodiver_ptr _lib_provider;
    prods_provider_ptr _prods_provider;

    void subscribe() {
        _in_net_channel->subscribe([&](const grandpa_net_msg& msg) {
            dlog("Grandpa received net message, type: ${type}", ("type", msg.data.which()));
#ifdef SYNC_GRANDPA
            process_msg(std::make_shared<grandpa_message>(msg));
#else
            _message_queue.push_message(msg);
#endif
        });

        _in_event_channel->subscribe([&](const grandpa_event& event) {
            dlog("Grandpa received event, type: ${type}", ("type", event.data.which()));
#ifdef SYNC_GRANDPA
            process_msg(std::make_shared<grandpa_message>(event));
#else
            _message_queue.push_message(event);
#endif
        });
    }

    template <typename T>
    void send(uint32_t ses_id, const T & msg) {
        auto net_msg = grandpa_net_msg { ses_id, msg };
        dlog("Grandpa net message sended, type: ${type}, ses_id: ${ses_id}",
            ("type", net_msg.data.which())
            ("ses_id", ses_id)
        );
        _out_net_channel->send(net_msg);
    }

    template <typename T>
    void bcast(const T & msg) {
        dlog("Grandpa net message bcasted, type: ${type}", ("type", grandpa_net_msg_data::tag<T>::value));
        for (const auto& peer: _peers) {
            _out_net_channel->send(grandpa_net_msg { peer.first, msg });
        }
    }

#ifndef SYNC_GRANDPA
    void loop() {
        while (true) {
            auto msg = _message_queue.get_next_msg_wait();

            if (_done) {
                break;
            }

            dlog("Grandpa message processing started, type: ${type}", ("type", msg->which()));

            process_msg(msg);
        }
    }
#endif

    auto get_lib() -> block_id_type {
        return _lib_provider->get();
    }

    auto get_prev_block_id(const block_id_type& id) {
        return _prev_block_provider->get(id);
    }

    auto get_prod_list() {
        return _prods_provider->get();
    }

    // need handle all messages
    void process_msg(grandpa_message_ptr msg_ptr) {
        auto msg = *msg_ptr;
        switch (msg.which()) {
            case grandpa_message::tag<grandpa_net_msg>::value:
                process_net_msg(msg.get<grandpa_net_msg>());
                break;
            case grandpa_message::tag<grandpa_event>::value:
                process_event(msg.get<grandpa_event>());
                break;
            default:
                elog("Grandpa received unknown message, type: ${type}", ("type", msg.which()));
                break;
        }
    }

    void process_net_msg(const grandpa_net_msg& msg) {
        auto ses_id = msg.ses_id;
        const auto& data = msg.data;

        switch (data.which()) {
            case grandpa_net_msg_data::tag<chain_conf_msg>::value:
                on(ses_id, data.get<chain_conf_msg>());
                break;
            case grandpa_net_msg_data::tag<block_get_conf_msg>::value:
                on(ses_id, data.get<block_get_conf_msg>());
                break;
            case grandpa_net_msg_data::tag<handshake_msg>::value:
                on(ses_id, data.get<handshake_msg>());
                break;
            case grandpa_net_msg_data::tag<handshake_ans_msg>::value:
                on(ses_id, data.get<handshake_ans_msg>());
                break;
            default:
                ilog("Grandpa message received, but handler not found, type: ${type}",
                    ("type", data.which())
                );
                break;
        }
    }

    void process_event(const grandpa_event& event) {
        const auto& data = event.data;
        switch (data.which()) {
            case grandpa_event_data::tag<on_accepted_block_event>::value:
                on(data.get<on_accepted_block_event>());
                break;
            case grandpa_event_data::tag<on_irreversible_event>::value:
                on(data.get<on_irreversible_event>());
                break;
            case grandpa_event_data::tag<on_new_peer_event>::value:
                on(data.get<on_new_peer_event>());
                break;
            default:
                ilog("Grandpa event received, but handler not found, type: ${type}",
                    ("type", data.which())
                );
                break;
        }
    }

    void on(uint32_t ses_id, const chain_conf_msg& msg) {
        dlog("Grandpa chain_conf_msg received, msg: ${msg}, ses_id: ${ses_id}", ("msg", msg)("ses_id", ses_id));

        auto peer_itr = _peers.find(ses_id);

        if (peer_itr == _peers.end()) {
            wlog("Grandpa handled chain_conf_msg, but peer is unknown, ses_id: ${ses_id}",
                ("ses_id", ses_id)
            );
            return;
        }

        if (!msg.validate(peer_itr->second.public_key)) {
            elog("Grandpa confirmation validation fail, ses_id: ${ses_id}",
                ("ses_id", ses_id)
            );
            return;
        }

        if (!is_producer(peer_itr->second.public_key)) {
            elog("Grandpa confirmation from non producer, ses_id: ${ses_id}",
                ("ses_id", ses_id)
            );
            return;
        }

        try {
            auto chain_ptr = std::make_shared<chain_type>(chain_type { msg.data.base_block, msg.data.blocks, msg.signature });
            auto max_conf_ptr = _prefix_tree_ptr->insert(chain_ptr, peer_itr->second.public_key);

            try_finalize(max_conf_ptr);
        }
        catch (const std::exception& e) {
            elog("Grandpa chain insert error, e: ${e}", ("e", e.what()));

            // dlog("Granpda requesting confirmations for block, id: ${id}", ("id", msg.data.base_block));
            // send(ses_id, make_network_msg(block_get_conf_type { msg.data.base_block }, _private_key));
            return;
        }
    }

    void on(uint32_t ses_id, const block_get_conf_msg& msg) {
        dlog("Grandpa block_get_conf received, msg: ${msg}", ("msg", msg));

        auto node = _prefix_tree_ptr->find(msg.data.block_id);

        if (!node) {
            elog("Grandpa cannot find block, id: ${id}", ("id", msg.data.block_id));
            return;
        }

        for ( auto & conf: node->confirmation_data ) {
            send(ses_id, chain_conf_msg { { conf.second->base_block, conf.second->blocks }, conf.second->signature });
        }
    }

    void on(uint32_t ses_id, const handshake_msg& msg) {
        wlog("Grandpa handshake_msg received, msg: ${msg}", ("msg", msg));
        try {
            _peers[ses_id] = peer_info {msg.public_key(), msg.data.lib, msg.data.lib};

            send(ses_id, handshake_ans_msg(handshake_ans_type { get_lib() }, _private_key));
        } catch (const fc::exception& e) {
            elog("Grandpa handshake_msg handler error, e: ${e}", ("e", e.what()));
        }
    }

    void on(uint32_t ses_id, const handshake_ans_msg& msg) {
        wlog("Grandpa handshake_ans_msg received, msg: ${msg}", ("msg", msg));
        try {
            _peers[ses_id] = peer_info {msg.public_key(), msg.data.lib, msg.data.lib};
        } catch (const fc::exception& e) {
            elog("Grandpa handshake_ans_msg handler error, e: ${e}", ("e", e.what()));
        }
    }

    void on(const on_accepted_block_event& event) {
        dlog("Grandpa on_accepted_block_event event handled, block_id: ${id}, num: ${num}",
            ("id", event.block_id)
            ("pid", event.prev_block_id)
            ("num", num(event.block_id))
        );

        auto prev_block_id = event.prev_block_id;
        auto base_block_ptr = _prefix_tree_ptr->find(prev_block_id);

        if (!base_block_ptr) {
            elog("Grandpa unlinkable block accepted, id: ${id}", ("id", prev_block_id));
            return;
        }

        auto chain_ptr = std::make_shared<chain_type>(chain_type{prev_block_id, {event.block_id}});
        auto conf_msg = chain_conf_msg(make_confirmation(*chain_ptr), _private_key);
        chain_ptr->signature = conf_msg.signature;

        auto max_conf_node = _prefix_tree_ptr->insert(chain_ptr, _private_key.get_public_key());
        try_finalize(max_conf_node);

        send_confirmations(chain_ptr);
    }

    void on(const on_irreversible_event& event) {
        dlog("Grandpa on_irreversible_event event handled, block_id: ${bid}, num: ${num}",
            ("bid", event.block_id)
            ("num", num(event.block_id))
        );

        update_lib(event.block_id);
    }

    void on(const on_new_peer_event& event) {
        elog("Grandpa on_new_peer_event event handled, ses_id: ${ses_id}", ("ses_id", event.ses_id));
        auto msg = handshake_msg(handshake_type{get_lib()}, _private_key);
        dlog("Sending handshake msg");
        send(event.ses_id, msg);
    }

    void update_lib(const block_id_type& lib_id) {
        auto node_ptr = _prefix_tree_ptr->find(lib_id);
        auto pub_key = _private_key.get_public_key();

        if (node_ptr) {
            _prefix_tree_ptr->set_root(node_ptr);
        }
        else {
            auto new_irb = std::make_shared<prefix_node>(prefix_node { lib_id });
            _prefix_tree_ptr->set_root(new_irb);
        }

        if (!_prefix_tree_ptr->get_root()->has_confirmation(pub_key)) {
            auto chain = std::make_shared<chain_type>(chain_type{ lib_id });

            auto conf_msg = chain_conf_msg(make_confirmation(*chain), _private_key);
            chain->signature = conf_msg.signature;

            _prefix_tree_ptr->get_root()->confirmation_data[pub_key] = chain;

            send_confirmations(chain);
        }
    }

    void send_confirmations(const chain_type_ptr& chain) {
        for (auto& peer: _peers) {
            auto ext_chain = extend_chain(chain, peer.second.last_known_block_id);

            if (ext_chain) {
                auto conf = chain_conf_msg(make_confirmation(*ext_chain), _private_key);
                send(peer.first, conf);
                peer.second.last_known_block_id = get_last_block_id(*ext_chain);
            }
        }
    }

    template <typename T>
    block_id_type get_last_block_id(const T& chain) {
        if (chain.blocks.size())
            return chain.blocks.back();
        else
            return chain.base_block;
    }

    chain_type_ptr extend_chain(const chain_type_ptr& chain, const block_id_type& from) {
         auto node_ptr = _prefix_tree_ptr->find(chain->base_block);

        if (!node_ptr) {
            elog("Grandpa cannot find base block in local tree, id: ${id}", ("id", chain->base_block));
            elog("Grandpa lib, id: ${id}, num: ${num}",
                ("id", _prefix_tree_ptr->get_root()->block_id)
                ("num", num(_prefix_tree_ptr->get_root()->block_id))
            );
            return nullptr;
        }

        vector<block_id_type> blocks;

        while (node_ptr && num(node_ptr->block_id) >= num(from)) {
            blocks.push_back(node_ptr->block_id);
            node_ptr = node_ptr->parent;
        }

        if (blocks.size() && blocks.back() != from) {
            dlog("Granpda cannot link chain with target block, id: ${id}", ("id", from));
        }

        auto ext_chain = std::make_shared<chain_type>(*chain);

        if (blocks.size()) {
            ext_chain->base_block = blocks.back();
            blocks.pop_back();
            std::reverse(blocks.begin(), blocks.end());
            ext_chain->blocks.insert(ext_chain->blocks.end(), blocks.begin(), blocks.end());
        }

        return ext_chain;
    }

    confirmation_type make_confirmation(const chain_type& chain) {
        return confirmation_type { chain.base_block, chain.blocks };
    }

    uint32_t num(const block_id_type& id) {
        return fc::endian_reverse_u32(id._hash[0]);
    }

    // TODO thread safe
    bool is_producer(const public_key_type& pub_key) {
        // const auto & ctlr = app().get_plugin<chain_plugin>().chain();
        // const auto & prods = ctlr.active_producers().producers;
        // auto prod_itr = std::find_if(prods.begin(), prods.end(), [&](const auto& prod) {
        //     return prod.block_signing_key == pub_key;
        // });
        // return prod_itr != prods.end();
        return true;
    }

    // TODO thread safe
    uint32_t bft_threshold() {
        // const auto & ctlr = app().get_plugin<chain_plugin>().chain();
        // return ctlr.active_producers().producers.size() * 2 / 3 ;
        return 2;
    }

    void try_finalize(const prefix_node_ptr& node_ptr) {
        auto id = node_ptr->block_id;

        ilog("Grandpa max conf block, id: ${id}, num: ${num}, confs: ${confs}",
            ("id", id)
            ("num", num(id))
            ("confs", node_ptr->confirmation_data.size())
        );

        if (num(id) <= num(_prefix_tree_ptr->get_root()->block_id)) {
            return;
        }

        if (node_ptr->confirmation_data.size() >= bft_threshold()) {
            _finality_channel->send(id);
            wlog("Grandpa finalized block, id: ${id}, num: ${num}",
                ("id", id)
                ("num", num(id))
            );
            update_lib(id);
        }
    }
};

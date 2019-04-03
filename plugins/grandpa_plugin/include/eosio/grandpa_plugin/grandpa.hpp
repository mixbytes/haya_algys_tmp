#pragma once
#include "network_messages.hpp"
#include "round.hpp"
#include <fc/exception/exception.hpp>
#include <fc/io/json.hpp>
#include <fc/bitutil.hpp>
#include <queue>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>


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

using grandpa_net_msg_data = static_variant<handshake_msg, handshake_ans_msg, prevote_msg, precommit_msg>;

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
    static constexpr uint32_t round_width = 2;
    static constexpr uint32_t prevote_width = 1;

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

    void start(prefix_tree_ptr tree) {
        FC_ASSERT(_in_net_channel && _in_event_channel, "in channels should be inited");
        FC_ASSERT(_out_net_channel, "out channels should be inited");
        FC_ASSERT(_finality_channel, "finality channel should be inited");
        FC_ASSERT(_prev_block_provider, "prev block provider should be inited");
        FC_ASSERT(_lib_provider, "LIB provider should be inited");
        FC_ASSERT(_prods_provider, "producer provider should be inited");

        _prefix_tree = tree;

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
    prefix_tree_ptr _prefix_tree;
    grandpa_round_ptr _round;
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
            case grandpa_net_msg_data::tag<prevote_msg>::value:
                _round->on(data.get<prevote_msg>());
                break;
            case grandpa_net_msg_data::tag<precommit_msg>::value:
                _round->on(data.get<precommit_msg>());
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

    void process_event(const grandpa_event& event){
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
            ("num", num(event.block_id))
        );

        //TODO insert block to tree

        if (should_start_round(event.block_id)) {
            finish_round();
            new_round(round_num(event.block_id), /* TODO pass primary */ _private_key.get_public_key());
        }

        if (should_end_prevote(event.block_id)) {
            _round->end_prevote();
        }
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

    uint32_t round_num(const block_id_type& block_id) const {
        return (num(block_id) - 1) / round_width;
    }

    uint32_t num_in_round(const block_id_type& block_id) const {
        return (num(block_id) - 1) % round_width;
    }

    bool should_start_round(const block_id_type& block_id) const {
        if (!_round) {
            return true;
        }

        return round_num(block_id) > _round->get_num();
    }

    bool should_end_prevote(const block_id_type& block_id) const {
        if (!_round) {
            return false;
        }

        return round_num(block_id) == _round->get_num()
            && num_in_round(block_id) == prevote_width;
    }

    void finish_round() {
        if (!_round) {
            return;
        }

        dlog("Grandpa finishing round, num: ${n}", ("n", _round->get_num()));
        _round->finish();

        //TODO get proof and finalize
        //TODO remove proofs from tree
    }

    void new_round(uint32_t round_num, const public_key_type& primary) {
        dlog("Grandpa staring round, num: ${n}", ("n", round_num));
        _round.reset(new grandpa_round(round_num, primary, _prefix_tree));
    }

    void update_lib(const block_id_type& lib_id) {
        auto node_ptr = _prefix_tree->find(lib_id);
        auto pub_key = _private_key.get_public_key();

        if (node_ptr) {
            _prefix_tree->set_root(node_ptr);
        }
        else {
            auto new_irb = std::make_shared<tree_node>(tree_node { lib_id });
            _prefix_tree->set_root(new_irb);
        }
    }

    template <typename T>
    block_id_type get_last_block_id(const T& chain) {
        if (chain.blocks.size())
            return chain.blocks.back();
        else
            return chain.base_block;
    }

    uint32_t num(const block_id_type& id) const {
        return fc::endian_reverse_u32(id._hash[0]);
    }

    // TODO thread safe
    uint32_t bft_threshold() {
        // const auto & ctlr = app().get_plugin<chain_plugin>().chain();
        // return ctlr.active_producers().producers.size() * 2 / 3 ;
        return 2;
    }

    void try_finalize(const tree_node_ptr& node_ptr) {
        auto id = node_ptr->block_id;

        ilog("Grandpa max conf block, id: ${id}, num: ${num}, confs: ${confs}",
            ("id", id)
            ("num", num(id))
            ("confs", node_ptr->confirmation_data.size())
        );

        if (num(id) <= num(_prefix_tree->get_root()->block_id)) {
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

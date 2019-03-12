#include <eosio/grandpa_plugin/grandpa_plugin.hpp>
#include <eosio/grandpa_plugin/network_messages.hpp>
#include <eosio/grandpa_plugin/prefix_chain_tree.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <fc/io/json.hpp>
#include <queue>
#include <chrono>
#include <atomic>

namespace eosio {

using namespace std::chrono_literals;
using namespace eosio::chain;
using namespace eosio::chain::plugin_interface;

static appbase::abstract_plugin& _grandpa_plugin = app().register_plugin<grandpa_plugin>();


static constexpr uint32_t net_message_types_base = 100;

using ::fc::static_variant;
using std::shared_ptr;
using std::unique_ptr;
using std::pair;
using chain::private_key_type;
using chain::public_key_type;

using prefix_chain_tree_ptr = unique_ptr<prefix_chain_tree>;

using grandpa_net_msg_data = static_variant<chain_conf_msg, block_get_conf_msg, handshake_msg>;
struct grandpa_net_msg {
    uint32_t ses_id;
    grandpa_net_msg_data data;
};

struct on_accepted_block_event {
    block_state_ptr block_ptr;
};

struct on_irreversible_event {
    block_state_ptr block_ptr;
};

using grandpa_event_data = static_variant<on_accepted_block_event, on_irreversible_event>;
struct grandpa_event {
    grandpa_event_data data;
};

using grandpa_message = static_variant<grandpa_net_msg, grandpa_event>;
using grandpa_message_ptr = shared_ptr<grandpa_message>;

struct peer_info {
    public_key_type public_key;
    uint64_t lib_num;
    block_id_type lib_id;
};

class grandpa_plugin_impl {
public:
    using mutex_guard = std::lock_guard<std::mutex>;

public:
    grandpa_plugin_impl() {}

    std::queue<grandpa_message_ptr> _message_queue {};
    std::mutex _message_queue_mutex;
    bool _need_notify = true;
    std::condition_variable _new_msg_cond;
    std::unique_ptr<std::thread> _thread_ptr;
    std::atomic<bool> _done { false };
    private_key_type _private_key;
    prefix_chain_tree_ptr _prefix_tree_ptr;
    std::map<uint32_t, peer_info> _peers;

    channels::irreversible_block::channel_type::handle _on_irb_handle;
    channels::accepted_block::channel_type::handle _on_accepted_block_handle;

    template <typename T>
    void push_message(const T& msg) {
        mutex_guard lock(_message_queue_mutex);

        auto grandpa_msg = std::make_shared<grandpa_message>(msg);
        _message_queue.push(grandpa_msg);

        if (_need_notify) {
            _new_msg_cond.notify_one();
        }
    }

    template <typename T>
    static constexpr uint32_t get_net_msg_type() {
        return net_message_types_base + grandpa_net_msg_data::tag<T>::value;
    }

    template <typename T>
    void subscribe() {
        app().get_plugin<bnet_plugin>().subscribe<T>(get_net_msg_type<T>(),
        [this](uint32_t ses_id, const T & msg) {
            push_message(grandpa_net_msg { ses_id, msg });

            dlog("Grandpa network message received, ses_id: ${ses_id}, type: ${type}, msg: ${msg}",
                ("ses_id", ses_id)
                ("type", get_net_msg_type<T>())
                ("msg", fc::json::to_string(fc::variant(msg)))
            );
        });
    }

    //need subscribe for all grandpa network message types
    void subscribe() {
        subscribe<chain_conf_msg>();
        subscribe<block_get_conf_msg>();
        subscribe<handshake_msg>();
    }

    template <typename T>
    void post_event(const T& event) {
        auto ev = grandpa_event { event };
        push_message(ev);

        dlog("Grandpa event posted, type: ${type}", ("type", ev.data.which()));
    }

    template <typename T>
    void send(uint32_t ses_id, const T & msg) {
        app().get_plugin<bnet_plugin>()
            .send(ses_id, get_net_msg_type<T>(), grandpa_message {msg} );
    }

    template <typename T>
    void bcast(const T & msg) {
        app().get_plugin<bnet_plugin>()
            .bcast(get_net_msg_type<T>(), grandpa_message {msg} );
    }

    grandpa_message_ptr get_next_msg() {
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

    grandpa_message_ptr get_next_msg_wait() {
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

    void loop() {
        while (true) {
            auto msg = get_next_msg_wait();

            if (_done) {
                break;
            }

            dlog("Granpa message processing started, type: ${type}", ("type", msg->which()));

            process_msg(msg);
        }
    }

    void do_bft_finalize(const block_id_type& block_id) {
        app().get_io_service().post([this, bid = block_id]() {
            app().get_plugin<chain_plugin>().chain().bft_finalize(bid);
        });
    }

    void stop() {
        _done = true;
        _new_msg_cond.notify_one();
        _thread_ptr->join();
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
            default:
                ilog("Grandpa message received, but handler not found, type: ${type}",
                    ("type", net_message_types_base + data.which())
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
            default:
                ilog("Grandpa event received, but handler not found, type: ${type}",
                    ("type", data.which())
                );
                break;
        }
    }

    void on(uint32_t ses_id, const chain_conf_msg& msg) {
        dlog("Grandpa chain_conf_msg received, msg: ${msg}", ("msg", msg));

        auto peer_itr = _peers.find(ses_id);

        if (peer_itr == _peers.end()) {
            wlog("Grandpa handled chain_conf_msg, but peer is unknown, ses_id: ${ses_id}",
                ("ses_id", ses_id)
            );
            return;
        }

        if (!validate_confirmation(msg, peer_itr->second.public_key)) {
            elog("Grandpa confirmation validation fail, ses_id: ${ses_id}",
                ("ses_id", ses_id)
            );
            return;
        }

        try {
            auto conf_ptr = std::make_shared<chain_conf_msg>(msg);
            auto chain_ptr = std::static_pointer_cast<chain_type>(conf_ptr);
            _prefix_tree_ptr->insert(chain_ptr, peer_itr->second.public_key);
        }
        catch (const fc::exception& e) {
            elog("Grandpa chain insert error, e: ${e}", ("e", e.what()));
            return;
        }

        auto final_chain = _prefix_tree_ptr->get_final(2);
        dlog("Grandpa final chain length: ${length}", ("length", final_chain.size()));
    }

    void on(uint32_t ses_id, const block_get_conf_msg& msg) {
        dlog("Grandpa block_get_conf received, msg: ${msg}", ("msg", msg));
    }

    void on(uint32_t ses_id, const handshake_msg& msg) {
        dlog("Grandpa handshake_msg received, msg: ${msg}", ("msg", msg));
    }

    void on(const on_accepted_block_event& event) {
        dlog("Grandpa on_accepted_block_event event handled, block_id: ${bid}", ("bid", event.block_ptr->id));
    }

    void on(const on_irreversible_event& event) {
        dlog("Grandpa on_irreversible_event event handled, block_id: ${bid}", ("bid", event.block_ptr->id));
    }
};


grandpa_plugin::grandpa_plugin():my(new grandpa_plugin_impl()){}
grandpa_plugin::~grandpa_plugin(){}

void grandpa_plugin::set_program_options(options_description& /*cli*/, options_description& cfg) {
    cfg.add_options()
        ("private-key", boost::program_options::value<string>(), "Private key for Grandpa finalizer")
    ;
}

void grandpa_plugin::plugin_initialize(const variables_map& options) {
    if( options.count("private-key") ) {
        auto wif_key = options["private-key"].as<std::string>();

        try {
            my->_private_key = private_key_type(wif_key);
        }
        catch ( fc::exception& e ) {
            elog("Malformed private key: ${key}", ("key", wif_key));
        }
    }
}

void grandpa_plugin::plugin_startup() {
    my->subscribe();

    auto lib_id = app().get_plugin<chain_plugin>().chain().last_irreversible_block_id();

    my->_prefix_tree_ptr.reset(
        new prefix_chain_tree(std::make_shared<prefix_node>(prefix_node { lib_id }))
    );

    my->_thread_ptr.reset(new std::thread([this]() {
        wlog("Grandpa thread started");
        my->loop();
        wlog("Grandpa thread terminated");
    }));

    my->_on_accepted_block_handle = app().get_channel<channels::accepted_block>()
    .subscribe( [this]( block_state_ptr s ) {
        my->post_event(on_accepted_block_event {s});
    });

    my->_on_irb_handle = app().get_channel<channels::irreversible_block>()
    .subscribe( [this]( block_state_ptr s ) {
        my->post_event(on_irreversible_event {s});
    });
}

void grandpa_plugin::plugin_shutdown() {
    my->stop();
}

}

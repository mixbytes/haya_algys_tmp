#include <eosio/grandpa_plugin/grandpa_plugin.hpp>
#include <eosio/grandpa_plugin/network_messages.hpp>
#include <eosio/grandpa_plugin/prefix_chain_tree.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <fc/io/json.hpp>
#include <queue>
#include <chrono>

namespace eosio {

using namespace std::chrono_literals;
using namespace eosio::chain;

static appbase::abstract_plugin& _grandpa_plugin = app().register_plugin<grandpa_plugin>();


static constexpr uint32_t message_types_base = 100;

using ::fc::static_variant;
using std::shared_ptr;
using std::unique_ptr;
using std::pair;
using chain::private_key_type;
using chain::public_key_type;

using grandpa_message = static_variant<chain_conf_msg, block_get_conf_msg, handshake_msg>;
using grandpa_message_ptr = shared_ptr<grandpa_message>;
using grandpa_message_pair = pair<uint32_t, grandpa_message_ptr>;
using prefix_chain_tree_ptr = unique_ptr<prefix_chain_tree>;

struct peer_info {
    public_key_type public_key;
    block_id_type lib_id;
};

class grandpa_plugin_impl {
public:
    using mutex_guard = std::lock_guard<std::mutex>;

public:
    grandpa_plugin_impl() {}

    std::queue<grandpa_message_pair> _message_queue {};
    std::mutex _message_queue_mutex;
    bool _need_notify = true;
    std::condition_variable _new_msg_cond;
    std::unique_ptr<std::thread> _thread_ptr;
    bool _done = false;
    private_key_type _private_key;
    prefix_chain_tree_ptr _prefix_tree_ptr;
    std::map<uint32_t, peer_info> _peers;

    template <typename T>
    static constexpr uint32_t get_msg_type() {
        return message_types_base + grandpa_message::tag<T>::value;
    }

    template <typename T>
    static constexpr uint32_t get_msg_type(const T& /* msg */) {
        return message_types_base + grandpa_message::tag<T>::value;
    }

    template <typename T>
    void subscribe() {
        app().get_plugin<bnet_plugin>().subscribe<T>(get_msg_type<T>(),
        [this](uint32_t ses_id, const T & msg) {
            mutex_guard lock(_message_queue_mutex);

            _message_queue.push(std::make_pair(ses_id, std::make_shared<grandpa_message>(msg)));

            if (_need_notify) {
                _new_msg_cond.notify_one();
            }

            dlog("Grandpa message received, ses_id: ${ses_id}, type: ${type}, msg: ${msg}",
                ("ses_id", ses_id)
                ("type", get_msg_type<T>())
                ("msg", fc::json::to_string(fc::variant(msg)))
            );
        });
    }

    //need subscribe for all grandpa message types
    void subscribe() {
        subscribe<chain_conf_msg>();
        subscribe<block_get_conf_msg>();
        subscribe<handshake_msg>();
    }

    template <typename T>
    void send(uint32_t ses_id, const T & msg) {
        app().get_plugin<bnet_plugin>()
            .send(ses_id, get_msg_type<T>(), grandpa_message {msg} );
    }

    template <typename T>
    void bcast(const T & msg) {
        app().get_plugin<bnet_plugin>()
            .bcast(get_msg_type<T>(), grandpa_message {msg} );
    }

    optional<grandpa_message_pair> get_next_msg() {
        mutex_guard lock(_message_queue_mutex);

        if (!_message_queue.size()) {
            _need_notify = true;
            return {};
        } else {
            _need_notify = false;
        }

        auto msg = _message_queue.front();
        _message_queue.pop();

        return msg;
    }

    optional<grandpa_message_pair> get_next_msg_wait() {
        while (true) {
            if (_need_notify) {
                std::unique_lock<std::mutex> lk(_message_queue_mutex);

                _new_msg_cond.wait(lk, [this](){
                    return (bool)_message_queue.size() || _done;
                });
            }

            if (_done)
                return {};

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

            dlog("Grandpa message processing started, type: ${type}, session: ${ses_id}",
                ("type", message_types_base + msg->second->which())
                ("ses_id", msg->first)
            );

            process_msg(msg->first, msg->second);
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
    void process_msg(uint32_t ses_id, grandpa_message_ptr msg_ptr) {
        auto msg = *msg_ptr;
        switch (msg.which()) {
            case grandpa_message::tag<chain_conf_msg>::value:
                on(ses_id, msg.get<chain_conf_msg>());
                break;
            case grandpa_message::tag<block_get_conf_msg>::value:
                on(ses_id, msg.get<block_get_conf_msg>());
                break;
            case grandpa_message::tag<handshake_msg>::value:
                on(ses_id, msg.get<handshake_msg>());
                break;
            default:
                ilog("Grandpa message received, but handler not found, type: ${type}",
                    ("type", message_types_base + msg.which())
                );
                break;
        }
    }

    void on(uint32_t ses_id, const chain_conf_msg& msg) {
        dlog("Grandpa chain_conf_msg received, msg: ${msg}", ("msg", msg));
    }

    void on(uint32_t ses_id, const block_get_conf_msg& msg) {
        dlog("Grandpa block_get_conf received, msg: ${msg}", ("msg", msg));
    }

    void on(uint32_t ses_id, const handshake_msg& msg) {
        dlog("Grandpa handshake_msg received, msg: ${msg}", ("msg", msg));
    }
};


grandpa_plugin::grandpa_plugin():my(new grandpa_plugin_impl()){}
grandpa_plugin::~grandpa_plugin(){}

void grandpa_plugin::set_program_options(options_description& /*cli*/, options_description& cfg) {
    cfg.add_options()
        ("grandpa-private-key", boost::program_options::value<string>(), "Private key for Grandpa finalizer")
    ;
}

void grandpa_plugin::plugin_initialize(const variables_map& options) {
    const auto iterator = options.find("grandpa-private-key");
    assert(iterator != options.end());
    auto wif_key = iterator->second.as<std::string>();
    try {
        my->_private_key = private_key_type(wif_key);
    }
    catch ( fc::exception& e ) {
        elog("Malformed private key: ${key}", ("key", wif_key));
    }
}

void grandpa_plugin::plugin_startup() {
    my->subscribe();

    my->_thread_ptr.reset(new std::thread([this]() {
        wlog("Grandpa thread started");
        my->loop();
        wlog("Grandpa thread terminated");
    }));
}

void grandpa_plugin::plugin_shutdown() {
    my->stop();
}

}

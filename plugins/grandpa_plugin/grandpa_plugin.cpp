#include <eosio/grandpa_plugin/grandpa_plugin.hpp>
#include <eosio/grandpa_plugin/network_messages.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <fc/io/json.hpp>
#include <queue>
#include <chrono>

namespace eosio {

using namespace std::chrono_literals;

static appbase::abstract_plugin& _grandpa_plugin = app().register_plugin<grandpa_plugin>();


static constexpr uint32_t message_types_base = 100;

using ::fc::static_variant;
using std::shared_ptr;

using test_message = std::string;
using grandpa_message = static_variant<test_message, chain_conf_msg, block_get_conf_msg, handshake_msg>;
using grandpa_message_ptr = shared_ptr<grandpa_message>;

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
    bool _done = false;

    template <typename T>
    void subscribe() {
        auto msg_type = message_types_base + grandpa_message::tag<T>::value;

        app().get_plugin<bnet_plugin>().subscribe<T>(msg_type,
        [this, msg_type](uint32_t ses_id, const T & msg) {
            mutex_guard lock(_message_queue_mutex);

            _message_queue.push(std::make_shared<grandpa_message>(msg));

            if (_need_notify) {
                _new_msg_cond.notify_one();
            }

            dlog("Grandpa message received, ses_id: ${ses_id}, type: ${type}, msg: ${msg}",
                ("ses_id", ses_id)
                ("type", msg_type)
                ("msg", fc::json::to_string(fc::variant(msg)))
            );
        });
    }

    //need subscribe for all grandpa message types
    void subscribe() {
        subscribe<test_message>();
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

            dlog("Granpa message processing started, type: ${type}",
                ("type", message_types_base + msg->which())
            );

            process_msg(msg);
        }
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
            case grandpa_message::tag<test_message>::value:
                on(msg.get<test_message>());
                break;
            case grandpa_message::tag<chain_conf_msg>::value:
                on(msg.get<chain_conf_msg>());
                break;
            case grandpa_message::tag<block_get_conf_msg>::value:
                on(msg.get<block_get_conf_msg>());
                break;
            case grandpa_message::tag<handshake_msg>::value:
                on(msg.get<handshake_msg>());
                break;
            default:
                ilog("Grandpa message received, but handler not found, type: ${type}",
                    ("type", message_types_base + msg.which())
                );
                break;
        }
    }

    void on(const test_message & msg) {
        dlog("Grandpa test message received, msg: ${msg}", ("msg", msg));
    }

    void on(const chain_conf_msg& msg) {
        dlog("Grandpa chain_conf_msg received, msg: ${msg}", ("msg", msg));
    }

    void on(const block_get_conf_msg& msg) {
        dlog("Grandpa block_get_conf received, msg: ${msg}", ("msg", msg));
    }

    void on(const handshake_msg& msg) {
        dlog("Grandpa handshake_msg received, msg: ${msg}", ("msg", msg));
    }
};


grandpa_plugin::grandpa_plugin():my(new grandpa_plugin_impl()){}
grandpa_plugin::~grandpa_plugin(){}

void grandpa_plugin::set_program_options(options_description&, options_description& cfg) {
}

void grandpa_plugin::plugin_initialize(const variables_map& options) {
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

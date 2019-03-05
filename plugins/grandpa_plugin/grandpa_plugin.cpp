#include <eosio/grandpa_plugin/grandpa_plugin.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <fc/io/json.hpp>
#include <queue>


namespace eosio {

static appbase::abstract_plugin& _grandpa_plugin = app().register_plugin<grandpa_plugin>();


static constexpr uint32_t message_types_base = 100;

using test_message = std::string;

using grandpa_message = ::fc::static_variant<test_message>;
using grandpa_message_ptr = std::shared_ptr<grandpa_message>;


class grandpa_plugin_impl {
public:
    using mutex_guard = std::lock_guard<std::mutex>;

public:
    grandpa_plugin_impl() {}

    std::queue<grandpa_message_ptr> _message_queue {};
    std::mutex _message_queue_mutex;

    template <typename T>
    void subscribe() {
        auto msg_type = message_types_base + grandpa_message::tag<T>::value;

        app().get_plugin<bnet_plugin>().subscribe<T>(msg_type,
        [this, msg_type](uint32_t ses_id, const T & msg) {
            mutex_guard lock(_message_queue_mutex);

            _message_queue.push(std::make_shared<grandpa_message>(msg));

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

        if (!_message_queue.size())
            return nullptr;

        auto msg = _message_queue.front();
        _message_queue.pop();

        return msg;
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
}

void grandpa_plugin::plugin_shutdown() {
   // OK, that's enough magic
}

}

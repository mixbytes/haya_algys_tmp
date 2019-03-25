/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */
#include <eosio/telemetry_plugin/telemetry_plugin.hpp>
#include <fc/exception/exception.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/block_state.hpp>

namespace eosio {
    using namespace chain::plugin_interface;

    static appbase::abstract_plugin &_telemetry_plugin = app().register_plugin<telemetry_plugin>();

    class telemetry_plugin_impl {
    private:
        channels::accepted_block::channel_type::handle _on_accepted_block_handle;
        channels::irreversible_block::channel_type::handle _on_irreversible_block_handle;

    public:
        telemetry_plugin_impl() {
        }
    };

    telemetry_plugin::telemetry_plugin() : my(new telemetry_plugin_impl()) {}

    telemetry_plugin::~telemetry_plugin() {}

    void telemetry_plugin::set_program_options(options_description &, options_description &cfg) {
        cfg.add_options()
                ("option-name", bpo::value<string>()->default_value("default value"),
                 "Option Description");
    }

    void telemetry_plugin::plugin_initialize(const variables_map &options) {
        try {
            if (options.count("option-name")) {
                // Handle the option
            }
        }
        FC_LOG_AND_RETHROW()
    }

    void telemetry_plugin::plugin_startup() {

    }

    void telemetry_plugin::plugin_shutdown() {
        // OK, that's enough magic
    }

}

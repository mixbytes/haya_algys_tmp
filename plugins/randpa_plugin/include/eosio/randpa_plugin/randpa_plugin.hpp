#pragma once
#include <appbase/application.hpp>
#include <eosio/bnet_plugin/bnet_plugin.hpp>

namespace eosio {

using namespace appbase;

class randpa_plugin : public appbase::plugin<randpa_plugin> {
public:
    randpa_plugin();
    virtual ~randpa_plugin();

    APPBASE_PLUGIN_REQUIRES((bnet_plugin)(chain_plugin))
    virtual void set_program_options(options_description&, options_description& cfg) override;

    void plugin_initialize(const variables_map& options);
    void plugin_startup();
    void plugin_shutdown();
    size_t message_queue_size();

private:
    std::unique_ptr<class randpa_plugin_impl> my;
};

}

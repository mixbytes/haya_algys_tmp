#pragma once
#include <appbase/application.hpp>
#include <eosio/bnet_plugin/bnet_plugin.hpp>

namespace eosio {

using namespace appbase;

class grandpa_plugin : public appbase::plugin<grandpa_plugin> {
public:
   grandpa_plugin();
   virtual ~grandpa_plugin();

   APPBASE_PLUGIN_REQUIRES((bnet_plugin))
   virtual void set_program_options(options_description&, options_description& cfg) override;

   void plugin_initialize(const variables_map& options);
   void plugin_startup();
   void plugin_shutdown();

private:
   std::unique_ptr<class grandpa_plugin_impl> my;
};

}

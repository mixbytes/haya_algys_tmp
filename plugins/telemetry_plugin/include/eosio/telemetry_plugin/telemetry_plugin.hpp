/**
 *  @file
 *  @copyright defined in haya/LICENSE
 */
#pragma once
#include <appbase/application.hpp>

namespace eosio {

using namespace appbase;

/**
 *  This is a plugin, intended to create a prometheus server for getting telemetry (using prometheus PULL model)
 */
class telemetry_plugin : public appbase::plugin<telemetry_plugin> {
public:
   telemetry_plugin();
   virtual ~telemetry_plugin();

   APPBASE_PLUGIN_REQUIRES()
   virtual void set_program_options(options_description&, options_description& cfg) override;

   void plugin_initialize(const variables_map& options);
   void plugin_startup();
   void plugin_shutdown();

private:
   std::unique_ptr<class telemetry_plugin_impl> my;
};

}

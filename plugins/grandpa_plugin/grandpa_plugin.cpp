#include <eosio/grandpa_plugin/grandpa_plugin.hpp>

namespace eosio {
   static appbase::abstract_plugin& _grandpa_plugin = app().register_plugin<grandpa_plugin>();

class grandpa_plugin_impl {
   public:
};

grandpa_plugin::grandpa_plugin():my(new grandpa_plugin_impl()){}
grandpa_plugin::~grandpa_plugin(){}

void grandpa_plugin::set_program_options(options_description&, options_description& cfg) {
}

void grandpa_plugin::plugin_initialize(const variables_map& options) {
}

void grandpa_plugin::plugin_startup() {
   // Make the magic happen
}

void grandpa_plugin::plugin_shutdown() {
   // OK, that's enough magic
}

}

/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */
#include <eosio/telemetry_plugin/telemetry_plugin.hpp>
#include <fc/exception/exception.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/block_state.hpp>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <ctime>

#define LATENCY_HISTOGRAM_KEYPOINTS 1000000, 2000000, 180000000

namespace eosio {
    using namespace chain::plugin_interface;
    using namespace prometheus;

    static appbase::abstract_plugin &_telemetry_plugin = app().register_plugin<telemetry_plugin>();

    class telemetry_plugin_impl {

    private:
        channels::accepted_block::channel_type::handle _on_accepted_block_handle;
        channels::irreversible_block::channel_type::handle _on_irreversible_block_handle;

        shared_ptr<Registry> registry;
        Exposer *exposer = nullptr;

        Counter *accepted_trx_count = nullptr;
        Gauge *last_irreversible_latency = nullptr;
        Histogram *irreversible_latency_hist = nullptr;

    public:
        void initialize(const std::string &endpoint, const std::string &uri, const size_t threads) {
            exposer = new Exposer{endpoint, uri, threads};

            registry = std::make_shared<Registry>();

            accepted_trx_count = &BuildCounter()
                    .Name("accepted_trx_total")
                    .Help("Total amount of transactions accepted")
                    .Register(*registry)
                    .Add({});

            irreversible_latency_hist = &BuildHistogram()
                    .Name("irreversible_latency")
                    .Help("The latency of irreversible blocks")
                    .Register(*registry)
                    .Add({},
                         Histogram::BucketBoundaries{
                                 LATENCY_HISTOGRAM_KEYPOINTS
                         });

            last_irreversible_latency = &BuildGauge()
                    .Name("last_irreversible_latency")
                    .Help("The latency of the last irreversible blocks")
                    .Register(*registry)
                    .Add({});

            exposer->RegisterCollectable(registry);

            _on_accepted_block_handle = app().get_channel<channels::accepted_block>()
                    .subscribe([this](block_state_ptr s) {
                        accepted_trx_count->Increment(s.get()->trxs.size());
                    });

            _on_irreversible_block_handle = app().get_channel<channels::irreversible_block>()
                    .subscribe([this](block_state_ptr s) {
                        fc::microseconds latency = fc::time_point::now() - s.get()->header.timestamp.to_time_point();
                        last_irreversible_latency->Set(latency.count());
                        irreversible_latency_hist->Observe(latency.count());
                    });
        }

        virtual ~telemetry_plugin_impl() {
            delete exposer;
        }
    };

    telemetry_plugin::telemetry_plugin() : my(new telemetry_plugin_impl()) {}

    telemetry_plugin::~telemetry_plugin() = default;

    void telemetry_plugin::set_program_options(options_description &, options_description &cfg) {
        cfg.add_options()
                ("telemetry-endpoint", bpo::value<string>()->default_value("8080"),
                 "the endpoint upon which to listen for incoming connections to promethus server")
                ("telemetry-uri", bpo::value<string>()->default_value("/metrics"),
                 "the base uri of the endpoint")
                ("telemetry-threads", bpo::value<size_t>()->default_value(1),
                 "the number of threads to use to process network messages to promethus server");
    }

    void telemetry_plugin::plugin_initialize(const variables_map &options) {
        try {
            my->initialize(
                    options.at("telemetry-endpoint").as<string>(),
                    options.at("telemetry-uri").as<string>(),
                    options.at("telemetry-threads").as<size_t>()
            );
        }
        FC_LOG_AND_RETHROW();
    }

    void telemetry_plugin::plugin_startup() {

    }

    void telemetry_plugin::plugin_shutdown() {

    }

}

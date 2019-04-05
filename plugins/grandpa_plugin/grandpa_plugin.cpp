#include <eosio/grandpa_plugin/grandpa_plugin.hpp>
#include <eosio/grandpa_plugin/network_messages.hpp>
#include <eosio/grandpa_plugin/prefix_chain_tree.hpp>
#include <eosio/grandpa_plugin/grandpa.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <fc/io/json.hpp>
#include <queue>
#include <chrono>
#include <atomic>
#include <fc/exception/exception.hpp>

namespace eosio {

using namespace std::chrono_literals;
using namespace eosio::chain;
using namespace eosio::chain::plugin_interface;

static appbase::abstract_plugin& _grandpa_plugin = app().register_plugin<grandpa_plugin>();


static constexpr uint32_t net_message_types_base = 100;

class grandpa_plugin_impl {
public:
    grandpa_plugin_impl() {}

    grandpa _grandpa;

    channels::irreversible_block::channel_type::handle _on_irb_handle;
    channels::accepted_block::channel_type::handle _on_accepted_block_handle;
    bnet_plugin::new_peer::channel_type::handle _on_new_peer_handle;

    template <typename T>
    static constexpr uint32_t get_net_msg_type(const T& msg = {}) {
        return net_message_types_base + grandpa_net_msg_data::tag<T>::value;
    }

    void start() {
        auto in_net_ch = std::make_shared<net_channel>();
        auto out_net_ch = std::make_shared<net_channel>();
        auto ev_ch = std::make_shared<event_channel>();
        auto finality_ch = std::make_shared<finality_channel>();

        _grandpa
            .set_in_net_channel(in_net_ch)
            .set_out_net_channel(out_net_ch)
            .set_event_channel(ev_ch)
            .set_finality_channel(finality_ch);

        subscribe<handshake_msg>(in_net_ch);
        subscribe<handshake_ans_msg>(in_net_ch);
        subscribe<prevote_msg>(in_net_ch);
        subscribe<precommit_msg>(in_net_ch);

        _on_accepted_block_handle = app().get_channel<channels::accepted_block>()
        .subscribe( [ev_ch]( block_state_ptr s ) {
            std::set<public_key_type> producer_keys;
            for (const auto& elem :  s->active_schedule.producers) {
                producer_keys.insert(elem.block_signing_key);
            }

            ev_ch->send(grandpa_event { on_accepted_block_event {
                    s->id,
                    s->header.previous,
                    s->block_signing_key,
                    std::move(producer_keys),
            } });
        });

        _on_irb_handle = app().get_channel<channels::irreversible_block>()
        .subscribe( [ev_ch]( block_state_ptr s ) {
            ev_ch->send(grandpa_event { on_irreversible_event { s->id } });
        });

        _on_new_peer_handle = app().get_channel<bnet_plugin::new_peer>()
        .subscribe( [ev_ch]( uint32_t ses_id ) {
            ev_ch->send(grandpa_event { on_new_peer_event { ses_id } });
        });

        out_net_ch->subscribe([this](const grandpa_net_msg& msg) {
            auto data = msg.data;
            switch (data.which()){
                case grandpa_net_msg_data::tag<prevote_msg>::value:
                    send(msg.ses_id, data.get<prevote_msg>());
                    break;
                case grandpa_net_msg_data::tag<precommit_msg>::value:
                    send(msg.ses_id, data.get<precommit_msg>());
                    break;
                case grandpa_net_msg_data::tag<handshake_msg>::value:
                    send(msg.ses_id, data.get<handshake_msg>());
                    break;
                case grandpa_net_msg_data::tag<handshake_ans_msg>::value:
                    send(msg.ses_id, data.get<handshake_ans_msg>());
                    break;
                default:
                    ilog("Grandpa message sent, but handler not found, type: ${type}",
                        ("type", data.which())
                    );
                break;
            }
        });

        finality_ch->subscribe([this](const block_id_type& msg) {
            //TODO finalization
        });

        auto prev_block_pr = std::make_shared<prev_block_prodiver>([](const block_id_type& id) -> optional<block_id_type> {
            auto bs = app().get_plugin<chain_plugin>().chain().fetch_block_state_by_id(id);
            if (!bs)
                return {};
            else
                return bs->header.previous;
        });

        auto lib_pr = std::make_shared<lib_prodiver>([]() -> block_id_type {
            return app().get_plugin<chain_plugin>().chain().last_irreversible_block_id();
        });

        auto prods_pr = std::make_shared<prods_provider>([]() -> vector<public_key_type> {
            const auto & prods = app().get_plugin<chain_plugin>()
                                        .chain()
                                        .active_producers()
                                        .producers;
            std::vector<public_key_type> keys;
            for (const auto& prod: prods) {
                keys.push_back(prod.block_signing_key);
            }
            return keys;
        });

        _grandpa
            .set_prev_block_provider(prev_block_pr)
            .set_lib_provider(lib_pr)
            .set_prods_provider(prods_pr);

        _grandpa.start();
    }

    void stop() {
        _grandpa.stop();
    }

    template <typename T>
    void send(uint32_t ses_id, const T& msg) {
        app().get_plugin<bnet_plugin>()
            .send(ses_id, get_net_msg_type(msg), msg);
    }

    template <typename T>
    void subscribe(const net_channel_ptr& ch) {
        app().get_plugin<bnet_plugin>()
        .subscribe<T>(get_net_msg_type<T>(),
        [ch](uint32_t ses_id, const T & msg) {
            dlog("Grandpa network message received, ses_id: ${ses_id}, type: ${type}, msg: ${msg}",
                ("ses_id", ses_id)
                ("type", get_net_msg_type<T>())
                ("msg", fc::json::to_string(fc::variant(msg)))
            );
            ch->send(grandpa_net_msg { ses_id, msg });
        });
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
    FC_ASSERT(iterator != options.end(), "Argument --grandpa-private-key not provided");
    auto wif_key = iterator->second.as<std::string>();
    try {
        my->_grandpa.set_private_key(private_key_type(wif_key));
    }
    catch ( fc::exception& e ) {
        elog("Malformed private key: ${key}", ("key", wif_key));
    }
}

void grandpa_plugin::plugin_startup() {
    my->start();
}

void grandpa_plugin::plugin_shutdown() {
    my->stop();
}

}

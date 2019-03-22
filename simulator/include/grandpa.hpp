#pragma once
#include "simulator.hpp"
#define SYNC_GRANDPA //SYNC mode
#include "../../plugins/grandpa_plugin/include/eosio/grandpa_plugin/grandpa.hpp"
#include <mutex>



class GrandpaNode: public Node {
public:
    explicit GrandpaNode(int id, Network && net): Node(id, std::move(net)) {
        init_channels();
        init_providers();
        init_grandpa();

        grandpa.start();
    }

    ~GrandpaNode() {
        grandpa.stop();
    }

    void on_receive(uint32_t from, void* msg) override {
        auto data = *static_cast<grandpa_net_msg*>(msg);
        in_net_ch->send(data);
    }

    void on_new_peer_event(uint32_t id) override {
        ev_ch->send(grandpa_event { ::on_new_peer_event { id } });
    }

private:
    void init_channels() {
        in_net_ch = std::make_shared<net_channel>();
        out_net_ch = std::make_shared<net_channel>();
        ev_ch = std::make_shared<event_channel>();

        out_net_ch->subscribe([this](const grandpa_net_msg& msg) {
            send<grandpa_net_msg>(msg.ses_id, msg);
        });
    }

    void init_providers() {
        prev_block_prov = std::make_shared<prev_block_prodiver>([](const block_id_type& id) -> fc::optional<block_id_type> {
            return {};
        });

        lib_prov = std::make_shared<lib_prodiver>([]() -> block_id_type {
            return block_id_type();
        });

        prods_prov = std::make_shared<prods_provider>([]() -> vector<public_key_type> {
            return {};
        });
    }

    void init_grandpa() {
        grandpa
            .set_event_channel(ev_ch)
            .set_in_net_channel(in_net_ch)
            .set_out_net_channel(out_net_ch)
            .set_prev_block_provider(prev_block_prov)
            .set_lib_provider(lib_prov)
            .set_prods_provider(prods_prov)
            .set_private_key(private_key_type::generate());
    }

    net_channel_ptr in_net_ch;
    net_channel_ptr out_net_ch;
    event_channel_ptr ev_ch;

    prev_block_prodiver_ptr prev_block_prov;
    lib_prodiver_ptr lib_prov;
    prods_provider_ptr prods_prov;

    grandpa grandpa;
};


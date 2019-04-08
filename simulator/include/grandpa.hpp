#pragma once
#include "simulator.hpp"
#include "database.hpp"
#define SYNC_GRANDPA //SYNC mode
#include <eosio/grandpa_plugin/grandpa.hpp>
#include <mutex>



class GrandpaNode: public Node {
public:
    explicit GrandpaNode(int id, Network && net, fork_db && db_, public_key_type pub_key):
        Node(id, std::move(net), std::move(db_), std::move(pub_key))
    {
        init_channels();
        init_providers();
        init_grandpa();

        prefix_tree_ptr tree(new prefix_tree(std::make_shared<tree_node>(tree_node { db.last_irreversible_block_id() })));
        grandpa.start(tree);
    }

    ~GrandpaNode() {
        grandpa.stop();
    }

    void on_receive(uint32_t from, void* msg) override {
        cout << "[Node] #" << this->id << " on_receive " << endl;
        auto data = *static_cast<grandpa_net_msg*>(msg);
        data.ses_id = from;
        in_net_ch->send(data);
    }

    void on_new_peer_event(uint32_t id) override {
        cout << "[Node] #" << this->id << " on_new_peer_event " << endl;
        ev_ch->send(grandpa_event { ::on_new_peer_event { id } });
    }

    void on_accepted_block_event(block_id_type id) override {
        cout << "[Node] #" << this->id << " on_accepted_block_event " << endl;
        ev_ch->send(grandpa_event { ::on_accepted_block_event { id, db.fetch_prev_block_id(id),
                                                                creator_key, get_active_bp_keys()
                                                                } });
    }

private:
    void init_channels() {
        in_net_ch = std::make_shared<net_channel>();
        out_net_ch = std::make_shared<net_channel>();
        ev_ch = std::make_shared<event_channel>();
        finality_ch = std::make_shared<finality_channel>();

        out_net_ch->subscribe([this](const grandpa_net_msg& msg) {
            send<grandpa_net_msg>(msg.ses_id, msg);
        });

        finality_ch->subscribe([this](const block_id_type& id) {
            db.bft_finalize(id);
        });
    }

    void init_providers() {
        prev_block_prov = std::make_shared<prev_block_prodiver>([this](const block_id_type& id) -> fc::optional<block_id_type> {
            auto block = db.find(id);
            if (!block || !block->parent)
                return {};
            else
                return block->parent->block_id;
        });

        lib_prov = std::make_shared<lib_prodiver>([this]() -> block_id_type {
            return db.last_irreversible_block_id();
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
            .set_finality_channel(finality_ch)
            .set_prev_block_provider(prev_block_prov)
            .set_lib_provider(lib_prov)
            .set_prods_provider(prods_prov)
            .set_private_key(private_key_type::generate());
    }

    net_channel_ptr in_net_ch;
    net_channel_ptr out_net_ch;
    event_channel_ptr ev_ch;
    finality_channel_ptr finality_ch;

    prev_block_prodiver_ptr prev_block_prov;
    lib_prodiver_ptr lib_prov;
    prods_provider_ptr prods_prov;

    grandpa grandpa;
};


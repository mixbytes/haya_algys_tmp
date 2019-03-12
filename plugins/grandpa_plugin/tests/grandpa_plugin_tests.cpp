#include <eosio/grandpa_plugin/prefix_chain_tree.hpp>
#include <eosio/grandpa_plugin/network_messages.hpp>
#include <fc/crypto/sha256.hpp>
#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <vector>
#include <iostream>
#include <string>
#include <memory>

using namespace eosio;
using namespace chain;
using std::vector;
using namespace fc::crypto;

BOOST_AUTO_TEST_SUITE(prefix_chain_tree_tests)

BOOST_AUTO_TEST_CASE(prefix_chain_) try {
    auto pub_key_1 = private_key::generate().get_public_key();
    auto pub_key_2 = private_key::generate().get_public_key();

    auto lib_block_id = fc::sha256("beef");
    auto node = prefix_node{lib_block_id};

    prefix_chain_tree tree(std::make_shared<prefix_node>(node));
    auto chain = std::make_shared<chain_type>(chain_type{lib_block_id,
                                                         vector<block_id_type>{fc::sha256("abc"), fc::sha256("def")}});
    tree.insert(chain, pub_key_1);

    const prefix_node_ptr root = tree.get_root();
    BOOST_REQUIRE_EQUAL(lib_block_id, root->block_id);
    BOOST_REQUIRE_EQUAL(1, root->adjacent_nodes.size());

    const auto chain_first_node = root->adjacent_nodes[0];
    BOOST_TEST(tree.find(fc::sha256("abc")) == chain_first_node);

    std::vector<prefix_node_ptr> expected{chain_first_node, chain_first_node->adjacent_nodes[0]};
    BOOST_TEST(expected == tree.get_final(1));


    // add second chain
    chain = std::make_shared<chain_type>(chain_type{fc::sha256("abc"),
                                                    vector<block_id_type>({fc::sha256("bbc")})});
    tree.insert(chain, pub_key_2);

    BOOST_REQUIRE_EQUAL(2, chain_first_node->adjacent_nodes.size());

    expected = vector<prefix_node_ptr>{chain_first_node};
    BOOST_TEST(expected == tree.get_final(2));


} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()


BOOST_AUTO_TEST_SUITE(confirmation_tests)

BOOST_AUTO_TEST_CASE(confirmation_) try {
    auto priv_key = private_key::generate();
    auto pub_key = priv_key.get_public_key();

    auto chain = confirmation_type {
        fc::sha256("a"),
        { fc::sha256("b"), fc::sha256("c"), fc::sha256("d") }
    };

    auto conf_msg = make_network_msg(chain, priv_key);

    BOOST_TEST(chain.base_block == conf_msg->data.base_block);
    BOOST_TEST(chain.blocks == conf_msg->data.blocks);
    BOOST_TEST(true == validate_network_msg(*conf_msg, pub_key));

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
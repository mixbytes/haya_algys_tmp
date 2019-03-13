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

using blocks_type = vector<block_id_type>;

inline auto make_chain_ptr(block_id_type lib, const blocks_type& blocks) {
    return std::make_shared<chain_type>(chain_type{lib, blocks});
}

inline auto get_pub_key() {
    return private_key::generate().get_public_key();
}

BOOST_AUTO_TEST_CASE(prefix_chain_one_node) try {
    auto lib_block_id = fc::sha256("beef");
    auto root = std::make_shared<prefix_node>(prefix_node{lib_block_id});
    prefix_chain_tree tree(root);
    BOOST_TEST(root, tree.get_root());
    BOOST_REQUIRE_EQUAL(nullptr, tree.get_final_chain_head(1));
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE(prefix_chain_two_nodes) try {
    auto lib_block_id = fc::sha256("beef");
    auto root = std::make_shared<prefix_node>(prefix_node{lib_block_id});
    auto chain = chain_type{lib_block_id,
                            vector<block_id_type>{fc::sha256("a")}};
    prefix_chain_tree tree(root);
    tree.insert(std::make_shared<chain_type>(chain), get_pub_key());
    tree.insert(std::make_shared<chain_type>(chain), get_pub_key());
    auto head = tree.get_final_chain_head(2);
    BOOST_TEST(head != nullptr);
    BOOST_TEST(head->block_id == fc::sha256("a"));
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE(prefix_chain_test_longest) try {
    /*
     ++++++++A++++++++++++++
     ++++++++|\+++++++++++++
     ++++++++B+C++++++++++++
     ++++++++++|++++++++++++
     ++++++++++D++++++++++++
     */
    auto pub_key_1 = get_pub_key();
    auto pub_key_2 = get_pub_key();
    auto lib_block_id = fc::sha256("beef");
    auto root = std::make_shared<prefix_node>(prefix_node{lib_block_id});
    prefix_chain_tree tree(root);
    std::map<char, block_id_type> blocks;
    for (char c = 'a'; c <= 'd'; c++) {
        blocks[c] = fc::sha256(std::string{c});
    }
    auto chain1 = make_chain_ptr(lib_block_id, blocks_type{blocks['a'], blocks['b']});
    auto chain2 = make_chain_ptr(blocks['a'],  blocks_type{blocks['c'], blocks['d']});
    tree.insert(chain1, pub_key_1);
    tree.insert(chain2, pub_key_1);

    auto chain3 = make_chain_ptr(lib_block_id, vector<block_id_type>{blocks['a'], blocks['b']});
    tree.insert(chain3, pub_key_2);
    BOOST_TEST(blocks['b'] == tree.get_final_chain_head(2)->block_id);

    auto chain4 = make_chain_ptr(blocks['c'], blocks_type{blocks['d']});
    tree.insert(chain4, pub_key_2);
    BOOST_TEST(blocks['d'] == tree.get_final_chain_head(2)->block_id);

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE(prefix_chain_internal) try {
    auto pub_key_1 = get_pub_key();
    auto pub_key_2 = get_pub_key();

    auto lib_block_id = fc::sha256("beef");
    auto node = prefix_node{lib_block_id};

    prefix_chain_tree tree(std::make_shared<prefix_node>(node));
    auto chain = make_chain_ptr(lib_block_id, blocks_type{fc::sha256("abc"), fc::sha256("def")});
    tree.insert(chain, pub_key_1);

    const prefix_node_ptr root = tree.get_root();
    BOOST_REQUIRE_EQUAL(lib_block_id, root->block_id);
    BOOST_REQUIRE_EQUAL(1, root->adjacent_nodes.size());

    const auto chain_first_node = root->adjacent_nodes[0];
    BOOST_TEST(tree.find(fc::sha256("abc")) == chain_first_node);
    BOOST_TEST(chain_first_node->adjacent_nodes[0] == tree.get_final_chain_head(1));

    // add second chain
    chain = make_chain_ptr(fc::sha256("abc"), blocks_type{fc::sha256("bbc")});
    tree.insert(chain, pub_key_2);

    BOOST_REQUIRE_EQUAL(2, chain_first_node->adjacent_nodes.size());
    BOOST_TEST(chain_first_node == tree.get_final_chain_head(2));

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

    BOOST_TEST(chain.base_block == conf_msg.data.base_block);
    BOOST_TEST(chain.blocks == conf_msg.data.blocks);
    BOOST_TEST(true == validate_network_msg(conf_msg, pub_key));

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
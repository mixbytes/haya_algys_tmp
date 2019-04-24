#!/usr/bin/env bash

echo $0 | ./bin/cleos wallet unlock

./bin/cleos set code eosio contracts/eosio.bios/eosio.bios.wasm
./bin/cleos set abi eosio  contracts/eosio.bios/eosio.bios.abi

./bin/cleos create account eosio prod1 EOS6doE2dVT5FuNQ6zCBdc7G3TikVTqNHzZtBims7pKi6M8cC3BA4 EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV
./bin/cleos create account eosio prod2 EOS55uftJHpfU9p6XsYJGUJB3sGZbqZoHbiFqSUiSQiGFa51RF28J EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV

./bin/cleos push action eosio setprods '[[{"producer_name": "eosio", "block_signing_key": "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"},
{"producer_name": "prod1", "block_signing_key": "EOS6doE2dVT5FuNQ6zCBdc7G3TikVTqNHzZtBims7pKi6M8cC3BA4"}]],
{"producer_name": "prod2", "block_signing_key": "EOS55uftJHpfU9p6XsYJGUJB3sGZbqZoHbiFqSUiSQiGFa51RF28J"}' -p eosio@active



FROM registry.gitlab.com/cyberos/infrastructure/cyberos-builder:latest
ARG symbol=SYS

ADD . cyberos
WORKDIR cyberos

RUN git submodule init && git submodule update --init --recursive

RUN echo "$(git rev-parse --abbrev-ref HEAD):$(git rev-parse HEAD)" > /etc/eosio-version \
    && cmake -H. -B"/opt/eosio" -GNinja -DCMAKE_BUILD_TYPE=Debug -DWASM_ROOT=/opt/wasm -DCMAKE_CXX_COMPILER=clang++ \
       -DCMAKE_C_COMPILER=clang -DCMAKE_INSTALL_PREFIX=/opt/eosio -DBUILD_MONGO_DB_PLUGIN=true -DCORE_SYMBOL_NAME=$symbol \
    && cmake --build /opt/eosio --target install \
    && cp /cyberos/Docker/config.ini / && ln -s /opt/eosio/contracts /contracts && cp /cyberos/Docker/nodeosd.sh /opt/eosio/bin/nodeosd.sh && ln -s /cyberos/tutorials /tutorials

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install openssl ca-certificates vim psmisc python3-pip libcurl4-openssl-dev libusb-1.0 && rm -rf /var/lib/apt/lists/*
RUN pip3 install numpy
ENV EOSIO_ROOT=/opt/eosio
RUN chmod +x /opt/eosio/bin/nodeosd.sh
RUN ulimit -c unlimited
ENV LD_LIBRARY_PATH /usr/local/lib
ENV PATH /opt/eosio/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

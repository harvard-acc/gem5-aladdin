FROM ubuntu:16.04
LABEL maintainer="Sam Xi"

# Install tools for development.
RUN apt-get update && apt-get install -y \
  python \
  python-pip \
  git \
  tmux \
  vim \
  cmake \
  wget \
  ack-grep

RUN pip install --upgrade pip

# Install gem5 dependencies.
RUN apt-get update -qq
RUN apt-get install -y m4 \
      libprotobuf-dev \
      protobuf-compiler \
      libsqlite3-dev \
      libtcmalloc-minimal4

# Install Aladdin dependencies.
RUN apt-get install -y \
      libboost-graph-dev \
      libboost-regex-dev \
      libpthread-stubs0-dev \
      libreadline-dev \
      libncurses5-dev

# Environment variables for gem5-aladdin
RUN mkdir -p /workspace
ENV ALADDIN_HOME /workspace/gem5-aladdin/src/aladdin
ENV TRACER_HOME /workspace/LLVM-Tracer
ENV LLVM_HOME /usr/local
ENV BOOST_ROOT /usr/include

ENV SHELL /bin/bash

# Install LLVM 3.4 from source.
WORKDIR /tmp
RUN wget -q http://releases.llvm.org/3.4.2/llvm-3.4.2.src.tar.gz && \
    tar -xf llvm-3.4.2.src.tar.gz

RUN wget -q http://releases.llvm.org/3.4.2/cfe-3.4.2.src.tar.gz && \
    tar -xf cfe-3.4.2.src.tar.gz && \
    mkdir -p llvm-3.4.2.src/tools/clang && \
    mv cfe-3.4.2.src/* llvm-3.4.2.src/tools/clang && \
    cd llvm-3.4.2.src && \
    mkdir build && \
    cd build && \
    cmake -DCMAKE_INSTALL_PREFIX=/usr/local .. && \
    make -j8 && \
    make install

# Install a newer version of ZLIB from source.
RUN wget -q https://www.zlib.net/zlib-1.2.11.tar.gz && \
    tar -xf zlib-1.2.11.tar.gz && \
    cd zlib-1.2.11 && \
    ./configure && \
    make && \
    make install

# Install libconfuse 3.2.1 from source.
RUN wget -q https://github.com/martinh/libconfuse/releases/download/v3.2.1/confuse-3.2.1.tar.gz && \
    tar -xf confuse-3.2.1.tar.gz && \
    cd confuse-3.2.1 && \
    ./configure && \
    make install

WORKDIR /workspace

# Build and install LLVM-Tracer
RUN git clone https://github.com/ysshao/LLVM-Tracer && \
    cd LLVM-Tracer && \
    mkdir bin && \
    mkdir lib && \
    mkdir build && cd build && \
    cmake .. && make && make install

# So we can link gem5 against libprotobuf when installed from apt.
ENV FORCE_CXX11_ABI 1

# Clone gem5-aladdin, but don't build. We'll assume the developer will build.
RUN apt-get install -y scons
RUN git clone https://github.com/harvard-acc/gem5-aladdin && \
    cd gem5-aladdin && \
    git submodule init && \
    git submodule update

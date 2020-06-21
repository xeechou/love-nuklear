#!/bin/bash
set -e

wget -qO /tmp/key1.asc https://apt.kitware.com/keys/kitware-archive-latest.asc
sudo apt-key add /tmp/key1.asc
echo 'deb https://apt.kitware.com/ubuntu/ bionic main' > /tmp/cmake-kitware.list
sudo mv /tmp/cmake-kitware.list /etc/apt/sources.list.d/

sudo apt-get update

sudo apt-get -y install

sudo apt-get -y install \
     cmake \
     gcc-8 \
     g++-8 \
     ninja-build \
     pkg-config \
     python3-pip \
     python3-setuptools \
     libfontconfig1-dev \
     liblua5.3-dev \
     libpam0g-dev

#register currdir
export CURDIR=$(pwd)

#install meson
export PATH=~/.local/bin:$PATH
pip3 install --user git+https://github.com/mesonbuild/meson.git@0.49

cd $(CURDIR)

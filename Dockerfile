FROM debian:jessie

RUN apt-get update

RUN apt-get install -y fuse
RUN apt-get install -y g++
RUN apt-get install -y git
RUN apt-get install -y libattr1-dev
RUN apt-get install -y make
RUN apt-get install -y patch
RUN apt-get install -y pylint
RUN apt-get install -y python3
RUN apt-get install -y python3-greenlet
RUN apt-get install -y python3-mako
RUN apt-get install -y python3-pip
RUN apt-get install -y realpath
RUN pip3 install mistune
RUN pip3 install orderedset

ADD . /root/fs

WORKDIR /root/fs/_build/linux64
ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8
RUN python3 ./drake -j $(nproc) //install --prefix=/usr

FROM ubuntu:xenial

# Install packages
RUN apt-get update
RUN apt-get install -y                          \
    ccache                                      \
    curl                                        \
    couchdb                                     \
    distcc                                      \
    fuse                                        \
    g++                                         \
    git                                         \
    libattr1-dev                                \
    libjpeg-dev                                 \
    libz-dev                                    \
    make                                        \
    patch                                       \
    pylint                                      \
    python3                                     \
    python3-greenlet                            \
    python3-mako                                \
    python3-pip                                 \
    python3-requests                            \
    realpath
RUN pip3 install --upgrade pip
RUN pip3 install mistune orderedset
RUN chmod a+r /etc/couchdb/local.ini

# Setup compilers, distcc and ccache
RUN ln -s $(which g++-5) /usr/bin/g++-5.3   &&  \
    ln -s $(which gcc-5) /usr/bin/gcc-5.3   &&  \
    ln -s $(which g++-5) /usr/bin/g++-5.3.1 &&  \
    ln -s $(which gcc-5) /usr/bin/gcc-5.3.1
RUN mkdir -p /usr/lib/distcc/bin/ /usr/lib/ccache/bin &&                \
    for suffix in '' '-5' '-5.3' '-5.3.1'; do                           \
        ln -s "$(which ccache)" "/usr/lib/ccache/bin/g++$suffix";      \
        ln -s "$(which ccache)" "/usr/lib/ccache/bin/gcc$suffix";      \
        ln -s "$(which distcc)" "/usr/lib/distcc/bin/g++$suffix";      \
        ln -s "$(which distcc)" "/usr/lib/distcc/bin/gcc$suffix";      \
    done
ENV PATH="/usr/lib/ccache/bin:/usr/lib/distcc/bin:$PATH"

# Install docker
RUN curl -fsSL https://get.docker.com/ | sh

# Add user
ARG user
ENV USER=$user
RUN useradd -m $USER
RUN usermod -aG docker $USER
USER $USER
ARG builddir
WORKDIR $builddir
ENTRYPOINT ["./drake"]

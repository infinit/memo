FROM alpine:latest

RUN apk add --update \
        attr-dev \
        bash \
        bsd-compat-headers \
        ca-certificates \
        cmake \
        curl \
        file \
        g++ \
        git \
        go \
        groff \
        jq \
        less \
        linux-headers \
        make \
        patch \
        perl \
        python3 \
        python3-dev \
    && python3 -m ensurepip \
    && pip3 install awscli mistune orderedset greenlet mako \
    && apk --purge -v del py-pip \
    && rm -rf /var/cache/apk/*

# RUN mkdir /lib64 && ln -s /lib/libc.musl-x86_64.so.1 /lib64/ld-linux-x86-64.so.2

ADD . /root/fs

RUN cat /root/fs/alpine_socket_header_signedness.patch | patch -p0 -d /

WORKDIR /root/fs/_build/alpine
RUN python3 ./drake //install --prefix=/usr/local

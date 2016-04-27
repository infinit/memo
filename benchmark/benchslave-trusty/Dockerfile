FROM ubuntu:trusty

RUN apt-get update
RUN apt-get install -y fuse
RUN apt-get install -y nfs-kernel-server
RUN apt-get install -y ssh
RUN apt-get install -y software-properties-common

RUN apt-add-repository multiverse
RUN apt-get update

RUN apt-get install -y bonnie++
RUN apt-get install -y dbench
RUN apt-get install -y iozone3

ADD .ssh /root/.ssh
RUN chmod -R go-rw /root/.ssh
ADD scripts /scripts
ADD server_home /server_home
ADD client_home /client_home

# NFS
EXPOSE 111/udp 111/tcp 2049/tcp 2049/udp
# Infinit
EXPOSE 60000/udp 60000/tcp

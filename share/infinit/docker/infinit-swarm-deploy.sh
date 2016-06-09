#! /bin/bash

# run me on the manager node, pass image env as url to infinit image

if test "$1" = "kill"; then
  docker service ls |grep infinit | awk '{print $1}' | xargs -n 1 docker service rm
  docker ps --no-trunc | grep infinit | awk '{print $1}' |xargs -n 1 docker kill
  for node in $(docker node ls |grep READY |grep -v '*' | awk {'print $2'}); do
    DOCKER_HOST=$node:2375 docker ps --no-trunc | grep infinit | awk '{print $1}' |DOCKER_HOST=$node:2375 xargs -n 1 docker kill
  done
  exit 0
fi

if test -z "$image"; then
  image=docker-image-infinit.tgz
fi

if echo $image |grep http; then
  wget -O docker-image-infinit.tgz $image
  image=docker-image-infinit.tgz
fi

if test -z "$beyond_image"; then
  beyond_image=docker-image-beyond.tgz
fi

if echo $beyond_image |grep http; then
  wget -O docker-image-beyond.tgz $beyond_image
  beyond_image=docker-image-beyond.tgz
fi


docker import $image infinit:latest
docker import $beyond_image beyond:latest


other_nodes=$(docker node ls |grep READY |grep -v '*' | awk {'print $2'})
self_node=$(docker node ls |grep '*' | awk '{print $3}')
self_id=$(docker node ls |grep '*' | awk '{print $1}')

# we hardcoded that one into docker
mount_host_root=
#mount_host_root="-v /:/tmp/hostroot"

docker run --privileged --rm $mount_host_root ubuntu nsenter --mount=/tmp/hostroot/proc/1/ns/mnt mount --make-shared /

# import infinit image on each node, and set / to shared mount
# WARNING: this requires nodes hostname to actually resolve!
for node in $other_nodes; do
  DOCKER_HOST=$node:2375 docker import $image infinit:latest
  DOCKER_HOST=$node:2375 docker run --privileged --rm $mount_host_root ubuntu nsenter --mount=/tmp/hostroot/proc/1/ns/mnt mount --make-shared /
done

network=infinit
# create our overlay network
docker network create --driver overlay --subnet 75.1.0.0/16 infinit


# start beyond
#BROKEN constraint="--constraint node.id==$self_id"
constraint=
docker service create --name beyond --network $network $constraint beyond /usr/bin/beyond --host 0.0.0.0

#get IP of beyond
ip=$(docker service inspect beyond |grep Addr |cut '-d"' -f 4 | cut -d/ -f 1)

beyond=http://$ip:8080

sleep 4

# initiate a default user
# FIXME docker run --net is broken, go through a service
docker service create --name infinit_init --network $network \
  -e INFINIT_BEYOND=$beyond \
  --restart-policy-max-attempts 1 \
  infinit \
  infinit-user --create --name default_user --email none@none.com --fullname default \
    --push --full --password docker
#FIXME: poll task count to know when it ends
sleep 5
docker service rm infinit_init

tcp=
tcp_fwd=
#tcp="--docker-socket-tcp --docker-socket-port 3210"
#tcp_fwd="-p 3210:3210"
log=
log="--log-path /tmp --log-level *model*:DEBUG,*filesys*:DEBUG,*over*:DEBUG"

# HARDCODED IN DOCKER mount_host_root_shared="-v /:/tmp/hostroot:shared"
mount_host_root_shared=
# run the mountpoint manager service
docker service create --name infinit --network $network --mode global \
    -e ELLE_LOG_LEVEL=infinit-daemon:DEBUG \
    -e ELLE_LOG_FILE=/tmp/daemon.log \
    -e INFINIT_BEYOND=$beyond \
    -e INFINIT_HTTP_NO_KEEPALIVE=1 \
    $mount_host_root_shared \
    infinit \
    infinit-daemon --start --foreground \
    --docker-socket-path /tmp/hostroot/run/docker/plugins \
    --docker-descriptor-path /tmp/hostroot/usr/lib/docker/plugins \
    --mount-root /tmp/hostroot/tmp/ \
    --docker-mount-substitute "hostroot/tmp:" \
    --default-user default_user \
    --default-network default_network \
    --login-user default_user:docker \
    $tcp $log

sleep 2

# Hmm, the plugin might take some time to be visible
docker volume ls

#create a default volume, on this host, so our running infinit-daemon has
#the storage

docker volume create --driver infinit --name default_volume@$RANDOM$RANDOM

# run the network to put storage online
docker run  -d --volume-driver infinit -v default_user/default_volume:/unused ubuntu sleep 30000d








# TEST ME WITH:
# DOCKER_HOST=ip-192-168-34-67.us-west-2.compute.internal:2375 docker run -i -t --rm -v cluster_user/cluster_volume:/shared ubuntu /bin/bash
# write to shared, exit and/or start on an other node, files are shared!

# apt-get update; apt-get install socat;
# (echo -e 'POST /VolumeDriver.Mount HTTP/1.0\r\nContent-Length: 39\r\nContent-Type: text/json\r\n\r\n{"Name": "default_user/default_volume"}\r\n' ; sleep 2) | socat stdio unix-client:/tmp/realroot/run/docker/plugins/infinit.sock | | cut '-d"' -f 8 > /tmp/mountpoint


# EXPERIMENTS
#storage_sid=$(docker service create  -p 51234:51234 -p 51234:51234/udp infinit /bin/bash /usr/bin/infinit-static.sh '' enable_storage)
# no way to know on which node the service is running
#storage_host=
#for node in $(docker node ls |grep READY |grep -v '*' | awk {'print $2'}); do
#  if DOCKER_HOST=$node:2375 docker ps --no-trunc |grep enable_storage; then
#    storage_host=$node
#  fi
#done
#storage_host=$(docker node ls |grep READY |grep -v '*' | awk {'print $2'} | tail -n 1)
#DOCKER_HOST=$storage_host:2375 docker run -d -p 51234:51234 -p 51234:51234/udp infinit /bin/bash /usr/bin/infinit-static.sh '' enable_storage

# get correct host to use
#manager_host=$(docker node ls |grep '*' | awk '{print $3}')



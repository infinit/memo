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

docker run --privileged --rm -v /:/tmp/hostroot ubuntu nsenter --mount=/tmp/hostroot/proc/1/ns/mnt mount --make-shared /

# import infinit image on each node, and set / to shared mount
for node in $other_nodes; do
  DOCKER_HOST=$node:2375 docker import $image infinit:latest
  DOCKER_HOST=$node:2375 docker run --privileged --rm -v /:/tmp/hostroot ubuntu nsenter --mount=/tmp/hostroot/proc/1/ns/mnt mount --make-shared /
done

# run beyond on manager
docker run -d -p 8080:8080 beyond /usr/bin/beyond --host 0.0.0.0

sleep 4
# initiate a default user
docker run -e INFINIT_BEYOND=http://$self_node:8080 infinit \
  infinit-user --create --name default_user --email none@none.com --fullname default \
    --push --full --password docker

tcp=
tcp_fwd=
#tcp="--docker-socket-tcp --docker-socket-port 3210"
#tcp_fwd="-p 3210:3210"
log=
log="--log-path /tmp --log-level *model*:DEBUG,*filesys*:DEBUG,*over*:DEBUG"
# cant run the volume plugin as a service: no --privileged
for node in $other_nodes $self_node ; do
  DOCKER_HOST=$node:2375 docker run $tcp_fwd -d --privileged \
    -p 51236:51236 -p 51236:51236/udp  \
    -e ELLE_LOG_LEVEL=infinit-daemon:DEBUG \
    -e ELLE_LOG_FILE=/tmp/daemon.log \
    -e INFINIT_BEYOND=http://$self_node:8080 \
    -e INFINIT_HTTP_NO_KEEPALIVE=1 \
    -v /:/tmp/hostroot:shared infinit \
    infinit-daemon --start --foreground \
    --docker-socket-path /tmp/hostroot/run/docker/plugins \
    --docker-descriptor-path /tmp/hostroot/usr/lib/docker/plugins \
    --mount-root /tmp/hostroot/tmp/ \
    --docker-mount-substitute "hostroot/tmp:" \
    --default-user default_user \
    --default-network default_network \
    --login-user default_user:docker \
    --advertise-host $node \
    $tcp $log
done

sleep 2

# Hmm, the plugin might take some time to be visible
docker volume ls

#create a default volume, on this host, so our running infinit-daemon has
#the storage
#note: since we don't have service --privileged, we can't use service
## so we can't user overlay networks, so we need to force a port
docker volume create --driver infinit --name default_volume@$RANDOM$RANDOM -o port=51236

# run the network to put storage online
docker run  -d --volume-driver infinit -v default_user/default_volume:/unused ubuntu sleep 30000d








# TEST ME WITH:
# DOCKER_HOST=ip-192-168-34-67.us-west-2.compute.internal:2375 docker run -i -t --rm -v cluster_user/cluster_volume:/shared ubuntu /bin/bash
# write to shared, exit and/or start on an other node, files are shared!



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



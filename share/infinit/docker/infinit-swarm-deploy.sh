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

docker import $image infinit:latest

if test -z "$INFINIT_PORT"; then
  INFINIT_PORT=51236
fi

# import infinit image on each node, and set / to shared mount
for node in $(docker node ls |grep READY |grep -v '*' | awk {'print $2'}); do
  DOCKER_HOST=$node:2375 docker import $image infinit:latest
  DOCKER_HOST=$node:2375 docker run --privileged --rm -v /:/tmp/hostroot ubuntu nsenter --mount=/tmp/hostroot/proc/1/ns/mnt mount --make-shared /
done
# run the storage on manager
docker run -d -e INFINIT_PORT=$INFINIT_PORT -p $INFINIT_PORT:$INFINIT_PORT -p $INFINIT_PORT:$INFINIT_PORT/udp infinit /bin/bash /usr/bin/infinit-static.sh '' enable_storage

# cant run the volume plugin as a service: no --privileged
manager_host=$(docker node ls |grep '*' | awk '{print $3}')
for node in $(docker node ls |grep READY |grep -v '*' | awk {'print $2'}); do
  DOCKER_HOST=$node:2375 docker run -d --privileged -v /:/tmp/hostroot:shared infinit /bin/bash /usr/bin/infinit-static.sh $manager_host:$INFINIT_PORT enable_volume_plugin
done






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



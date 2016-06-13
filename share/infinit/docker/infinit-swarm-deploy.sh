#! /bin/bash

# run me on the manager node, pass image env as url to infinit image

if test "$1" = "kill"; then
  docker service ls |grep infinit | awk '{print $1}' | xargs -n 1 docker service rm
  docker ps --no-trunc | grep infinit | awk '{print $1}' |xargs -n 1 docker kill
  for node in $(docker node ls |grep -i ready |grep -v '*' | awk {'print $2'}); do
    DOCKER_HOST=$node:2375 docker ps --no-trunc | grep infinit | awk '{print $1}' |DOCKER_HOST=$node:2375 xargs -n 1 docker kill
  done
  exit 0
fi

if ! test -z "$1"; then
  expect=$(( $1 + 1))
  while test $(docker node ls -q |wc -l) != $expect; do
    echo "Waiting for $expect nodes..."
  done
fi

other_nodes=$(docker node ls |grep -i ready |grep -v '*' | awk {'print $2'})
self_node=$(docker node ls |grep '*' | awk '{print $3}')
self_id=$(docker node ls |grep '*' | awk '{print $1}')

# we hardcoded that one into docker
mount_host_root=
#mount_host_root="-v /:/tmp/hostroot"

docker run --privileged --rm $mount_host_root ubuntu nsenter --mount=/tmp/hostroot/proc/1/ns/mnt mount --make-shared /

#  set / to shared mount
# WARNING: this requires nodes hostname to actually resolve!
for node in $other_nodes; do
  DOCKER_HOST=$node:2375 docker run --privileged --rm $mount_host_root ubuntu nsenter --mount=/tmp/hostroot/proc/1/ns/mnt mount --make-shared /
done

network=infinit
# create our overlay network
docker network create --driver overlay --subnet 75.1.0.0/16 infinit


# start beyond
#BROKEN constraint="--constraint node.id==$self_id"
constraint=
docker service create --name beyond --restart-max-attempts 1 --network $network $constraint beyond \
  bash -c "/usr/bin/beyond --host 0.0.0.0 >>/tmp/hostroot/tmp/beyond.log 2>&1"

sleep 10
# access beyond through hostname
beyond_id=$(docker service tasks beyond | tail -n 1 | awk '{print $1}')
beyond_host=beyond.1.$beyond_id
beyond=http://$beyond_host:8080

# access beyond through the virtual IP...
#ip=$(docker service inspect beyond |grep Addr |cut '-d"' -f 4 | cut -d/ -f 1)
#beyond=http://$ip:8080
# THIS IS WRONG docker node inspect does not see containers in other nodes!
#ip=$(docker network inspect infinit |grep IPv4Address |cut '-d"' -f 4 | cut -d/ -f 1)


# initiate a default user
# NOTE docker run --net is unsupported, go through a service
docker service create --name infinit_init --network $network \
  -e INFINIT_BEYOND=$beyond \
  -e ELLE_LOG_TIME=1 \
  -e ELLE_LOG_LEVEL=infinit-user:DEBUG \
  --restart-max-attempts 1 \
  infinit \
  sh -c 'infinit-user --create --name default_user --email none@none.com --fullname default --push --full --password docker >/tmp/hostroot/tmp/user_create.log 2>&1'


#FIXME: poll task count to know when it ends
sleep 5
while test $(docker service tasks infinit_init | wc -l) != 1; do
  sleep 1
done

docker service rm infinit_init

tcp=
tcp_fwd=
#tcp="--docker-socket-tcp --docker-socket-port 3210"
#tcp_fwd="-p 3210:3210"
log=
log="--log-path /tmp/hostroot/tmp/ --log-level *model*:DEBUG,*filesys*:DEBUG,*over*:DEBUG"

# HARDCODED IN DOCKER mount_host_root_shared="-v /:/tmp/hostroot:shared"
mount_host_root_shared=
# run the mountpoint manager service
# beyond runs with a mono-request-at-a-time test serve so sleep a bit
docker service create --name infinit --network $network --mode global \
    -e ELLE_LOG_LEVEL=infinit-daemon:DEBUG \
    -e ELLE_LOG_TIME=1 \
    -e ELLE_LOG_FILE=/tmp/hostroot/tmp/daemon.log \
    -e INFINIT_BEYOND=$beyond \
    -e INFINIT_HTTP_NO_KEEPALIVE=1 \
    $mount_host_root_shared \
    infinit \
    bash -c "sleep \$(( \$RANDOM / 2000)).\$RANDOM; infinit-daemon --start --foreground --docker-socket-path /tmp/hostroot/run/docker/plugins --docker-descriptor-path /tmp/hostroot/usr/lib/docker/plugins --mount-root /tmp/hostroot/tmp/ --docker-mount-substitute hostroot/tmp: --default-user default_user --default-network default_network --login-user default_user:docker --mount default_user/default_volume $tcp $log"

sleep 20

# Hmm, the plugin might take some time to be visible
docker volume ls

#create a default volume, on this host, so our running infinit-daemon has
#the storage

while ! docker volume create --driver infinit --name default_volume@$RANDOM$RANDOM -o nocache; do
  echo "Failed to create volume, waiting for plugin..."
  sleep 1
done

# run the network to put storage online
# NOT NEEDED, automounter will do it
#docker run  -d --volume-driver infinit -v default_user/default_volume:/unused ubuntu sleep 30000d


# start once without infinit backed volume
docker service create --name gen --mode global --restart-max-attempts 1 --publish 8081 grapheditor

# activate volume fetcher on each node to make docker aware of default_volume
# NOT NEEDE, automounter
# for n in $other_nodes; do DOCKER_HOST=$n:2375 docker volume ls ; done

# start again with infinit volume backend
#docker service create --name ge --mode global --restart-max-attempts 1 --mount type=volume,source=default_user/default_volume,target=/tmp/gw,writable=true --publish 8081 grapheditor
#mounts in services is broken, workaround
# NOT NEEDED, automounter force a mount on all nodes
#for n in $other_nodes; do
#  DOCKER_HOST=$n:2375 docker run -d --volume-driver infinit -v default_user/default_volume:/unused ubuntu sleep 1000d
#done

# hijack the mount with a bind
# since said mount is coming from infinit-daemon auto-mount, we need to wait for
# it to succeed
docker service create --name geinf --mode global --restart-max-attempts 1 --publish 8081 \
  grapheditor bash -c "mkdir /tmp/gw; while ! mount |grep default_user_default_volume; do sleep 2; done; mount -o bind \$(mount | grep default_user_default_volume |cut '-d ' -f 3) /tmp/gw && cd /root && python3 graphed.py >/tmp/hostroot/tmp/graphed.log 2>&1"


# hack to force a mount by posting directly to the driver
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

# Test network connectivity with a ping
# docker service create --name ping --restart-max-attempts 1 --mode global --network infinit ubuntu sh -c '/tmp/hostroot/bin/ping -c 4  beyond.1.1vc1x3tzofg82kbh47u9ia6a8 > /tmp/hostroot/tmp/ping.log'
# test beyond connectivity
#docker service create --name tb --network infinit --restart-max-attempts 1 \
#  --mode global -e LD_LIBRARY_PATH=/tmp/hostroot/lib/x86_64-linux-gnu:/tmp/hostroot/usr/lib/x86_64-linux-gnu \
#  ubuntu sh -c "/tmp/hostroot/usr/bin/wget -O - $beyond >/tmp/hostroot/tmp/wg.log 2>&1"
#
#docker service create --name tb --network infinit --restart-max-attempts 1 \
#  --mode global -e LD_LIBRARY_PATH=/tmp/hostroot/lib/x86_64-linux-gnu:/tmp/hostroot/usr/lib/x86_64-linux-gnu \
#  ubuntu sh -c "/tmp/hostroot/usr/bin/wget -O - http://75.1.0.2:8080 >/tmp/hostroot/tmp/wg.log 2>&1"
#
#test IP attrib
#docker service create --name ti --network infinit --restart-max-attempts 1 \
#--mode global ubuntu sh -c "/tmp/hostroot/sbin/ifconfig >/tmp/hostroot/tmp/ifconfig.log"
#test service mount
# docker service create --name test --restart-max-attempts 1 --mode global --mount source=/var,target=/tmp/var,writable=true,type=bind,propagation=shared ubuntu bash -c 'ls /tmp/var > /tmp/hostroot/tmp/ls.log'

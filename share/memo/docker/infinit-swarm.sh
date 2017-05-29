#! /bin/bash

set -e

#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-145-gd21d5f0
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-159-g6f0ca35
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-192-g41b6a48
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-200-g84be6ab
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-204-ge26cb81
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-386-g1aadd87
# master + connecting_it checked
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-399-geff4b79-check
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-424-g686e73a-debug2
# fix segv and assert in dock and kouncil
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-426-gc538142
# merge mefyl reconnection fixes
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-433-g8550d1c-b
#again
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-448-g4d4733a-f
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-453-g2189dbe
#0.8.0
#image=mefyl/infinit:0.8.0-rc-16-ga137129
#image=mefyl/infinit:0.8.0-rc-32-gc474f28
#image=mefyl/infinit:0.8.0-rc-38-gad76ea9
# fix rebalancing after delete
#image=mefyl/infinit:0.8.0-rc-58-g5a20779
#+gauge
#image=bearclaw/infinit:0.8.0-rc-60-g2d520b0-gauge
#image=bearclaw/infinit:0.8.0-rc-61-g47cb97b-gauge
# on master, --resign-on-shutdown
#image=bearclaw/infinit:0.8.0-rc-98-g5a65fa6
# remove had_value check in paxos.cc
#image=bearclaw/infinit:0.8.0-rc-99-gd63592f-rebalance-confirm
#proper
#image=bearclaw/infinit:0.8.0-rc-122-gb63cc0e
#hacked ignore chb in resign
#image=bearclaw/infinit:0.8.0-rc-141-gbb9a1cc-resign-chb
# hacked try to workaround missingkey
#image=bearclaw/infinit:0.8.0-rc-141-gbb9a1cc-resign-chb-mkw
# resign chb workaround + paxos return true for failed to propagate workaround
image=bearclaw/infinit:0.8.0-rc-141-gbb9a1cc-chb-returntrue
# read balancing
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-391-g0147bdf
# + cache ram disableable
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-393-gf505e81
# balancing random
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-394-ge7bd0af
#balancing random proper
#image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-394-g818403c
#network_options="--kelips --replication-factor 3 --eviction-delay 1s --kelips-contact-timeout 10s"
network_options="--protocol tcp --replication-factor 3 --eviction-delay 1s --disable-encrypt-rpc --disable-signature"
docker_mounts=
grpc_port=9000
grpc_host=0.0.0.0:$grpc_port
infinit_port=9001

# create the network
if docker network ls --filter name=infinit |grep -q infinit ; then
  echo "infinit network exists, not recreating it"
else
  docker network create --driver overlay infinit
fi
# create the configuration
if ! docker secret ls |grep -q infinit_volume; then
  tmpdir=/tmp/ifnt-$RANDOM$RANDOM$RANDOM
  mkdir $tmpdir
  chmod go-rwx $tmpdir

  docker_run="docker run --rm -v $tmpdir:/tmp/infinit -e INFINIT_HOME=/tmp/infinit $image"
  $docker_run infinit user create docker
  $docker_run infinit network create --as docker docker $network_options
  $docker_run infinit network update --as docker --name docker --protocol tcp
  $docker_run infinit volume create --as docker docker --network docker

  # write the configuration to docker secrets
  docker secret create infinit_user $tmpdir/.local/share/infinit/filesystem/users/docker
  docker secret create infinit_network $tmpdir/.local/share/infinit/filesystem/networks/docker/docker
  docker secret create infinit_volume $tmpdir/.local/share/infinit/filesystem/volumes/docker/docker

  rm -r $tmpdir
else
  echo "Infinit secrets are there, not recreating them"
fi

# run the service.
# to get the node list we should be able to dig A tasks.infinit but it does not work on 1.13.1
#run a port scan
command=" \
    exec >> /tmp/logs/run.log 2>&1
    ulimit -c unlimited && \
    infinit user import -i /run/secrets/infinit_user && \
    infinit network import --as docker -i /run/secrets/infinit_network && \
    infinit volume import --as docker -i /run/secrets/infinit_volume && \
    infinit silo create filesystem --name docker --as docker && \
    infinit network link --as docker -n docker/docker --storage docker && \
    apt-get install -y iproute2 && \
    export ip=\$(ip addr |grep inet |grep 10.0 |grep '/24' |awk '{print \$2'} |cut -d/ -f 1) && \
    export cid=\$(basename \$(head -n 1 /proc/1/cgroup)) && \
    exec >> /tmp/logs/run.log.\$ip.\$cid 2>&1 && \
    export ELLE_LOG_TIME_MICROSEC=1 INFINIT_BACKTRACE=1 INFINIT_RDV= ELLE_LOG_TIME=1 ELLE_LOG_LEVEL='*athena*:DEBUG,*cli*:DEBUG,*model*:DEBUG,*grpc*:DEBUG,*prometheus:LOG' && \
    exec infinit network run --as docker docker --resign-on-shutdown true --no-local-endpoints --no-public-endpoints --listen \$ip --advertise-host \$ip --prometheus 0.0.0.0:9090 --grpc $grpc_host --port $infinit_port --peer tasks.infinit:$infinit_port"
    
docker service create --name infinit --replicas 8 \
  --secret infinit_user \
  --secret infinit_network \
  --secret infinit_volume \
  --stop-grace-period 120s \
  $docker_mounts \
  --network infinit \
  --publish $infinit_port/udp \
  --publish $grpc_port \
  --mount type=bind,source=/tmp,destination=/tmp/logs \
  --mount type=bind,source=/tmp,destination=/tmp/cores \
  $image  \
  sh -x -c "$command"


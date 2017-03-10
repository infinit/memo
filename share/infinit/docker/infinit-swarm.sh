#! /bin/bash

set -e

image=mefyl/infinit:0.8.0-kouncil-lamport-clocks-merge-192-g41b6a48
network_options="--kelips --replication-factor 3 --eviction-delay 1s --kelips-contact-timeout 10s"
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
    exec > /tmp/logs/run.log 2>&1
    apt-get update && \
    apt-get install -y fping dnsutils iputils-ping net-tools && \
    export peers=\"\$(host tasks.infinit | grep 'has address' | cut '-d ' -f 4 | sed -e 's/^/--peer /' -e 's/\$/:$infinit_port/')\" && \
    infinit user import -i /run/secrets/infinit_user && \
    infinit network import --as docker -i /run/secrets/infinit_network && \
    infinit volume import --as docker -i /run/secrets/infinit_volume && \
    infinit silo create filesystem --name docker --as docker && \
    infinit network link --as docker -n docker/docker --storage docker && \
    export ELLE_LOG_TIME=1 ELLE_LOG_LEVEL='*model*:DEBUG,*grpc*:DEBUG' && \
    infinit network run --as docker docker --grpc $grpc_host --port $infinit_port \$peers"
    
docker service create --name infinit --mode global  \
  --secret infinit_user \
  --secret infinit_network \
  --secret infinit_volume \
  $docker_mounts \
  --network infinit \
  --publish $infinit_port/udp \
  --publish $grpc_port \
  --mount type=bind,source=/tmp,destination=/tmp/logs \
  $image  \
  sh -x -c "$command"


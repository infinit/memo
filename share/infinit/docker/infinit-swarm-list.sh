#! /bin/sh

# List running infinit containers: output one line per container with:
# hostname containerId infinitNodeId ipAddress numberOfConnectedPeers

containers=$(docker ps -q -f name=infinit)
for c in $containers; do
  peers=$(docker exec $c infinit network inspect docker --as docker --peers --script | jq length)
  ip=$(docker exec $c ip addr |grep inet |grep 10.0 |grep '/24' |awk '{print $2}' |cut -d/ -f 1)
  id=$(docker exec $c infinit network inspect docker --as docker --all --script | jq .overlay.id)
  echo $(hostname) $c $id $ip $peers
done

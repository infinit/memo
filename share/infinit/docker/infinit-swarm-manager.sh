#! /bin/sh


MANAGER_INFINIT_ENDPOINT=$1
KV_URL=$2

# Create network infrastructure: two users, storage on the manager

infinit-user --create --name cluster_manager
infinit-storage --create --name cluster_storage --filesystem
infinit-network --create --name cluster_network --kelips --storage cluster_storage --as cluster_manager --port 51234
infinit-volume --create --name cluster_volume --network cluster_network --as cluster_manager

infinit-user --create --name cluster_node

infinit-user --export --full --name cluster_node \
  | INFINIT_HOME=/tmp/node_home infinit-user --import

infinit-network --export --name cluster_network --as cluster_manager \
  | INFINIT_HOME=/tmp/node_home infinit-network --import --as cluster_node

infinit-volume --export --name cluster_volume --as cluster_manager\
  | INFINIT_HOME=/tmp/node_home infinit-volume --import --as cluster_node

infinit-passport --create --network cluster_network --user cluster_node --as cluster_manager
infinit-passport --export --network cluster_network --user cluster_node --as cluster_manager\
  | INFINIT_HOME=/tmp/node_home infinit-passport --import

INFINIT_HOME=/tmp/node_home infinit-network --link cluster_manager/cluster_network --as cluster_node
INFINIT_HOME=/tmp/node_home infinit-volume --update cluster_manager/cluster_volume \
  --as cluster_node --cache --peer $MANAGER_INFINIT_ENDPOINT
# initialize
echo '{"operation": "stat", "path": "/"}' \
  | infinit-volume --run cluster_volume --as cluster_manager -s

# set permissions
infinit-volume --run cluster_volume --as cluster_manager --mountpoint /tmp/mntcluster &
vol_pid=$!
while ! stat /tmp/mntcluster; do echo waiting for mount; sleep 1; done
infinit-acl --register --user cluster_node --path /tmp/mntcluster --network cluster_manager/cluster_network --as cluster_manager
infinit-acl --set --mode rw --enable-inherit --user cluster_node --path /tmp/mntcluster
kill $vol_pid
sleep 5
# run the storage node
infinit-network --run cluster_network --as cluster_manager &

# push the node configuration to KV
kv $KV_URL set infinit/users/cluster_node "$(infinit-user --export --full cluster_node)"
kv $KV_URL set infinit/users/cluster_manager "$(infinit-user --export cluster_manager)"

data_dir=/tmp/node_home/.local/share/infinit/filesystem
kv $KV_URL set infinit/networks/cluster_network "$(cat $data_dir/networks/cluster_manager/cluster_network)"
kv $KV_URL set infinit/volumes/cluster_volume "$(cat $data_dir/volumes/cluster_manager/cluster_volume)"


# PLAN B:create docker image for nodes with the configuration
#mkdir -p /tmp/infinit_swarm_node
#dockerfile=/tmp/infinit_swarm_node/Dockerfile
#echo '' > $dockerfile
#echo 'FROM ubuntu' >> $dockerfile
#echo 'MAINTAINER Infinit <contact@infinit.sh>' >> $dockerfile
#echo 'ADD /tmp/node_home/.local /root/' >> $dockerfile
#echo 'ADD /usr/bin/infinit* /usr/bin/' >> $dockerfile
#echo 'ADD /usr/lib /usr/' >> $dockerfile
#echo 'ENTRYPOINT ["infinit-daemon", "--start", "--foreground"]'  >> $dockerfile
#
#docker build /tmp/infinit_swarm_node






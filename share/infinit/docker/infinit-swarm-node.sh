#! /bin/sh

KV_URL=$1

# import infinit configuration from KV
kv $KV_URL get infinit/users/cluster_node | infinit-user --import
kv $KV_URL get infinit/users/cluster_manager | infinit-user --import

data_dir=~/.local/share/infinit/filesystem
mkdir -p $data_dir/networks/cluster_manager
mkdir -p $data_dir/volumes/cluster_manager
kv $KV_URL get infinit/networks/cluster_network > $data_dir/networks/cluster_manager/cluster_network
kv $KV_URL get infinit/volumes/cluster_volume > $data_dir/volumes/cluster_manager/cluster_volume

#start infinit-daemon
infinit-daemon --start --foreground \
  --docker-socket-path /tmp/hostroot/run/docker/plugins \
  --docker-descriptor-path /tmp/hostroot/usr/lib/docker/plugins \
  --mount-root /tmp/hostroot/tmp/ \
  --docker-mount-substitute "hostroot/tmp:"
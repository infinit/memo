#!/bin/bash


# Setup path with the docker binaries
PATH=$PATH:/usr/docker/bin

export LOCAL_HOSTNAME=`wget -qO- http://169.254.169.254/latest/meta-data/hostname`
echo "PATH=$PATH"
echo "NODE_TYPE=$NODE_TYPE"
echo "DYNAMODB_TABLE=$DYNAMODB_TABLE"
echo "Container Name=$HOSTNAME"
echo "HOSTNAME=$LOCAL_HOSTNAME"
echo "#================"

INFINIT_VERSION="0.6.0-docker-demo-1-185-ga2feedc"
INFINIT_DAEMON_IMAGE="bearclaw/infinit:$INFINIT_VERSION"
INFINIT_BEYOND_IMAGE="bearclaw/infinit-beyond:$INFINIT_VERSION"
INFINIT_FNETUSERMOUNT_IMAGE="bearclaw/infinit-fnetusermount:$INFINIT_VERSION"

get_primary_manager_ip()
{
    echo "Get Primary Manager IP"
    # query dynamodb and get the Ip for the primary manager.
    export MANAGER_IP=$(aws dynamodb get-item --table-name $DYNAMODB_TABLE --key '{"node_type":{"S": "primary_manager"}}' | jq -r '.Item.ip.S')
}

confirm_primary_ready()
{
    n=0
    until [ $n -ge 5 ]
    do
        get_primary_manager_ip
        echo "PRIMARY_MANAGER_IP=$MANAGER_IP"
        if [ -z "$MANAGER_IP" ]; then
            echo "Primary manager Not ready yet, sleep for 60 seconds."
            sleep 60
            n=$[$n+1]
        else
            echo "Primary manager is ready."
            break
        fi
    done
}


setup_manager()
{
    echo "Setup Manager"
    export PRIVATE_IP=`wget -qO- http://169.254.169.254/latest/meta-data/local-ipv4`
    
    echo "   PRIVATE_IP=$PRIVATE_IP"
    # try to write to the table as the primary_manager, if it succeeds then it is the first
    # and it is the primary manager. If it fails, then it isn't first, and treat the record
    # that is there, as the primary manager, and join that swarm.
    aws dynamodb put-item \
        --table-name $DYNAMODB_TABLE \
        --item '{"node_type":{"S": "primary_manager"},"ip": {"S":"'"$PRIVATE_IP"'"}}' \
        --condition-expression 'attribute_not_exists(node_type)' \
        --return-consumed-capacity TOTAL
    PRIMARY_RESULT=$?
    echo "   PRIMARY_RESULT=$PRIMARY_RESULT"
    
    mount --make-shared /
    # run mount helper
    if test -z "$DO_NOTHING"; then
      docker run --name infinit-mount-helper -d \
        --privileged                 \
        -v /tmp:/tmp:shared          \
        $INFINIT_FNETUSERMOUNT_IMAGE \
        bash -c "FNETUSERMOUNT_DEBUG=1 fnetusermount-server"
    fi
    if [ $PRIMARY_RESULT -ne 0 ]; then
        echo "   non-primary Manager"
        confirm_primary_ready
        docker swarm join --manager --listen-addr $PRIVATE_IP:4500 $MANAGER_IP:4500
        docker swarm update --auto-accept manager --auto-accept worker
        return 0
    fi
    
    docker swarm init --auto-accept manager --auto-accept worker --listen-addr $PRIVATE_IP:4500

    if ! test -z "$DO_NOTHING"; then
      return 0
    fi
    docker network create --driver overlay --subnet 75.1.0.0/16 infinit

    # run replicated beyond FIXME : run on all managers
    if docker service create --help 2>&1 |grep -q scale; then
      scale_arg=--scale
    else
      scale_arg=--replicas
    fi
    docker service create --name infinit-beyond --network infinit \
      $scale_arg $N_MANAGERS \
      -e PYTHONUNBUFFERED=1  \
      "$INFINIT_BEYOND_IMAGE" \
      beyond --host 0.0.0.0 --datastore-type DynamoDB \
      --dynamodb-region "$AWS_DEFAULT_REGION" \
      --dynamodb-table "$INFINIT_BEYOND_DYNAMODB_TABLE"
    sleep 2
    ip=$(docker service inspect infinit-beyond |grep Addr |cut '-d"' -f 4 | cut -d/ -f 1)
    INFINIT_BEYOND=http://$ip:8080
    # cant scan for beyond, it is available only in our overlay network
    sleep 8
 
    # create our initial user
    docker service create --name infinit_init --network infinit \
        -e INFINIT_BEYOND=$INFINIT_BEYOND \
        -e ELLE_LOG_TIME=1 \
        -e ELLE_LOG_LEVEL=infinit-user:DEBUG \
        --restart-max-attempts 1 \
        $INFINIT_DAEMON_IMAGE \
        infinit-user --create --name default_user --email none@none.com --fullname default --push --full --password docker

    sleep 5
    while test $(docker service tasks infinit_init | wc -l) != 1; do
      sleep 1
    done
    docker service rm infinit_init

    # run the infinit main service. no need for shared, we delegate mountpoint creation
    # FIXME: finer-grained binds
    # FIXME: provide storage on managers only
    # FIXME: figure out why mount detection fails
    docker service create --name infinit --network infinit --mode global \
    -e ELLE_LOG_LEVEL=infinit-daemon:DEBUG \
    -e ELLE_LOG_TIME=1 \
    -e INFINIT_BEYOND=$INFINIT_BEYOND \
    -e INFINIT_HTTP_NO_KEEPALIVE=1 \
    -e INFINIT_IS_MOUNTED_DUMMY_DELAY=4 \
    --mount type=bind,source=/tmp,target=/tmp,writable=true \
    --mount type=bind,source=/root,target=/root,writable=true \
    --mount type=bind,source=/,target=/tmp/hostroot,writable=true \
    $INFINIT_DAEMON_IMAGE \
    bash -c "sleep \$(( \$RANDOM / 2000)).\$RANDOM; infinit-daemon --start --foreground --docker-socket-path /tmp/hostroot/run/docker/plugins --docker-descriptor-path /tmp/hostroot/usr/lib/docker/plugins --mount-root /tmp --as default_user --default-network default_network --login-user default_user:docker --mount default_user/default_volume"

    while ! docker volume create --driver infinit --name default_volume@$RANDOM$RANDOM -o nocache -o replication-factor=$N_MANAGERS; do
      echo "Failed to create volume, waiting for plugin..."
      sleep 1
    done

    if ! test -z "$DEMO"; then
      # run our test service without infinit backend
      docker service create --name demo1 --mode global --restart-max-attempts 1 --publish 8081 bearclaw/grapheditor
      # run our test service with infinit backend
      docker service create --name demo2 --mode global --restart-max-attempts 1 --publish 8081 \
        --mount type=bind,source=/tmp,target=/tmp,writable=true \
        bearclaw/grapheditor bash -c "while ! mount |grep default_user_default_volume; do sleep 2; done; ln -s \$(mount | grep default_user_default_volume |cut '-d ' -f 3) /tmp/gw && cd /root && python3 graphed.py"
    fi
}


setup_node()
{
    echo " Setup Node"
    # setup the node, by joining the swarm.
    confirm_primary_ready
    echo "   MANAGER_IP=$MANAGER_IP"
    docker swarm join $MANAGER_IP:4500
        mount --make-shared /
    if ! test -z "$DO_NOTHING"; then
      return 0
    fi
    # run mount helper
    docker run --name infinit-mount-helper -d \
      --privileged                 \
      -v /tmp:/tmp:shared          \
      $INFINIT_FNETUSERMOUNT_IMAGE \
      bash -c "FNETUSERMOUNT_DEBUG=1 fnetusermount-server"
}

# if it is a manager, setup as manager, if not, setup as worker node.
if [ "$NODE_TYPE" == "manager" ] ; then
    echo " It's a Manager, run setup"
    setup_manager
else
    echo " It's a worker Node, run setup"
    setup_node
fi

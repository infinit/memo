#! /bin/sh

# This script can be used to serialize an infinit context
# (users, networks, passports, volumes) on the command line,
# facilitating running infinit commands through SSH, or into docker
# containers.
#
# It requires infinit to be installed on the other end of the 'pipe'.
#
# Examples:
#
# The following will execute "infinit-user --list" on "somehost" after
# importing the user "bob" from the local machine:
# $ ssh somehost $(infinit-context.sh --user bob -- infinit-user --list)
#
# This more complex example runs a volume in a docker container,
# on a network owned by a different user:
# $ docker run --rm -it bearclaw/infinit \
#   $(infinit-context.sh --as mounter --user-public owner --user mounter \
#     --network owner/network --passport mounter --link owner/network \
#     --volume owner/volume -- infinit-volume --run --as mounter owner/volume)
#
# Which can be rewriten as:
# $ docker run --rm -it bearclaw/infinit $(infinit-context.sh \
#    --as mounter --volume-deps owner/volume \
#    -- infinit-volume --run --as mounter owner/volume)


if test "$1" = "--help" || test "$1" = "-h"; then
  echo "infinit-context.sh [OPTIONS] -- COMMAND"
  echo "\tserialize an infinit 'context' specified by OPTIONS and run COMMAND"
  echo
  echo "Options:"
  echo "\t--as USER\t run all infinit commands as USER"
  echo "\t--home HOME\t set remote infinit home folder to HOME"
  echo "\t--user USER\t transfer user USER"
  echo "\t--user-public USER\t transfer public part of user USER"
  echo "\t--network NET\t transfer network NET"
  echo "\t--passport USER\t transfer passport for user USER on last transfered network"
  echo "\t--slink NETWORK[:PATH]\t link '--as' user on network NETWORK, adding storage on PATH"
  echo "\t--link NETWORK\t link user specified by --as with network NETWORK"
  echo "\t--volume VOLUME\t transfer volume VOLUME"
  echo "\t--volume-deps VOLUME\t transfer volume VOLUME and all dependencies"
  echo "\t--volume-deps-storage VOLUME\t transfer volume VOLUME and all dependencies, and create a storage"
  exit 0
fi


json_field()
{
  name=$2
  json=$1
  echo "$json" | grep -o "$name[^,}]*" | cut '-d"' -f 3
}

dependencies()
{
  volume=$1
  with_storage=$2
  network=$(json_field "$(infinit-volume --export $volume)" network)
  owner=$(echo $network | sed -re 's@/.*@@')
  echo -n " --ouser " $(infinit-user --export $INFINIT_USER --full| base64 -w 0)
  if test $INFINIT_USER != $owner; then
    echo -n " --ouser " $(infinit-user --export $owner | base64 -w 0)
    echo -n " --opassport " $(infinit-passport --export -n $network -u $INFINIT_USER | base64 -w 0)
  fi
  echo -n " --onetwork " $(infinit-network --export $network | base64 -w 0)
  echo -n " --ovolume " $(infinit-volume --export $volume | base64 -w 0)
  if $with_storage ; then
    echo -n " --oslink " $network
  else
     echo -n " --olink " $network
  fi
}

if test "$1" = "--out"; then
  shift
  SHORT=A:H:U:N:P:L:S:V:
  LONG=as:,home:,ouser:,onetwork:,opassport:,olink:,oslink:,ovolume:
  PARSED=`getopt --options $SHORT --longoptions $LONG --name "$0" -- "$@"`
  eval set -- "$PARSED"
  while true; do
      case "$1" in
      -A|--as)
        export INFINIT_USER=$2
        shift 2
        ;;
      -H|--home)
        export INFINIT_HOME=$2
        shift 2
        ;;
      -U|--ouser)
        echo $2 | base64 -d | infinit-user --import
        shift 2
        ;;
      -N|--onetwork)
        echo $2 | base64 -d | infinit-network --import
        shift 2
        ;;
      -P|--opassport)
        echo $2 | base64 -d | infinit-passport --import
        shift 2
        ;;
      -S|--oslink)
        network=$(echo $2 | sed -re 's/:.*//')
        path=$(echo $2 | sed -re 's/[^:]*:?//')
        sname=$(echo $INFINIT_USER-$network | sed -e 's@/@-@g')
        path_opt=
        if ! test -z "$path"; then
          path_opt="--path $path"
        fi
        infinit-storage --create --name $sname --filesystem $path_opt
        infinit-network --link $network --storage $sname
        shift 2
        ;;
      -L|--olink)
        infinit-network --link $2
        shift 2
        ;;
      -V|--ovolume)
        echo $2 | base64 -d | infinit-volume --import
        shift 2
        ;;
      --)
        shift
        break
        ;;
      *)
        break
        ;;
      esac
  done
  exec "$@"
fi

SHORT=a:H:u:n:p:l:v:V:S:
LONG=as:,home:,user:,user-public:,network:,passport:,link:,slink:,volume:,volume-deps:,volume-deps-storage:
  PARSED=`getopt --options $SHORT --longoptions $LONG --name "$0" -- "$@"`
  eval set -- "$PARSED"
  echo -n "infinit-context.sh --out"
  while true; do
      case "$1" in
      -a|--as)
        echo -n " --as $2"
        export INFINIT_USER=$2
        shift 2
        ;;
      -H|--home)
        echo -n " --home $2"
        shift 2
        ;;
      -u|--user)
        echo -n " --ouser " $(infinit-user --export --full $2 | base64 -w 0)
        shift 2
        ;;
      --user-public)
        echo -n " --ouser " $(infinit-user --export $2 | base64 -w 0)
        shift 2
        ;;
      -n|--network)
        echo -n " --onetwork " $(infinit-network --export $2 | base64 -w 0)
        network=$2
        shift 2
        ;;
      -p|--passport)
        echo -n " --opassport " $(infinit-passport --export -n $network -u $2 | base64 -w 0)
        shift 2
        ;;
      -l|--link)
        echo -n " --olink $2"
        shift 2
        ;;
      -s|--slink)
        echo -n " --oslink $2"
        shift 2
        ;;
      -v|--volume)
        echo -n " --ovolume " $(infinit-volume --export  $2 | base64 -w 0)
        shift 2
        ;;
      -V|--volume-deps)
        dependencies $2 false
        shift 2
        ;;
      --volume-deps-storage)
        dependencies $2 true
        shift 2
        ;;
      --)
        shift
        break
        ;;
      *)
        echo plop "'$@'" "'$1'" 1>&2
        break
      esac
  done
  echo " -- " "$@"

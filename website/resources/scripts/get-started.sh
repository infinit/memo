#! /bin/bash

#
# ---------- UTILITIES -------------------------------------------------------
#

#! /bin/bash

export ELLE_LOG_TIME=1
export ELLE_LOG_PID=1
export ELLE_LOG_LEVEL="infinit.overlay*:DEBUG,infinit.model*:DEBUG,infinit.filesystem*:DEBUG,reactor.filesystem*:DEBUG,athena*:DEBUG"

# ---------- globals ---------------------------------------------------------

ROLE_CLIENT="Client"
ROLE_SERVER="Server"
ROLE_CLIENT_SERVER="Client/Server"
ROLE_SERVER_CLIENT="${ROLE_CLIENT_SERVER}"

# ---------- output ----------------------------------------------------------

r()
{
  output="${1}"
  n="${2}"

  echo "${output}" | sed "${n}q;d"
}

oko()
{
  status="${1}"

  if [ ${status} -eq 0 ] ; then
    echo "ok"
  else
    echo "ko"
  fi
}

http_request()
{
  uri="${1}"

  curl -s "${1}"
}

json_get()
{
  document="${1}"
  action="${2}"

  echo ${document} | python -c "import json,sys;object=json.load(sys.stdin);${action}"
}

# ---------- utils -----------------------------------------------------------

random()
{
  head -c 32 /dev/random | md5 | perl -ne 'print lc'
}

arrayfy()
{
  path="${1}"

  _ifs=${IFS}
  IFS=$'\n'
  array=( $(cat ${path}) )
  for i in ${array[@]} ; do echo $i ; done
  IFS=${_ifs}
}

UNIQUE_TRUE=1
UNIQUE_FALSE=0
array_combine()
{
  unique="${1}"
  if [ ${unique} -eq ${UNIQUE_TRUE} ] ; then
    _sort_options="-du"
  else
    _sort_options="-d"
  fi
  shift
  _ifs="$IFS"
  IFS=$'\n'
  array=($(for item in ${@} ; do _array=("${!item}") ; for _item in "${_array[@]}" ; do echo "${_item}" ; done ; done | sort ${_sort_options}))
  IFS="${_ifs}"
  for i in ${array[@]} ; do
    echo ${i}
  done
}

array_compare()
{
  arrayA=("${!1}")
  arrayB=("${!2}")

  if [ ${#arrayA[@]} -ne ${#arrayB[@]} ] ; then
    echo "1"
    return
  fi

  _arrayA=$(echo ${arrayA[*]})
  _arrayB=$(echo ${arrayB[*]})

  [[ "${_arrayA}" = "${_arrayB}" ]]
  echo ${?}
}

# ---------- log -------------------------------------------------------------

_log_commands_path="/dev/null"
_log_outputs_path="/dev/null"

log_commands()
{
  _log_commands_path=$(mktemp)

  echo "[ ] Commands log file: ${_log_commands_path}"
}

log_outputs()
{
  _log_outputs_path=$(mktemp)

  echo "[ ] Outputs log file: ${_log_outputs_path}"
}

execute_foreground()
{
  action="${1}"

  echo ${action} >> "${_log_commands_path}"

  ${action} >>${_log_outputs_path} 2>&1
}

execute_background()
{
  action="${1}"
  error="${2}"

  echo ${action} "&" >> "${_log_commands_path}"

  if [ ! -f "${error}" ] ; then
    error="${_log_outputs_path}"
  fi

  ${action} >>${_log_outputs_path} 2>>${error} &
  echo ${!}
}

# ---------- home ------------------------------------------------------------

set_home()
{
  path="${1}"

  execute_foreground "export INFINIT_HOME=${path}"
}

# ---------- functionalities -------------------------------------------------

fqn()
{
  user="${1}"
  object="${2}"

  set +e
  echo "${object}" | grep -- "/" >/dev/null 2>&1
  if [ ${?} -eq 0 ] ; then
    echo "${object}"
  else
    echo "${user}/${object}"
  fi
  set -e
}

create_user()
{
  name="${1}"
  options="${2}"

  execute_foreground "infinit-user --create --name ${name} ${options}"
}

create_filesystem_storage()
{
  name="${1}"
  capacity="${2}"
  storage_path="${3}"

  execute_foreground "infinit-storage --create --filesystem --path ${storage_path} --name ${name} --capacity ${capacity}"
}

create_network()
{
  as="${1}"
  name="${2}"
  storages="${!3}"
  options="${4}"

  _storages=""
  for _storage in ${storages[@]} ; do
    _storages="${_storages} --storage ${_storage}"
  done

  execute_foreground "infinit-network --create --as ${as} ${_storages} --name ${name} ${options}"
}

run_network()
{
  as="${1}"
  name="${2}"
  peers="${!3}"
  options="${4}"

  _peers=""
  for _peer in ${peers[@]} ; do
    _peers="${_peers} --peer ${_peer}"
  done

  endpoints_path=$(mktemp)

  pid=$(execute_background "infinit-network --run --as ${as} --name ${name} --endpoints-file ${endpoints_path} ${_peers} ${options}")

  # wait for the endpoints to be published
  set +e
  echo "${options}" | grep -- "--publish\|--push-endpoints\|--push" >/dev/null 2>&1
  if [ ${?} -eq 0 ] ; then
    _fqn=$(fqn "${as}" "${name}")
    while true ; do
      _network=$(http_request "https://beyond.infinit.io/networks/${_fqn}")
      _endpoints=$(json_get "${_network}" "print(len(object['endpoints']))")
      if [ "${_endpoints}" != "0" ] ; then
        break
      fi
      sleep 2
    done
  fi
  set -e

  # XXX
  sleep 30

  echo "${pid}"
  echo "${endpoints_path}"
}

create_volume()
{
  as="${1}"
  network="${2}"
  name="${3}"
  options="${4}"

  execute_foreground "infinit-volume --create --as ${as} --network ${network} --name ${name} ${options}"
}

mount_volume()
{
  as="${1}"
  name="${2}"
  mount_point="${3}"
  peers="${!4}"
  options="${5}"

  _peers=""
  for _peer in ${peers[@]} ; do
    _peers="${_peers} --peer ${_peer}"
  done

  endpoints_path=$(mktemp)

  pid=$(execute_background "infinit-volume --mount --as ${as} --name ${name} --mountpoint ${mount_point} --endpoints-file ${endpoints_path} ${_peers} ${options}")

  # wait for the endpoints to be published, if hub-assisted
  set +e
  echo "${options}" | grep -- "--publish\|--push-endpoints\|--push" >/dev/null 2>&1
  if [ ${?} -eq 0 ] ; then
    _volume_path=$(export_volume "${as}" "${name}")
    track_file "${_volume_path}"
    _volume=$(cat "${_volume_path}")
    _network_name=$(json_get "${_volume}" "print(object['network'])")
    _fqn=$(fqn "${as}" "${_network_name}")
    while true ; do
      _network=$(http_request "https://beyond.infinit.io/networks/${_fqn}")
      _endpoints=$(json_get "${_network}" "print(len(object['endpoints']))")
      if [ "${_endpoints}" != "0" ] ; then
        break
      fi
      sleep 2
    done
  fi
  set -e

  # wait for the volume to be mounted
  set +e
  while true ; do
    mount | grep -- "${mount_point}" >/dev/null 2>&1
    if [ ${?} -eq 0 ] ; then
      break
    fi
    sleep 2
  done
  set -e

  # XXX
  sleep 30

  echo "${pid}"
  echo "${endpoints_path}"
}

transmit_identity()
{
  name="${1}"
  passphrase="${2}"
  options="${3}"

  execute_foreground "infinit-device --transmit --as ${name} --user --passphrase ${passphrase} --no-countdown ${options}"
}

receive_identity()
{
  name="${1}"
  passphrase="${2}"
  options="${3}"

  execute_foreground "infinit-device --receive --user --name ${name} --passphrase ${passphrase} ${options}"
}

export_item()
{
  object="${1}"
  as="${2}"
  name="${3}"
  file_path=$(mktemp)

  execute_foreground "infinit-${object} --export --as ${as} --name ${name} --output ${file_path}"

  echo "${file_path}"
}

export_user()
{
  name="${1}"
  options="${2}"
  file_path=$(mktemp)

  execute_foreground "infinit-user --export --name ${name} --output ${file_path} ${options}"

  echo "${file_path}"
}

export_passport()
{
  as="${1}"
  network="${2}"
  invitee="${3}"
  file_path=$(mktemp)

  execute_foreground "infinit-passport --export --as ${as} --network ${network} --user ${invitee} --output ${file_path}"

  echo "${file_path}"
}

export_user_public()
{
  export_user "${1}" ""
}

export_user_private()
{
  export_user "${1}" "--full"
}

export_network()
{
  export_item "network" "${1}" "${2}" "${3}"
}

export_volume()
{
  export_item "volume" "${1}" "${2}" "${3}"
}

import_item()
{
  object="${1}"
  file_path="${2}"

  execute_foreground "infinit-${object} --import --input ${file_path}"
}

import_user()
{
  import_item "user" "${1}"
}

import_network()
{
  import_item "network" "${1}"
}

import_volume()
{
  import_item "volume" "${1}"
}

import_passport()
{
  import_item "passport" "${1}"
}

unmount_volume()
{
  mount_point="${1}"
  pid="${2}"

  kill -INT "${pid}" >/dev/null 2>&1 || true
  sleep 15
  (umount force "${mount_point}" >/dev/null 2>&1 || true) &
  sleep 15
  kill -INT "${pid}" >/dev/null 2>&1 || true
  sleep 30
}

kill_run()
{
  pid="${1}"

  kill -INT "${pid}" >/dev/null 2>&1 || true
  sleep 15
}

create_passport()
{
  as="${1}"
  network="${2}"
  invitee="${3}"
  options="${4}"

  execute_foreground "infinit-passport --create --as ${as} --network ${network} --user ${invitee} ${options}"
}

link_device()
{
  as="${1}"
  network="${2}"
  storages="${!3}"
  options="${4}"

  _storages=""
  for _storage in ${storages[@]} ; do
    _storages="${_storages} --storage ${_storage}"
  done

  execute_foreground "infinit-network --link --as ${as} --name ${network} ${_storages} ${options}"
}

grant_access()
{
  path="${1}"
  mode="${2}"
  target="${3}"

  execute_foreground "infinit-acl --set --mode ${mode} --path ${path} --user ${target}"
}

storage_blocks()
{
  user="${1}"
  storage="${2}"

  storage_path=$(json_get $(infinit-storage --export --as "${user}" --name "${storage}") "print(object['path'])")

  for block in $(find ${storage_path} -type f) ; do
    basename ${block}
  done
}

# ---------- hub -------------------------------------------------------------

fetch_user()
{
  as="${1}"
  name="${2}"

  execute_foreground "infinit-user --fetch --as ${as} --name ${name}"
}

fetch_network()
{
  as="${1}"
  name="${2}"

  if [ ! -z "${name}" ] ; then
    _name="--name ${name}"
  else
    _name=""
  fi

  execute_foreground "infinit-network --fetch --as ${as} ${_name}"
}

fetch_volume()
{
  as="${1}"
  network="${2}"
  name="${3}"

  if [ ! -z "${network}" ] ; then
    _network="--network ${network}"
  else
    _network=""
  fi

  if [ ! -z "${name}" ] ; then
    _name="--name ${name}"
  else
    _name=""
  fi

  execute_foreground "infinit-volume --fetch --as ${as} ${_network} ${_name}"
}

fetch_passport()
{
  as="${1}"
  network="${2}"

  if [ ! -z "${network}" ] ; then
    _network="--network ${network}"
  else
    _network=""
  fi

  execute_foreground "infinit-passport --fetch --as ${as} ${_network}"
}

pull_item()
{
  object="${1}"
  as="${2}"
  name="${3}"

  execute_foreground "infinit-${object} --pull --as ${as} --name ${name}"
}

pull_user()
{
  name="${1}"

  execute_foreground "infinit-user --pull --as ${name} --name ${name}"
}

pull_network()
{
  as="${1}"
  name="${2}"

  pull_item "network" "${as}" "${name}"
}

pull_volume()
{
  as="${1}"
  name="${2}"

  pull_item "volume" "${as}" "${name}"
}

pull_passport()
{
  as="${1}"
  network="${2}"
  user="${3}"

  execute_foreground "infinit-passport --pull --as ${as} --network ${network} --user ${user}"
}

# ---------- track -----------------------------------------------------------

_cleanings_1=()
_cleanings_2=()
_cleanings_3=()
_cleanings_4=()
_cleanings_5=()
_cleanings_6=()
_cleanings_7=()
_cleanings_8=()

track_mount()
{
  mount_point="${1}"
  pid="${2}"

  _cleanings_1+=("unmount_volume ${mount_point} ${pid}")
}

track_run()
{
  pid="${1}"

  _cleanings_1+=("kill_run ${pid}")
}

track_file()
{
  path="${1}"

  _cleanings_7+=("rm ${path}")
}

track_directory()
{
  path="${1}"

  _cleanings_8+=("rm -Rf ${path}")
}

track_user()
{
  home="${1}"
  name="${2}"

  _cleanings_6+=("set_home ${home}" "pull_user ${name}")
}

track_network()
{
  home="${1}"
  as="${2}"
  name="${3}"

  _cleanings_5+=("set_home ${home}" "pull_network ${as} ${name}")
}

track_volume()
{
  home="${1}"
  as="${2}"
  name="${3}"

  _cleanings_4+=("set_home ${home}" "pull_volume ${as} ${name}")
}

track_passport()
{
  home="${1}"
  as="${2}"
  network="${3}"
  user="${4}"

  _cleanings_4+=("set_home ${home}" "pull_passport ${as} ${network} ${user}")
}

# ---------- clean -----------------------------------------------------------

clean()
{
  echo "[ ] Clean temporary files, processes, homes and mount points"

  for ((index = 0; index < ${#_cleanings_1[@]}; index++)) ; do
    echo -n "  o ${_cleanings_1[${index}]} ... "
    ${_cleanings_1[${index}]}
    oko ${?}
  done

  for ((index = 0; index < ${#_cleanings_2[@]}; index++)) ; do
    echo -n "  o ${_cleanings_2[${index}]} ... "
    ${_cleanings_2[${index}]}
    oko ${?}
  done

  for ((index = 0; index < ${#_cleanings_3[@]}; index++)) ; do
    echo -n "  o ${_cleanings_3[${index}]} ... "
    ${_cleanings_3[${index}]}
    oko ${?}
  done

  for ((index = 0; index < ${#_cleanings_4[@]}; index++)) ; do
    echo -n "  o ${_cleanings_4[${index}]} ... "
    ${_cleanings_4[${index}]}
    oko ${?}
  done

  for ((index = 0; index < ${#_cleanings_5[@]}; index++)) ; do
    echo -n "  o ${_cleanings_5[${index}]} ... "
    ${_cleanings_5[${index}]}
    oko ${?}
  done

  for ((index = 0; index < ${#_cleanings_6[@]}; index++)) ; do
    echo -n "  o ${_cleanings_6[${index}]} ... "
    ${_cleanings_6[${index}]}
    oko ${?}
  done

  for ((index = 0; index < ${#_cleanings_7[@]}; index++)) ; do
    echo -n "  o ${_cleanings_7[${index}]} ... "
    ${_cleanings_7[${index}]}
    oko ${?}
  done

  for ((index = 0; index < ${#_cleanings_8[@]}; index++)) ; do
    echo -n "  o ${_cleanings_8[${index}]} ... "
    ${_cleanings_8[${index}]}
    oko ${?}
  done
}

#
# ---------- SCRIPT ----------------------------------------------------------
#

#! /bin/bash
#
# Note that the options --async and --cache have been removed to make sure that
# when an operation returns, its actions have taken effects. This is necessary
# in a script where every operation depends on the previous one.

set -e

# ---------- imports ---------------------------------------------------------


log_commands
log_outputs

trap clean EXIT

# ---------- arguments -------------------------------------------------------

if ! [ -z ${1} ] ; then
  if ! [ -f ${1}/infinit-user ] ; then
    echo ""
    echo "[WARNING] ${1} does not seem to contain the binary infinit-user etc."
    echo ""
  fi
  export PATH="${1}:${PATH}"
fi

if ! [ -z ${2} ] ; then
  export INFINIT_BEYOND="${2}"
fi

which infinit-user >/dev/null 2>&1
if [ ${?} != 0 ] ; then
  echo "[usage] ${0} {path to command-line tools} {beyond's hostname:port}"
fi

# ---------- globals ---------------------------------------------------------

USER_NAME_A="alice.$(random)"
USER_NAME_B="bob.$(random)"

# ---------- device A --------------------------------------------------------

# homes
infinit_home_A=$(mktemp -d)
track_directory "${infinit_home_A}"
echo "[Device A] Home location: ${infinit_home_A}"
infinit_home_B=$(mktemp -d)
track_directory "${infinit_home_B}"
echo "[Device B] Home location: ${infinit_home_B}"
infinit_home_C=$(mktemp -d)
track_directory "${infinit_home_C}"
echo "[Device C] Home location: ${infinit_home_C}"

# create user
set_home "${infinit_home_A}"
echo "[Device A] Create user: ${USER_NAME_A}"
create_user "${USER_NAME_A}" "--email nobody+${USER_NAME_A}@infinit.sh --push"
track_user "${infinit_home_A}" "${USER_NAME_A}"

# create storage
storage_path_A=$(mktemp -d)
track_directory "${storage_path_A}"
echo "[Device A] Create local storage: ${storage_path_A}"
create_filesystem_storage "local" "1GB" "${storage_path_A}"

# create network
echo "[Device A] Create network: my-network"
storages_A=("local")
create_network "${USER_NAME_A}" "my-network" storages_A[@] "--kelips --push"
track_network "${infinit_home_A}" "${USER_NAME_A}" "my-network"

# create volume
echo "[Device A] Create volume: my-volume"
create_volume "${USER_NAME_A}" "my-network" "my-volume" "--push"
track_volume "${infinit_home_A}" "${USER_NAME_A}" "my-volume"

# mount volume
mount_point_A=$(mktemp -d)
track_directory "${mount_point_A}"
echo "[Device A] Mount volume 'my-volume': ${mount_point_A}"
peers_A=()
output_A=$(mount_volume "${USER_NAME_A}" "my-volume" "${mount_point_A}" peers_A[@] "--publish")
pid_A=$(r "${output_A}" 1)
track_file $(r "${output_A}" 2)
track_mount "${mount_point_A}" "${pid_A}"

# create file
echo "[Device A] Create and write file: awesome.txt"
echo "everything is" > ${mount_point_A}/awesome.txt

# transmit user identity from device A to device B
echo "[Device A] Transmit user identity from device A to device B"
passphrase=$(random)
transmit_identity "${USER_NAME_A}" "${passphrase}"

# ---------- device B --------------------------------------------------------

# home
set_home "${infinit_home_B}"

# receive identity
echo "[Device B] Receive identity from device A"
receive_identity "${USER_NAME_A}" "${passphrase}"

# fetch objects
echo "[Device B] Fetch network and volume"
fetch_network "${USER_NAME_A}" "my-network"
fetch_volume "${USER_NAME_A}" "my-network" "my-volume"

# link device
echo "[Device B] Link new device to network 'my-network'"
storages_B=()
link_device "${USER_NAME_A}" "my-network" storages_B[@]

# mount volume
mount_point_B=$(mktemp -d)
track_directory "${mount_point_B}"
echo "[Device B] Mount volume 'my-volume': ${mount_point_B}"
peers_B=()
output_B=$(mount_volume "${USER_NAME_A}" "my-volume" "${mount_point_B}" peers_B[@] "--publish")
pid_B=$(r "${output_B}" 1)
track_file $(r "${output_B}" 2)
track_mount "${mount_point_B}" "${pid_B}"

# read the file from device B
content_B=$(cat ${mount_point_B}/awesome.txt)
echo "[Device B] Read file 'awesome.txt': \"${content_B}\""
if [ "${content_B}" != "everything is" ] ; then
  echo "[error] unexpected content '${content_B}'"
  exit 1
fi

# ---------- device C --------------------------------------------------------

# home
set_home "${infinit_home_C}"

# create user
echo "[Device C] Create user: ${USER_NAME_B}"
create_user "${USER_NAME_B}" "--email nobody+${USER_NAME_B}@infinit.sh --push"
track_user "${infinit_home_C}" "${USER_NAME_B}"

# ---------- device A --------------------------------------------------------

# home
set_home "${infinit_home_A}"

# fetch user identity
echo "[Device A] Fetch user identity: ${USER_NAME_B}"
fetch_user "${USER_NAME_A}" "${USER_NAME_B}"

# invite user B
echo "[Device A] Invite user '${USER_NAME_B}' to the network 'my-network'"
create_passport "${USER_NAME_A}" "my-network" "${USER_NAME_B}" "--push"
track_passport "${infinit_home_A}" "${USER_NAME_A}" "my-network" "${USER_NAME_B}"

# ---------- device C --------------------------------------------------------

# home
set_home "${infinit_home_C}"

# fetch objects
echo "[Device C] Fetch objects"
fetch_user "${USER_NAME_B}" "${USER_NAME_A}"
fetch_network "${USER_NAME_B}"
fetch_volume "${USER_NAME_B}"
fetch_passport "${USER_NAME_B}"

# link device
echo "[Device C] Link device to network '${USER_NAME_A}/my-network'"
storages_C=()
link_device "${USER_NAME_B}" "${USER_NAME_A}/my-network" storages_C[@]

# mount volume
mount_point_C=$(mktemp -d)
track_directory "${mount_point_C}"
echo "[Device C] Mount volume '${USER_NAME_A}/my-volume': ${mount_point_C}"
peers_C=()
output_C=$(mount_volume "${USER_NAME_B}" "${USER_NAME_A}/my-volume" "${mount_point_C}" peers_C[@] "--publish")
pid_C=$(r "${output_C}" 1)
track_file $(r "${output_C}" 2)
track_mount "${mount_point_C}" "${pid_C}"

# check if the root directory can be accessed
if [ -x "${mount_point_C}" ] ; then
  echo "[error] wrongly accessible root directory"
  exit 1
fi
echo "[Device C] Check root directory accessibility: ok"

# ---------- device A --------------------------------------------------------

# home
set_home "${infinit_home_A}"

# grant user B access to the file
echo "[Device A] Grant access to '${USER_NAME_B}'"
grant_access "${mount_point_A}/" "r" "${USER_NAME_B}"
grant_access "${mount_point_A}/awesome.txt" "r" "${USER_NAME_B}"

# ---------- device C --------------------------------------------------------

# home
set_home "${infinit_home_C}"

# read the file from device C
content_C=$(cat ${mount_point_C}/awesome.txt)
echo "[Device C] Read file 'awesome.txt': \"${content_C}\""
if [ "${content_C}" != "everything is" ] ; then
  echo "[error] unexpected content '${content_C}'"
  exit 1
fi

# check that the same file is not writable
if [ -w "${mount_point_C}/awesome.txt" ] ; then
  echo "[error] wrongly writable file"
  exit 1
fi
echo "[Device C] Check file writability: ok"

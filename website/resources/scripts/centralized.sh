#! /bin/bash

#
# ---------- UTILITIES -------------------------------------------------------
#

#! /bin/bash

export ELLE_LOG_TIME=1
export ELLE_LOG_LEVEL="infinit.model.*:DEBUG,infinit.filesystem*:DEBUG"

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

  sleep 10

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

  sleep 10

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
USER_NAME_C="charlie.$(random)"

# XXX temporary
export INFINIT_RDV=""

# ---------- prolog ----------------------------------------------------------

# homes
infinit_home_As=$(mktemp -d)
track_directory "${infinit_home_As}"
echo "[Server A] Home location: ${infinit_home_As}"
infinit_home_Ac=$(mktemp -d)
track_directory "${infinit_home_Ac}"
echo "[Client A] Home location: ${infinit_home_Ac}"
infinit_home_Bc=$(mktemp -d)
track_directory "${infinit_home_Bc}"
echo "[Client B] Home location: ${infinit_home_Bc}"
infinit_home_Cc=$(mktemp -d)
track_directory "${infinit_home_Cc}"
echo "[Client C] Home location: ${infinit_home_Cc}"

# create user A
set_home "${infinit_home_Ac}"
echo "[Client A] Create user: ${USER_NAME_A}"
create_user "${USER_NAME_A}" "--email ${USER_NAME_A}@infinit.sh --push"
track_user "${infinit_home_Ac}" "${USER_NAME_A}"

# create user B
set_home "${infinit_home_Bc}"
echo "[Client B] Create user: ${USER_NAME_B}"
create_user "${USER_NAME_B}" "--email ${USER_NAME_B}@infinit.sh --push"
track_user "${infinit_home_Bc}" "${USER_NAME_B}"

# create user C
set_home "${infinit_home_Cc}"
echo "[Client B] Create user: ${USER_NAME_C}"
create_user "${USER_NAME_C}" "--email ${USER_NAME_C}@infinit.sh --push"
track_user "${infinit_home_Cc}" "${USER_NAME_C}"

# transmit user identity from client A to server A
set_home "${infinit_home_Ac}"
echo "[Client A] Transmit user identity from client A to server A"
passphrase=$(random)
transmit_identity "${USER_NAME_A}" "${passphrase}"

# ---------- server A --------------------------------------------------------

# home
set_home "${infinit_home_As}"

# receive identity
echo "[Server A] Receive identity from client A"
receive_identity "${USER_NAME_A}" "${passphrase}"

# create storage
storage_path_As=$(mktemp -d)
track_directory "${storage_path_As}"
echo "[Server A] Create local storage: ${storage_path_As}"
create_filesystem_storage "local" "1GB" "${storage_path_As}"

# create network
echo "[Server A] Create network: cluster"
storages_As=("local")
create_network "${USER_NAME_A}" "cluster" storages_As[@] "--kelips --replication-factor 1 --push"
track_network "${infinit_home_As}" "${USER_NAME_A}" "cluster"

# create volume
echo "[Server A] Create volume: shared"
create_volume "${USER_NAME_A}" "cluster" "shared" "--push"
track_volume "${infinit_home_As}" "${USER_NAME_A}" "shared"

# fetch user B and C into A's home
echo "[Server A] Fetch user B's and C's public identity"
fetch_user "${USER_NAME_A}" "${USER_NAME_B}"
fetch_user "${USER_NAME_A}" "${USER_NAME_C}"

# invite user B and C to network
echo "[Server A] Invite user '${USER_NAME_B}' to the network 'cluster'"
create_passport "${USER_NAME_A}" "cluster" "${USER_NAME_B}" "--push"
track_passport "${infinit_home_As}" "${USER_NAME_A}" "cluster" "${USER_NAME_B}"
echo "[Server A] Invite user '${USER_NAME_C}' to the network 'cluster'"
create_passport "${USER_NAME_A}" "cluster" "${USER_NAME_C}" "--push"
track_passport "${infinit_home_As}" "${USER_NAME_A}" "cluster" "${USER_NAME_C}"

# run network
echo "[Server A] Run network 'cluster'"
peers_As=()
output_As=$(run_network "${USER_NAME_A}" "cluster" peers_As[@] "--publish")
pid_As=$(r "${output_As}" 1)
track_file $(r "${output_As}" 2)
track_run "${pid_As}"

# ---------- client B...X ----------------------------------------------------

client_X()
{
  identifier="${1}"
  user="${2}"
  home="${3}"
  mount_point="${4}"

  # home
  set_home "${home}"

  # fetch objects
  echo "[Client ${identifier}] Fetch user, network, volume and passport"
  fetch_user "${user}" "${USER_NAME_A}"
  fetch_network "${user}"
  fetch_volume "${user}"
  fetch_passport "${user}"

  # link device
  echo "[Client ${identifier}] Link new device to network 'cluster'"
  storages_Xc=()
  link_device "${user}" "${USER_NAME_A}/cluster" storages_Xc[@]

  # mount volume
  track_directory "${mount_point}"
  echo "[Client ${identifier}] Mount volume 'shared': ${mount_point}"
  peers_Xc=()
  output_Xc=$(mount_volume "${user}" "${USER_NAME_A}/shared" "${mount_point}" peers_Xc[@] "--publish")
  pid_Xc=$(r "${output_Xc}" 1)
  track_file $(r "${output_Xc}" 2)
  track_mount "${mount_point}" "${pid_Xc}"

  sleep 3
}

# ---------- client A --------------------------------------------------------

# home
set_home "${infinit_home_Ac}"

# fetch objects
echo "[Client A] Fetch users, network and volume"
fetch_network "${USER_NAME_A}"
fetch_volume "${USER_NAME_A}"

# link device
echo "[Client A] Link new device to network 'cluster'"
storages_Ac=()
link_device "${USER_NAME_A}" "cluster" storages_Ac[@]

# mount volume
mount_point_Ac=$(mktemp -d)
track_directory "${mount_point_Ac}"
echo "[Client A] Mount volume 'shared': ${mount_point_Ac}"
peers_Ac=()
output_Ac=$(mount_volume "${USER_NAME_A}" "shared" "${mount_point_Ac}" peers_Ac[@] "--publish")
pid_Ac=$(r "${output_Ac}" 1)
track_file $(r "${output_Ac}" 2)
track_mount "${mount_point_Ac}" "${pid_Ac}"

sleep 3

# create file
echo "[Client A] Create and write file: awesome.txt"
echo "everything is" > ${mount_point_Ac}/awesome.txt

# ---------- client B --------------------------------------------------------

mount_point_Bc=$(mktemp -d)
client_X "B" "${USER_NAME_B}" "${infinit_home_Bc}" "${mount_point_Bc}"

# check permission to read the file
echo "[Client B] Check the permissions on the file 'awesome.txt'"
if [ -r "${mount_point_Bc}/awesome.txt" ] ; then
  echo "[error] ${USER_NAME_B} seems to be able to read the file"
  exit 1
fi

# ---------- client C --------------------------------------------------------

mount_point_Cc=$(mktemp -d)
client_X "C" "${USER_NAME_C}" "${infinit_home_Cc}" "${mount_point_Cc}"

# check permission to read the file
echo "[Client C] Check the permissions on the file 'awesome.txt'"
if [ -r "${mount_point_Cc}/awesome.txt" ] ; then
  echo "[error] ${USER_NAME_C} seems to be able to read the file"
  exit 1
fi

# ---------- client A --------------------------------------------------------

# home
set_home "${infinit_home_Ac}"

# grant user B and C access to the file
echo "[Client A] Grant access to '${USER_NAME_B}'"
grant_access "${mount_point_Ac}/" "r" "${USER_NAME_B}"
grant_access "${mount_point_Ac}/awesome.txt" "r" "${USER_NAME_B}"
echo "[Client A] Grant access to '${USER_NAME_C}'"
grant_access "${mount_point_Ac}/" "r" "${USER_NAME_C}"
grant_access "${mount_point_Ac}/awesome.txt" "r" "${USER_NAME_C}"

sleep 3

# ---------- client B --------------------------------------------------------

# home
set_home "${infinit_home_Bc}"

# read the file with user B
content_Bc=$(cat ${mount_point_Bc}/awesome.txt)
echo "[Client B] Read file 'awesome.txt': \"${content_Bc}\""
if [ "${content_Bc}" != "everything is" ] ; then
  echo "[error] unexpected content '${content_Bc}'"
  exit 1
fi

# ---------- client C --------------------------------------------------------

# home
set_home "${infinit_home_Cc}"

# read the file with user C
content_Cc=$(cat ${mount_point_Cc}/awesome.txt)
echo "[Client C] Read file 'awesome.txt': \"${content_Cc}\""
if [ "${content_Cc}" != "everything is" ] ; then
  echo "[error] unexpected content '${content_Cc}'"
  exit 1
fi

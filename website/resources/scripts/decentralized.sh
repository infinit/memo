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

  output_path=$(mktemp)

  pid=$(execute_background "infinit-network --run --as ${as} --name ${name} ${_peers} ${options}" "${output_path}")

  sleep 10

  endpoint=$(cat <"${output_path}" | grep "listening on 192.168.0" | sed -E "s/^.*listening on ([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+:[0-9]+).*$/\1/")

  echo "${pid}"
  echo "${endpoint}"
  echo "${output_path}"

  cat ${output_path} >> ${_log_outputs_path}
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

  output_path=$(mktemp)

  pid=$(execute_background "infinit-volume --mount --as ${as} --name ${name} --mountpoint ${mount_point} ${_peers} ${options}" "${output_path}")

  sleep 10

  endpoint=$(cat <"${output_path}" | grep "listening on 192.168.0" | sed -E "s/^.*listening on ([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+:[0-9]+).*$/\1/")

  echo "${pid}"
  echo "${endpoint}"
  echo "${output_path}"

  cat ${output_path} >> ${_log_outputs_path}
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
#
# In addition, because connecting a AWS S3 storage resource is complicated for
# such an example, device C contributes storage capacity through two local
# disks: local1 and local2; hence replacing S3 with local2.

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
USER_NAME_D="dave.$(random)"
USER_NAME_E="eve.$(random)"

# XXX temporary
export INFINIT_RDV=""

# ---------- prolog ----------------------------------------------------------

# homes
infinit_home_Acs=$(mktemp -d)
track_directory "${infinit_home_Acs}"
echo "[Client/Server A] Home location: ${infinit_home_Acs}"
infinit_home_Bc=$(mktemp -d)
track_directory "${infinit_home_Bc}"
echo "[Client B] Home location: ${infinit_home_Bc}"
infinit_home_Cs=$(mktemp -d)
track_directory "${infinit_home_Cs}"
echo "[Server C] Home location: ${infinit_home_Cs}"
infinit_home_Dcs=$(mktemp -d)
track_directory "${infinit_home_Dcs}"
echo "[Client/Server D] Home location: ${infinit_home_Dcs}"
infinit_home_Ecs=$(mktemp -d)
track_directory "${infinit_home_Ecs}"
echo "[Client/Server E] Home location: ${infinit_home_Dcs}"

# create user A
set_home "${infinit_home_Acs}"
echo "[Client/Server A] Create user: ${USER_NAME_A}"
create_user "${USER_NAME_A}" "--email ${USER_NAME_A}@infinit.sh --push"
track_user "${infinit_home_Acs}" "${USER_NAME_A}"

# create user B
set_home "${infinit_home_Bc}"
echo "[Client B] Create user: ${USER_NAME_B}"
create_user "${USER_NAME_B}" "--email ${USER_NAME_B}@infinit.sh --push"
track_user "${infinit_home_Bc}" "${USER_NAME_B}"

# create user C
set_home "${infinit_home_Cs}"
echo "[Server C] Create user: ${USER_NAME_C}"
create_user "${USER_NAME_C}" "--email ${USER_NAME_C}@infinit.sh --push"
track_user "${infinit_home_Cs}" "${USER_NAME_C}"

# create user D
set_home "${infinit_home_Dcs}"
echo "[Client/Server D] Create user: ${USER_NAME_D}"
create_user "${USER_NAME_D}" "--email ${USER_NAME_D}@infinit.sh --push"
track_user "${infinit_home_Dcs}" "${USER_NAME_D}"

# create user E
set_home "${infinit_home_Ecs}"
echo "[Client/Server E] Create user: ${USER_NAME_E}"
create_user "${USER_NAME_E}" "--email ${USER_NAME_E}@infinit.sh --push"
track_user "${infinit_home_Ecs}" "${USER_NAME_E}"

# ---------- client/server A -------------------------------------------------

# home
set_home "${infinit_home_Acs}"

# create storage
storage_path_Acs=$(mktemp -d)
track_directory "${storage_path_Acs}"
echo "[Client/Server A] Create local storage: ${storage_path_Acs}"
create_filesystem_storage "local" "1.5GB" "${storage_path_Acs}"

# create network
echo "[Client/Server A] Create network: cluster"
storages_Acs=("local")
create_network "${USER_NAME_A}" "cluster" storages_Acs[@] "--kelips --replication-factor 3 --push"
track_network "${infinit_home_Acs}" "${USER_NAME_A}" "cluster"

# create volume
echo "[Client/Server A] Create volume: shared"
create_volume "${USER_NAME_A}" "cluster" "shared" "--push"
track_volume "${infinit_home_Acs}" "${USER_NAME_A}" "shared"

# fetch user B, C, D and E into A's home
echo "[Client/Server A] Fetch user B's, C's, D's and E's public identity"
fetch_user "${USER_NAME_A}" "${USER_NAME_B}"
fetch_user "${USER_NAME_A}" "${USER_NAME_C}"
fetch_user "${USER_NAME_A}" "${USER_NAME_D}"
fetch_user "${USER_NAME_A}" "${USER_NAME_E}"

# invite user B, C, D and E to network
echo "[Client/Server A] Invite user '${USER_NAME_B}' to the network 'cluster'"
create_passport "${USER_NAME_A}" "cluster" "${USER_NAME_B}" "--push"
track_passport "${infinit_home_Acs}" "${USER_NAME_A}" "cluster" "${USER_NAME_B}"
echo "[Client/Server A] Invite user '${USER_NAME_C}' to the network 'cluster'"
create_passport "${USER_NAME_A}" "cluster" "${USER_NAME_C}" "--push"
track_passport "${infinit_home_Acs}" "${USER_NAME_A}" "cluster" "${USER_NAME_C}"
echo "[Client/Server A] Invite user '${USER_NAME_D}' to the network 'cluster'"
create_passport "${USER_NAME_A}" "cluster" "${USER_NAME_D}" "--push"
track_passport "${infinit_home_Acs}" "${USER_NAME_A}" "cluster" "${USER_NAME_D}"
echo "[Client/Server A] Invite user '${USER_NAME_E}' to the network 'cluster'"
create_passport "${USER_NAME_A}" "cluster" "${USER_NAME_E}" "--push"
track_passport "${infinit_home_Acs}" "${USER_NAME_A}" "cluster" "${USER_NAME_E}"

# mount volume
mount_point_Acs=$(mktemp -d)
track_directory "${mount_point_Acs}"
echo "[Client/Server A] Mount volume 'shared': ${mount_point_Acs}"
peers_Acs=()
output_Acs=$(mount_volume "${USER_NAME_A}" "shared" "${mount_point_Acs}" peers_Acs[@] "--publish")
pid_Acs=$(r "${output_Acs}" 1)
track_file $(r "${output_Acs}" 3)
# echo "[XXX] $(r "${output_Acs}" 3)"
track_mount "${mount_point_Acs}" "${pid_Acs}"

# ---------- client B --------------------------------------------------------

# home
set_home "${infinit_home_Bc}"

# fetch objects
echo "[Client B] Fetch user, network, volume and passport"
fetch_user "${USER_NAME_B}" "${USER_NAME_A}"
fetch_network "${USER_NAME_B}"
fetch_volume "${USER_NAME_B}"
fetch_passport "${USER_NAME_B}"

# link device
echo "[Client B] Link new device to network 'cluster'"
storages_Bc=()
link_device "${USER_NAME_B}" "${USER_NAME_A}/cluster" storages_Bc[@]

# mount volume
mount_point_Bc=$(mktemp -d)
track_directory "${mount_point_Bc}"
echo "[Client B] Mount volume 'shared': ${mount_point_Bc}"
peers_Bc=()
output_Bc=$(mount_volume "${USER_NAME_B}" "${USER_NAME_A}/shared" "${mount_point_Bc}" peers_Bc[@] "--publish")
pid_Bc=$(r "${output_Bc}" 1)
track_file $(r "${output_Bc}" 3)
# echo "[XXX] $(r "${output_Bc}" 3)"
track_mount "${mount_point_Bc}" "${pid_Bc}"

sleep 3

# ---------- server C --------------------------------------------------------

# home
set_home "${infinit_home_Cs}"

# fetch objects
echo "[Server C] Fetch user, network, volume and passport"
fetch_user "${USER_NAME_C}" "${USER_NAME_A}"
fetch_network "${USER_NAME_C}"
fetch_volume "${USER_NAME_C}"
fetch_passport "${USER_NAME_C}"

# create storage resources
storage_path_Cs1=$(mktemp -d)
track_directory "${storage_path_Cs1}"
echo "[Server C] Create local storage 1: ${storage_path_Cs1}"
create_filesystem_storage "local1" "2GB" "${storage_path_Cs1}"
storage_path_Cs2=$(mktemp -d)
track_directory "${storage_path_Cs2}"
echo "[Server C] Create local storage 2: ${storage_path_Cs2}"
create_filesystem_storage "local2" "4GB" "${storage_path_Cs2}"

# link device
echo "[Server C] Link new device to network 'cluster'"
storages_Cs=("local1" "local2")
link_device "${USER_NAME_C}" "${USER_NAME_A}/cluster" storages_Cs[@]

# run network
echo "[Server C] Run network 'cluster'"
peers_Cs=()
output_Cs=$(run_network "${USER_NAME_C}" "${USER_NAME_A}/cluster" peers_Cs[@] "--publish")
pid_Cs=$(r "${output_Cs}" 1)
track_file $(r "${output_Cs}" 3)
# echo "[XXX] $(r "${output_Cs}" 3)"
track_run "${pid_Cs}"

sleep 3

# ---------- client_server D...X ---------------------------------------------

client_server_X()
{
  identifier="${1}"
  user="${2}"
  home="${3}"
  mount_point="${4}"
  capacity="${5}"

  # home
  set_home "${home}"

  # fetch objects
  echo "[Client/Server ${identifier}] Fetch user, network, volume and passport"
  fetch_user "${user}" "${USER_NAME_A}"
  fetch_network "${user}"
  fetch_volume "${user}"
  fetch_passport "${user}"

  # create storage
  storage_path_Xcs=$(mktemp -d)
  track_directory "${storage_path_Xcs}"
  echo "[Client/Server ${identifier}] Create local storage: ${storage_path_Xcs}"
  create_filesystem_storage "local" "${capacity}" "${storage_path_Xcs}"

  # link device
  echo "[Client/Server ${identifier}] Link new device to network 'cluster'"
  storages_Xcs=("local")
  link_device "${user}" "${USER_NAME_A}/cluster" storages_Xcs[@]

  # mount volume
  track_directory "${mount_point}"
  echo "[Client/Server ${identifier}] Mount volume 'shared': ${mount_point}"
  peers_Xcs=()
  output_Xcs=$(mount_volume "${user}" "${USER_NAME_A}/shared" "${mount_point}" peers_Xcs[@] "--publish")
  pid_Xcs=$(r "${output_Xcs}" 1)
  track_file $(r "${output_Xcs}" 3)
  # echo "[XXX] $(r "${output_Xcs}" 3)
  track_mount "${mount_point}" "${pid_Xcs}"

  sleep 3
}

# ---------- client/server D -------------------------------------------------

mount_point_Dcs=$(mktemp -d)
client_server_X "D" "${USER_NAME_D}" "${infinit_home_Dcs}" "${mount_point_Dcs}" "5GB"

# ---------- client/server E -------------------------------------------------

mount_point_Ecs=$(mktemp -d)
client_server_X "E" "${USER_NAME_E}" "${infinit_home_Ecs}" "${mount_point_Ecs}" "5GB"

# ---------- client/server A -------------------------------------------------

# create file
set_home "${infinit_home_Acs}"
echo "[Client/Server A] Create and write file: awesome.txt"
echo "everything is" > ${mount_point_Acs}/awesome.txt

# ---------- client B --------------------------------------------------------

# check permission to read the file
set_home "${infinit_home_Bc}"
echo "[Client B] Check the permissions on the file 'awesome.txt'"
if [ -r "${mount_point_Bc}/awesome.txt" ] ; then
  echo "[error] ${USER_NAME_B} seems to be able to read the file"
  exit 1
fi

# ---------- client/server D -------------------------------------------------

# check permission to read the file
set_home "${infinit_home_Dcs}"
echo "[Client/Server D] Check the permissions on the file 'awesome.txt'"
if [ -r "${mount_point_Dcs}/awesome.txt" ] ; then
  echo "[error] ${USER_NAME_D} seems to be able to read the file"
  exit 1
fi

# ---------- client/server E -------------------------------------------------

# check permission to read the file
set_home "${infinit_home_Ecs}"
echo "[Client/Server E] Check the permissions on the file 'awesome.txt'"
if [ -r "${mount_point_Ecs}/awesome.txt" ] ; then
  echo "[error] ${USER_NAME_E} seems to be able to read the file"
  exit 1
fi

# ---------- client/server A -------------------------------------------------

# grant user B, D and E access to the file
set_home "${infinit_home_Acs}"
echo "[Client/Server A] Grant access to '${USER_NAME_B}'"
grant_access "${mount_point_Acs}/" "r" "${USER_NAME_B}"
grant_access "${mount_point_Acs}/awesome.txt" "r" "${USER_NAME_B}"
echo "[Client/Server A] Grant access to '${USER_NAME_D}'"
grant_access "${mount_point_Acs}/" "r" "${USER_NAME_D}"
grant_access "${mount_point_Acs}/awesome.txt" "r" "${USER_NAME_D}"
echo "[Client/Server A] Grant access to '${USER_NAME_E}'"
grant_access "${mount_point_Acs}/" "r" "${USER_NAME_E}"
grant_access "${mount_point_Acs}/awesome.txt" "r" "${USER_NAME_E}"

sleep 3

# ---------- client B --------------------------------------------------------

# read the file with user B
set_home "${infinit_home_Bc}"
content_Bc=$(cat ${mount_point_Bc}/awesome.txt)
echo "[Client B] Read file 'awesome.txt': \"${content_Bc}\""
if [ "${content_Bc}" != "everything is" ] ; then
  echo "[error] unexpected content '${content_Bc}'"
  exit 1
fi

# ---------- client/server D -------------------------------------------------

# read the file with user D
set_home "${infinit_home_Dcs}"
content_Dcs=$(cat ${mount_point_Dcs}/awesome.txt)
echo "[Client/Server D] Read file 'awesome.txt': \"${content_Dcs}\""
if [ "${content_Dcs}" != "everything is" ] ; then
  echo "[error] unexpected content '${content_Dcs}'"
  exit 1
fi

# ---------- client/server E -------------------------------------------------

# read the file with user E
set_home "${infinit_home_Ecs}"
content_Ecs=$(cat ${mount_point_Ecs}/awesome.txt)
echo "[Client/Server E] Read file 'awesome.txt': \"${content_Ecs}\""
if [ "${content_Ecs}" != "everything is" ] ; then
  echo "[error] unexpected content '${content_Ecs}'"
  exit 1
fi

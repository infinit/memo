#! /usr/bin/env python3

# This utility monitors a beyond, and runs infinit-network on all swarm nodes,
# providing storage.

'''
# Sample usage, running all commands through docker images:

export BEYOND_HOST=http://my_beyond:8081
export INFINIT_IMAGE=bearclaw/infinit:latest
export BEYOND_IMAGE=bearclaw/infinit-beyond:latest
export USERNAME=bob
export PASSWORD=bob
export USER=$USERNAME:$PASSWORD

# start beyond
docker run -e PYTHONUNBUFFERED=1 -d --publish 8081:8081 $BEYOND_IMAGE \
  beyond --port 8081 --host 0.0.0.0

# start monitor. Requires access to docker and infinit binaries
docker run -d                                                            \
  -v /home/docker/.ssh:/root/.ssh:ro                                     \
  -v /var/run/docker.sock:/var/run/docker.sock                           \
  -v /var/lib/docker/swarm/lb_name:/var/lib/docker/swarm/lb_name:ro      \
  -v /var/lib/docker/swarm/elb.config:/var/lib/docker/swarm/elb.config   \
  -v /usr/bin/docker:/usr/bin/docker                                     \
  -v /var/log:/var/log                                                   \
  -e PYTHONUNBUFFERED=1                                                  \
  $INFINIT_IMAGE                                                          \
  infinit-service-runner.py --watch --login $USER --beyond $BEYOND_HOST \
  --image $INFINIT_IMAGE

# mount a new volume on the default network
docker run --privileged -d -v /tmp:/tmp:shared  \
  $INFINIT_IMAGE \
  infinit-service-runner.py --mount --login $USER --beyond $BEYOND_HOST \
  --mountpoint /tmp/shared --volume default_volume --network default_network

# make an other network. It will be automatically detected and instantiated on all nodes
docker run --rm -it -e INFINIT_BEYOND=$BEYOND_HOST \
  $INFINIT_IMAGE \
  bash -c 'infinit-user --login --name $USERNAME --password $PASSWORD && infinit-network --create --name net2  --kelips --as $USERNAME --push'

# Manually provide storage on all nodes for given network 'mynet'
docker service create --name storage_mynet \
  --mode global \
  --mount source=/root/infinit,target=/root/infinit,type=bind,writable=true \
  -e INFINIT_HOME=/root/infinit \
  $INFINIT_IMAGE infinit-service-runner.py --run --network mynet \
  --beyond $BEYOND_HOST --login $USER
'''

# TODO:
# graceful exit on 'service rm'
# networking
#
import json
import os
import subprocess
import sys
import time
import pipes

def log(msg):
  print(msg)
  pass


class Infinit:
  def __init__(self, user, beyond):
    self._user = user
    self._beyond = beyond
    self._user_name = self._user.split(':')[0]
  def ifnt_run(self, cmd, raise_on_error = True, with_as = True, timeout=100):
    env = dict(os.environ)
    env['INFINIT_BEYOND'] = self._beyond
    if with_as:
      cmd += ' --as ' + self._user_name
    log('Running command: ' + str(cmd.split(' ')))
    out = ''
    err = ''
    if timeout is None:
      process = subprocess.Popen(
        ['env'] + cmd.split(' '),
        env = env,
      )
      process.wait()
    else:
      process = subprocess.Popen(
        ['env'] + cmd.split(' '),
        env = env,
        stdin =  subprocess.PIPE,
        stdout =  subprocess.PIPE,
        stderr =  subprocess.PIPE,
        )
      out, err = process.communicate(timeout = timeout)
      out = out.decode('utf-8')
      err = err.decode('utf-8')
    if process.returncode != 0 and raise_on_error:
      raise Exception(
        'command failed with code %s: %s\nstdout: %s\nstderr: %s' % \
        (process.returncode, '', out, err))
    elif process.returncode != 0:
      print('command failed with code %s: %s\nstdout: %s\nstderr: %s' % \
        (process.returncode, '', out, err))
    return out, err

  def ifnt_run_json(self, cmd):
    out, err = self.ifnt_run(cmd)
    try:
      return json.loads(out)
    except Exception as e:
      raise Exception('invalid JSON: %r' % out)


class InfinitNetworkRunner(Infinit):
  def __init__(self, user, beyond, network):
    super().__init__(user, beyond)
    self.__network = network
  def has_storage(self, name):
    storages = self.ifnt_run_json('infinit-storage --list -s')
    return len(list(filter(lambda x: x['name'] == name, storages))) != 0
  def prepare(self, with_storage):
    up = self._user.split(':')
    self.ifnt_run('infinit-user --login --name ' + up[0] + ' --password ' + up[1], with_as = False, raise_on_error = False)
    self.ifnt_run('infinit-network --fetch --name ' + self.__network, raise_on_error = False)
    if with_storage:
      storage_name = 'storage-' + self.__network.replace('/', '-')
      if not self.has_storage(storage_name):
        self.ifnt_run('infinit-storage --create --name ' + storage_name + ' --filesystem')
      self.ifnt_run('infinit-network --link --name ' + self.__network + ' --storage ' + storage_name, raise_on_error = False)
    else:
      try:
        self.ifnt_run('infinit-network --link --name ' + self.__network)
      except Exception:
        pass

  def run(self):
    self.prepare(True)
    self.ifnt_run('infinit-network --run --publish --name ' + self.__network , timeout=None)
    log('infinit-network exited')

  def mount(self, mountpoint, volume):
    self.prepare(False)
    try:
      self.ifnt_run('infinit-volume --fetch --name ' + volume)
    except Exception:
      self.ifnt_run('infinit-volume --create --name ' + volume
        +' --network ' + self.__network, raise_on_error = False)
      self.ifnt_run('infinit-volume --push --name ' + volume, raise_on_error = False)
    self.ifnt_run('infinit-volume --run --publish --name ' + volume
      + ' --mountpoint ' + mountpoint
      + ' --cache --async'
      + ' --allow-root-creation', timeout = None)

class InfinitServiceRunner(Infinit):
  def __init__(self, user, beyond, poll_interval=10,
               infinit_image = 'bearclaw/infinit:latest',
               network = 'docker',
               volume = 'docker'):
    super().__init__(user, beyond)
    self.__poll_interval = poll_interval
    self.__infinit_image = infinit_image
    self.__network = network
    self.__volume = volume

  def service_running(self, name):
    sname = 'infinit-network-' + name.replace('/', '-')
    try:
      self.ifnt_run('docker service inspect ' + sname, with_as = False)
    except Exception:
      return False
    return True

  def service_start(self, name):
    sname = 'infinit-network-' + name.replace('/', '-')
    runner_cmd = 'infinit-service-runner.py --run ' \
     + '--network ' + name         \
     + ' --beyond ' + self._beyond \
     + ' --login ' + self._user
    cmd = 'docker service create --name ' + sname + \
      ' --restart-max-attempts 0' \
      ' --mount source=/root/infinit,target=/root/infinit,type=bind,writable=true' \
      ' -e INFINIT_HOME=/root/infinit' \
      ' --mode global ' + self.__infinit_image + ' ' + runner_cmd
    log('starting ' + cmd)
    cmd = cmd.split(' ')
    subprocess.Popen(cmd)
  def list_networks(self):
    return self.ifnt_run_json('infinit-network --list -s')
  def init(self):
    up = self._user.split(':')
    try:
      self.ifnt_run('infinit-user --login --name ' + up[0] + ' --password ' + up[1], with_as = False)
    except Exception:
      self.ifnt_run('infinit-user --create --name ' + up[0]
        + ' --password ' + up[1]
        + ' --email nobody@nowhere.com --push --full')
    self.ifnt_run('infinit-network --fetch')
    networks = self.list_networks()
    self.ifnt_run('infinit-network --create --name ' + self.__network + ' --kelips --push', raise_on_error = False)
    self.ifnt_run('infinit-volume --create --name ' + self.__volume + ' --network ' + self.__network + ' --push', raise_on_error = False)
  def run(self):
    self.init()
    while True:
      self.ifnt_run('infinit-network --fetch')
      networks = self.list_networks()
      link_attempt = False
      for n in networks:
        if not n['linked']:
          link_attempt = True
          self.ifnt_run('infinit-network --link --name ' + n['name'],
            raise_on_error = False)
      if link_attempt:
        networks = self.list_networks()
      for n in networks:
        if n['linked'] and not self.service_running(n['name']):
          self.service_start(n['name'])
      time.sleep(self.__poll_interval)


def parse_options():
  import argparse
  # Parse options
  parser = argparse.ArgumentParser(add_help = False,
                                   description = 'Infinit service runner')
  parser.add_argument('-h', '--help',
                      action = 'help',
                      default = argparse.SUPPRESS,
                      help = 'Show this help message and exit')
  parser.add_argument('--watch', action='store_true', help = 'enter network watch mode')
  parser.add_argument('--run',   action='store_true', help = 'run a network')
  parser.add_argument('--login', type=str, help = 'user:password to login as', default='docker:docker')
  parser.add_argument('--beyond', type=str, help = 'beyond URL', default='https://beyond.infinit.sh')
  parser.add_argument('--image', type=str, help = 'infinit docker image to use')
  parser.add_argument('--network', type=str, help = 'network to connect to (run mode)', default='docker')
  parser.add_argument('--mount', action='store_true', help = 'mount a volume')
  parser.add_argument('--mountpoint', type=str, help = 'mountpoint')
  parser.add_argument('--volume', type=str, help = 'volume to mount', default='docker')
  parser.add_argument('--init', action='store_true', help = 'Initialize beyond with given user,network and volume')
  return parser.parse_args()

args = parse_options()
if os.environ.get('INFINIT_BEYOND', None) is not None:
  args.beyond = os.environ['INFINIT_BEYOND']
if os.environ.get('SECRET', None) is not None:
  args.login = args.login.split(':')[0] + ':' + os.environ['SECRET']
if not args.watch and not args.run and not args.mount and not args.init:
  args.run = True
if args.watch:
  ifr = InfinitServiceRunner(args.login, args.beyond, infinit_image = args.image)
  ifr.run()
elif args.run:
  inr = InfinitNetworkRunner(args.login, args.beyond, args.network)
  inr.run()
elif args.mount:
  inr = InfinitNetworkRunner(args.login, args.beyond, args.network)
  inr.mount(args.mountpoint, args.volume)
elif args.init:
  ifr = InfinitServiceRunner(args.login, args.beyond, infinit_image = args.image,
    network = args.network, volume = args.volume)
  ifr.init()

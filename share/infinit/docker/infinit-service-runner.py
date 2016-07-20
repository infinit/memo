#! /usr/bin/env python3

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
  def run(self):
    up = self._user.split(':')
    self.ifnt_run('infinit-user --login --name ' + up[0] + ' --password ' + up[1], with_as = False)
    self.ifnt_run('infinit-network --fetch --name ' + self.__network)
    storage_name = 'storage-' + self.__network.replace('/', '-')
    if not self.has_storage(storage_name):
      self.ifnt_run('infinit-storage --create --name ' + storage_name + ' --filesystem')
      self.ifnt_run('infinit-network --link --name ' + self.__network + ' --storage ' + storage_name)
    self.ifnt_run('infinit-network --run --publish --name ' + self.__network , timeout=None)
    log('infinit-network exited')

class InfinitServiceRunner(Infinit):
  def __init__(self, user, beyond, poll_interval=10,
               infinit_image = 'bearclaw/infinit:latest'):
    super().__init__(user, beyond)
    self.__poll_interval = poll_interval
    self.__infinit_image = infinit_image

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
      ' --mount source=/root/infinit,target=/root/infinit,type=bind' \
      ' -e INFINIT_HOME=/root/infinit' \
      ' --mode global ' + self.__infinit_image + ' ' + runner_cmd
    log('starting ' + cmd)
    cmd = cmd.split(' ')
    subprocess.Popen(cmd)
  def list_networks(self):
    return self.ifnt_run_json('infinit-network --list -s')
  def run(self):
    up = self._user.split(':')
    try:
      self.ifnt_run('infinit-user --login --name ' + up[0] + ' --password ' + up[1], with_as = False)
    except Exception:
      self.ifnt_run('infinit-user --create --name ' + up[0]
        + ' --password ' + up[1]
        + ' --email nobody@nowhere.com --push --full')
    self.ifnt_run('infinit-network --fetch')
    networks = self.list_networks()
    if len(networks) == 0:
      self.ifnt_run('infinit-network --create --name default_network --kelips --push')
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
  parser.add_argument('--login', type=str, help = 'user:password to login as')
  parser.add_argument('--beyond', type=str, help = 'beyond URL', default='https://beyond.infinit.io')
  parser.add_argument('--image', type=str, help = 'infinit docker image to use')
  parser.add_argument('--network', type=str, help = 'network to connect to (run mode)')
  return parser.parse_args()

args = parse_options()
if not args.watch and not args.run:
  raise Exception('Either watch or run must be specified')
if args.watch:
  ifr = InfinitServiceRunner(args.login, args.beyond, infinit_image = args.image)
  ifr.run()
else:
  inr = InfinitNetworkRunner(args.login, args.beyond, args.network)
  inr.run()
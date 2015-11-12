import cryptography

import json
import pipes
import shutil
import subprocess
import sys
import tempfile
import time
import os

import infinit.beyond
import infinit.beyond.bottle
import infinit.beyond.couchdb

class TemporaryDirectory:

  def __init__(self):
    self.__dir = None

  def __enter__(self):
    self.__dir = tempfile.mkdtemp()
    return self

  def __exit__(self, *args, **kwargs):
    shutil.rmtree(self.__dir)

  @property
  def dir(self):
    return self.__dir


class Infinit(TemporaryDirectory):

  def run(self, args, input = None, return_code = 0, env = {}):
    self.env = {
      'PATH': 'bin:backend/bin:/bin',
      'INFINIT_HOME': self.dir,
      'INFINIT_BEYOND': '127.0.0.1:4242',
      'INFINIT_RDV': ''
    }
    self.env.update(env)

    pretty = '%s %s' % (
      ' '.join('%s=%s' % (k, v) for k, v in self.env.items()),
      ' '.join(pipes.quote(arg) for arg in args))
    # print(pretty)
    if input is not None:
      if isinstance(input, list):
        input = '\n'.join(map(json.dumps, input)) + '\n'
      else:
        input = json.dumps(input) + '\n'
      input = input.encode('utf-8')
    process = subprocess.Popen(
      args + ['-s'],
      env = self.env,
      stdin =  subprocess.PIPE,
      stdout =  subprocess.PIPE,
      stderr =  subprocess.PIPE,
    )
    if input is not None:
      # FIXME: On OSX, if you spam stdin before the FDStream takes it
      # over, you get a broken pipe.
      time.sleep(0.5)
    out, err = process.communicate(input)
    if process.returncode != return_code:
      print(err.decode('utf-8'), file = sys.stderr)
      raise Exception('command failed with code %s: %s' % \
                      (process.returncode, pretty))
    try:
      out = out.decode('utf-8')
      if len(out.split('\n')) > 2:
        out = '[' + out.replace('\n', ',')[0:-1] + ']'
      return json.loads(out)
    except:
      return None
  def run_script(self, user = None, volume='volume', seq = None, **kvargs):
    cmd = ['infinit-volume', '--run', volume]
    if user is not None:
      cmd += ['--as', user]
    response = self.run(cmd, input = seq or kvargs)
    return response

def assertEq(a, b):
  if a != b:
    raise Exception('%r != %r' % (a, b))

import threading
import bottle
from functools import partial

def __enter__(self):
  thread = threading.Thread(
    target = partial(bottle.run, app = self, port = 4242))
  thread.daemon = True
  thread.start()
  while not hasattr(self, 'port'):
    import time
    time.sleep(.1)
  return self

@property
def host(self):
  return 'http://127.0.0.1:%s' % self.port

infinit.beyond.bottle.Bottle.__enter__ = __enter__
infinit.beyond.bottle.Bottle.host = host

class Beyond():

  def __init__(self):
    super().__init__()
    self.__app = None
    self.__beyond = None
    self.__bottle = None
    self.__couchdb = infinit.beyond.couchdb.CouchDB()
    self.__datastore = None

  def __enter__(self):
    couchdb = self.__couchdb.__enter__()
    self.__datastore = \
      infinit.beyond.couchdb.CouchDBDatastore(couchdb)
    self.__beyond = infinit.beyond.Beyond(
      datastore = self.__datastore,
      dropbox_app_key = 'db_key',
      dropbox_app_secret = 'db_secret',
      google_app_key = 'google_key',
      google_app_secret = 'google_secret',
    )
    self.__app = infinit.beyond.bottle.Bottle(self.__beyond)
    self.__app.__enter__()
    return self

  def __exit__(self, *args, **kwargs):
    pass

class User():

  def __init__(self, name, infinit):
    self.name = name
    self.storage = '%s/%s-storage' % (name, name)
    self.network = '%s/%s-network' % (name, name)
    self.volume = '%s/%s-volume' % (name, name)
    self.mountpoint = '%s/mountpoint' % infinit.dir
    os.mkdir(self.mountpoint)

    self.infinit = infinit

  def run(self, cli, **kargs):
    print('run as %s:\t' % self.name, cli)
    self.infinit.run(cli.split(' '), env = { 'INFINIT_USER': self.name }, **kargs)

  def run_split(self, args, **kargs):
    self.infinit.run(args, env = { 'INFINIT_USER': self.name }, **kargs)

  def exec(self, args, return_code = 0):
    process = subprocess.Popen(
      args,
      stdin =  subprocess.PIPE,
      stdout =  subprocess.PIPE,
      stderr =  subprocess.PIPE,
    )
    out, err = process.communicate(input)
    if process.returncode != return_code:
      print(err.decode('utf-8'), file = sys.stderr)
      raise Exception('command failed with code %s: %s' % \
                      (process.returncode, pretty))

  def async(self, cli, **kargs):
    thread = threading.Thread(
      target = partial(self.run, cli = cli, **kargs))
    thread.daemon = True
    thread.start()
    return thread

  def fail(self, cli, **kargs):
    self.infinit.run(cli.split(' '), return_code = 1, **kargs)

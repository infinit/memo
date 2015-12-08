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

from datetime import timedelta

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

class Unreachable(BaseException):

  def __init__(self):
    super().__init__("Unreachable code reached")

def unreachable():
  raise Unreachable()

class Infinit(TemporaryDirectory):

  def __init__(self, beyond = None):
    self.__beyond = beyond

  def run(self, args, input = None, return_code = 0, env = {}):
    self.env = {
      'PATH': 'bin:backend/bin:/bin:/usr/sbin',
      'INFINIT_HOME': self.dir,
      'INFINIT_RDV': ''
    }
    if self.__beyond is not None:
      self.env['INFINIT_BEYOND'] = self.__beyond.domain
    self.env.update(env)
    args.append('-s')
    pretty = '%s %s' % (
      ' '.join('%s=%s' % (k, v) for k, v in self.env.items()),
      ' '.join(pipes.quote(arg) for arg in args))
    if input is not None:
      if isinstance(input, list):
        input = '\n'.join(map(json.dumps, input)) + '\n'
      elif isinstance(input, dict):
        input = json.dumps(input) + '\n'
      pretty = 'echo %s | %s' % (
        pipes.quote(input.strip()), pretty)
      input = input.encode('utf-8')
    print(pretty)
    process = subprocess.Popen(
      args,
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
    process.wait()
    if process.returncode != return_code:
      reason = err.decode('utf-8')
      print(reason, file = sys.stderr)
      if process.returncode not in [0, 1]:
        unreachable()
      raise Exception('command failed with code %s: %s (reason: %s)' % \
                      (process.returncode, pretty, reason))
    out = out.decode('utf-8')
    try:
      return json.loads(out)
    except:
      _out = []
      for line in out.split('\n'):
        if len(line) == 0:
          continue
        try:
          _out.append(json.loads(line))
        except:
          _out.append('%s' % line);
      return _out

  def run_script(self, user = None, volume='volume', seq = None, **kvargs):
    cmd = ['infinit-volume', '--run', volume]
    if user is not None:
      cmd += ['--as', user]
    response = self.run(cmd, input = seq or kvargs)
    return response

def assertEq(a, b):
  if a != b:
    raise AssertionError('%r != %r' % (a, b))

def assertIn(a, b):
  if a not in b:
    raise AssertionError('%r not in %r' % (a, b))

import bottle

class Beyond():

  def __init__(self):
    super().__init__()
    self.__advance = timedelta()
    self.__server = bottle.WSGIRefServer(port = 0)
    self.__app = None
    self.__beyond = None
    self.__couchdb = infinit.beyond.couchdb.CouchDB()
    self.__datastore = None

  def __enter__(self):
    couchdb = self.__couchdb.__enter__()
    self.__datastore = \
      infinit.beyond.couchdb.CouchDBDatastore(couchdb)
    def run():
      self.__beyond = infinit.beyond.Beyond(
        datastore = self.__datastore,
        dropbox_app_key = 'db_key',
        dropbox_app_secret = 'db_secret',
        google_app_key = 'google_key',
        google_app_secret = 'google_secret',
      )
      setattr(self.__beyond, '_Beyond__now', self.now)
      self.__app = infinit.beyond.bottle.Bottle(beyond = self.__beyond)
      try:
        bottle.run(app = self.__app,
                   quiet = True,
                   server = self.__server)
      except Exception as e:
        raise e

    import threading
    from functools import partial
    thread = threading.Thread(target = run)
    thread.daemon = True
    thread.start()
    while self.__server.port == 0 and thread.is_alive():
      import time
      time.sleep(.1)
    if not thread.is_alive():
      raise Exception("Server is already dead")
    return self

  def __exit__(self, *args, **kwargs):
    self.__couchdb.__exit__()

  @property
  def domain(self):
    return "http://localhost:%s" % self.__server.port

  # XXX: Duplicated from beyond/tests/utils.py, could be merged someday.
  def now(self):
    import datetime
    return datetime.datetime.utcnow() + self.__advance

  def advance(self, seconds, set = False):
    if set:
      self.__advance = timedelta(seconds = seconds)
    else:
      self.__advance += timedelta(seconds = seconds)

class User():

  def __init__(self, name, infinit):
    self.name = name
    self.storage = '%s/%s-storage' % (name, name)
    self.network = '%s/%s-network' % (name, name)
    self.volume = '%s/%s-volume' % (name, name)
    self.mountpoint = '%s/mountpoint' % infinit.dir
    self.drive = '%s/%s-drive' % (name, name)
    os.mkdir(self.mountpoint)

    self.infinit = infinit

  def run(self, cli, **kargs):
    print('run as %s:\t' % self.name, cli)
    return self.infinit.run(cli.split(' '),
                            env = { 'INFINIT_USER': self.name }, **kargs)

  def run_split(self, args, **kargs):
    return self.infinit.run(args, env = { 'INFINIT_USER': self.name }, **kargs)

  def async(self, cli, **kargs):
    import threading
    from functools import partial
    thread = threading.Thread(
      target = partial(self.run, cli = cli, **kargs))
    thread.daemon = True
    thread.start()
    return thread

  def fail(self, cli, **kargs):
    self.infinit.run(cli.split(' '), return_code = 1, **kargs)

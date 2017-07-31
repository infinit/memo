import copy
import json
import os
import pipes
import shutil
import subprocess
import sys
import tempfile
import time

from difflib import unified_diff as udiff

import infinit.beyond
import infinit.beyond.bottle
import infinit.beyond.couchdb

from datetime import timedelta

cr = '\r\n' if os.environ.get('EXE_EXT') else '\n'
binary = 'memo'

def here():
  '''Find the top-level call.'''
  import inspect
  frame = inspect.currentframe()
  while frame.f_back:
    frame = frame.f_back
  finfo = inspect.getframeinfo(frame)
  return finfo.filename + ":" + str(finfo.lineno)

def log(*args, level='info'):
  print(here() + ':',
        # pass "info:" so that Emacs' compilation-mode don't believe
        # all these lines are errors.
        level + ':' if level else '',
        *args,
        file=sys.stderr, flush=True)

class TemporaryDirectory:

  def __init__(self, path = None):
    self.__dir = path
    self.__del = False

  def __enter__(self):
    if self.__dir is None:
      self.__dir = tempfile.mkdtemp()
      self.__del = True
    return self

  def __exit__(self, *args, **kwargs):
    if self.__del:
      shutil.rmtree(self.__dir)

  def __str__(self):
    return str(self.__dir)

  @property
  def dir(self):
    return self.__dir

class Unreachable(BaseException):

  def __init__(self):
    super().__init__("Unreachable code reached")

def unreachable():
  raise Unreachable()

class Memo(TemporaryDirectory):

  def __init__(self,
               beyond = None,
               memo_root = None,
               home = None,
               user = None):
    super().__init__(home)
    self.__beyond = beyond
    self.__memo_root = memo_root or ''
    self.__user = user
    self.__env = {}

  def __enter__(self):
    super().__enter__()
    return self

  @property
  def version(self):
    return self.run(['memo', '--version'])[0]

  @property
  def user(self):
    return self.__user

  @property
  def env(self):
    return self.__env

  @property
  def data_home(self):
    return '%s/.local/share/infinit/memo' % self.dir

  @property
  def state_path(self):
    return '%s/.local/state/infinit/memo' % self.dir

  @property
  def silos_path(self):
    return '%s/silos' % self.data_home

  @property
  def networks_path(self):
    return '%s/networks' % self.data_home

  @property
  def linked_networks_path(self):
    return '%s/linked_networks' % self.data_home

  @property
  def passports_path(self):
    return '%s/passports' % self.data_home

  @property
  def volumes_path(self):
    return '%s/volumes' % self.data_home

  @property
  def drives_path(self):
    return '%s/drives' % self.data_home

  def spawn(self,
            args,
            input = None,
            return_code = 0,
            env = {},
            noscript = False,
            gdb = False,
            valgrind = False,
            binary = binary):
    if isinstance(args, str):
      args = args.split(' ')
    if args[0][0] != '/':
      if '/' not in args[0]:
        args[0] = 'bin/%s' % args[0]
      build_dir = os.environ.get('BUILD_DIR')
      if build_dir:
        args[0] = '%s/%s' % (build_dir, args[0])
    args[0] += os.environ.get('EXE_EXT', '')
    if gdb:
      args = ['/usr/bin/gdb', '--args'] + args + ['-s']
      if input is not None:
        log('GDB input: %s' % json.dumps(input))
        input = None
    elif valgrind:
      args = ['/usr/bin/valgrind'] + args
    env_ = {
      'MEMO_BACKTRACE': '1',
      'MEMO_RDV': '',
    }
    if self.dir is not None:
      env_['MEMO_HOME'] = self.dir
    if self.__user is not None:
      env_['MEMO_USER'] = self.__user
    env_['WINEDEBUG'] = os.environ.get('WINEDEBUG', '-all')
    for k in ['ELLE_LOG_LEVEL', 'ELLE_LOG_FILE', 'ELLE_LOG_TIME',
              'MEMO_CACHE_HOME']:
      if k in os.environ:
        env_[k] = os.environ[k]
    if self.__beyond is not None:
      env_['MEMO_BEYOND'] = self.__beyond.domain
    env_.update(env)
    env_.update(self.__env)
    if input is not None and not noscript:
      args.append('-s')
    pretty = '%s %s' % (
      ' '.join('%s=%s' % (k, v) for k, v in sorted(env_.items())),
      ' '.join(pipes.quote(arg) for arg in args))
    if input is not None:
      if isinstance(input, list):
        input = '\n'.join(map(json.dumps, input)) + cr
      elif isinstance(input, dict):
        input = json.dumps(input) + cr
      pretty = 'echo %s | %s' % (
        pipes.quote(input.strip()), pretty)
      input = input.encode('utf-8')
    log(pretty)
    process = subprocess.Popen(
      args,
      env = env_,
      stdin = subprocess.PIPE if not gdb else None,
      stdout = subprocess.PIPE if not gdb else None,
      stderr = subprocess.PIPE if not gdb else None,
    )
    self.process = process
    if input is not None:
      process.stdin.write(input)
    process.pretty = pretty
    return process

  def run(self,
          args,
          input = None,
          return_code = 0,
          env = {},
          gdb = False,
          valgrind = False,
          timeout = 600,
          noscript = False,
          binary = binary,
          kill = False):
    '''Return (stdout, stderr).'''
    args = [binary] + args
    try:
      process = self.spawn(
        args, input, return_code, env,
        gdb = gdb, valgrind = valgrind,
        noscript = noscript)
      out, err = process.communicate(timeout = timeout)
      process.wait()
    except (subprocess.TimeoutExpired, KeyboardInterrupt):
      process.kill()
      try:
        out, err = process.communicate(timeout = 15)
      except ValueError as e:
        log("Got exception while trying to kill process:", e)
        # Python bug, throws ValueError. But in that case blocking read is fine
        # out = process.stdout.read()
        # err = process.stderr.read()
      log('STDOUT: %s' % out.decode('utf-8'))
      log('STDERR: %s' % err.decode('utf-8'))
      if kill:
        return out, err
      raise
    out = out.decode('utf-8')
    err = err.decode('utf-8')
    # log('STDOUT: %s' % out)
    # log('STDERR: %s' % err)
    if process.returncode != return_code:
      raise Exception(
        'command failed with code %s: %s\nstdout: %s\nstderr: %s' % \
        (process.returncode, process.pretty, out, err))
    self.last_out = out
    self.last_err = err
    return out, err

  def run_json(self, args, gdb = False, valgrind = False,
               *largs, **kwargs):
    out, err = self.run(args.split(' ') if isinstance(args, str) else args,
                        gdb = gdb, valgrind = valgrind,
                        *largs, **kwargs)
    try:
      res = [json.loads(l) for l in out.split(cr) if l]
      if len(res) == 0:
        return None
      elif len(res) == 1:
        return res[0]
      else:
        return res
    except Exception as e:
      raise Exception('invalid JSON: %r' % out)

def assertEq(a, b):
  if a == b:
    log('PASS: {} == {}'.format(a, b))
  else:
    def lines(s):
      s = str(s)
      if s[:-1] != '\n':
        s += '\n'
      return s.splitlines(1)

    diff = ''.join(udiff(lines(a),
                         lines(b),
                         fromfile='a', tofile='b'))
    raise AssertionError('%s: %r != %r\n%s' % (here(), a, b, diff))

def assertNeq(a, b):
  if a != b:
    log('PASS: {} != {}'.format(a, b))
  else:
    raise AssertionError('%r == %r' % (a, b))

def assertIn(a, b):
  if a in b:
    log('PASS: {} in {}'.format(a, b))
  else:
    raise AssertionError('%r not in %r' % (a, b))

def throws(f, contains = None):
  try:
    f()
    assert False
  except Exception as e:
    if contains is not None:
      assertIn(contains, str(e))
    pass

import bottle

class FakeGCS:

  def __init__(self):
    self.__icons = {}

  def upload(self, bucket, path, *args, **kwargs):
    self.__icons[path] = 'url'

  def delete(self, bucket, path):
    if path in self.__icons:
      del self.__icons[path]

  def download_url(self, bucket, path, *args, **kwargs):
    if path in self.__icons:
      return self.__icons[path]
    return None

class Emailer:

  def __init__(self):
    self.emails = {}

  def send_one(self, template, recipient_email, variables = {}, *args, **kwargs):
    self.__store(template, recipient_email, variables)

  def __store(self, template, recipient_email, variables):
    self.get_specifics(recipient_email, template).append(variables)

  def get(self, email):
    return self.emails.setdefault(email, {})

  def get_specifics(self, email, template):
    return self.get(email).setdefault(template, [])

class Beyond():

  def __init__(self, beyond_args = {}, disable_authentication = False,
               bottle_args = {}):
    super().__init__()
    self.__beyond_args = beyond_args
    self.__advance = timedelta()
    self.__server = bottle.WSGIRefServer(port = 0)
    self.__app = None
    self.__beyond = None
    self.__couchdb = infinit.beyond.couchdb.CouchDB()
    self.__datastore = None
    self.__gcs = FakeGCS()
    self.__hub_delegate_user = None
    self.__disable_authentication = disable_authentication
    self.__bottle_args = bottle_args

  def __enter__(self):
    couchdb = self.__couchdb.__enter__()
    self.__datastore = \
      infinit.beyond.couchdb.CouchDBDatastore(couchdb)
    def run():
      args = {
        'dropbox_app_key': 'db_key',
        'dropbox_app_secret': 'db_secret',
        'google_app_key': 'google_key',
        'google_app_secret': 'google_secret',
        'gcs_app_key': 'google_key',
        'gcs_app_secret': 'google_secret',
      }
      args.update(self.__beyond_args)
      self.__beyond = infinit.beyond.Beyond(
        datastore = self.__datastore,
        **args
      )
      setattr(self.__beyond, '_Beyond__now', self.now)
      bargs = {
        'beyond': self.__beyond,
        'gcs': self.__gcs
      }
      bargs.update(self.__bottle_args)
      self.__app = infinit.beyond.bottle.Bottle(**bargs)
      if self.__disable_authentication:
        self.__app.authenticate = lambda x: None
      self.emailer = Emailer()
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
    # Register hub user.
    import requests
    self.__hub_delegate_user = {
      'email': 'hub@infini.io',
      'name': self.__beyond.delegate_user,
      'private_key': {
        'rsa': 'MIIEowIBAAKCAQEAspp/p8TnTRLao+KeBnz1tvlAC3UKAjXOmfyVJw0Lpe29mxbnAq3uUD8Um5t5jHwYX3P8r+FOL83Yt41Y+dkbWM3hQoYA2Et4ypDRUQ3k+ku6kNHkRhRY9nmAhHM9L8C5VlcYQG197mN/+h9sS1EarYV/BawY4VIcEj7z65Xv6Z0YYvEgjLXjlDmUPEg1wZOA2mcx8RcTyhvok5sl7WWO0J00sZSFqTHCFpZFiBY49cCax2+EuXMdlqlcnKZvWtQVQc5JR4T1vccx+TJUM3JeZpKNAVCI9von2CxXaqCaDwN3D9B6V7cgFW8j+PSSQFjri2341/zkK37IkicJAi/vqQIDAQABAoIBACdc/b2QHBpUiXONQp7Tc6Q8Eu1zgh0ylrJBhO3yZhrP5vYDei6Q/vEYtgYFoihgQU7oVUy+L2lByP3LXUzTmL9dwMPUnOMO3zRl7nwav9iaUCgS2mjHm0PXS4fljhq0MyTgVSv99b9QlqgdvNRsr6CGx5QMdf9TBXTQAxptFU87Ph5O8KrX8wgmFcWqSNEPh6yT9fhl9E0KxkuWh0x2zf8NpsUrBP1CQRhJsxtraTLfKTy8OowVYcx9mHAj4MHg2LVqjRn/QXN4IPdyU5wHMKk95Tf8sLByn0lAfiYM0SMUjy428ueY01WTl0+sN4lSJkHJ7Oz8fajMWeIQhm+wmrECgYEA1/nGE5XndPH82idwXcauGIWTW/jIJAI2VoqHHl7CW0Jw4Q1AeyyJB+3Tu+lUjNwTHDgq0fEjXyup1Hv2diPZecoiw/UWDqMHGawN9JXz/V6ro56eQN3jAuwg15Xig36CtEw8Ay9NdnD7pK/9h8vGsmtqwH3BR0qFR5PX33PE4VMCgYEA07O6/A9BCQpKYB7ovlPu9xxm5Y907HdyqYfSrz2RXP7m0VvXp18cB+KqqCfkipj/ckv2qAA/ng6P/43b+6o5li5g0wM83GwJ0UXIFeoClcTKXlP8x531eVwP58nFsDHUKd3F7hLdmBbAizVV6WQqKFL7g/H+K9mjCTW0vskQn5MCgYAjo/1S+BblDpX6bi212/aY51olAE4O2yqaZ2va0Cpkovc7vFMawIOwgkfBp8SjJiIlMwOl95QtvWfeP8KxRkM6POg1zDkimzatvt3iseg8tKXAb4mQDM3Miqj0yrBBoNvy4u24XNL8q7JrP/unsDIO+Xj5YQdHO335DOW/4zvnLwKBgQCD+Ch59LBgCFAw91OzQfNXjBcAx5rlxdhuokLOBx1U0XnlzND0fy+kIsKrrKKlW5byEzShqfX+e6l8b1xQ196qJiMpp30LEzZThKKkNoqB/nkAsG6FqYxaqO8pWPipS4asypkWPiBxLM2+efMiWNSG6qPrrrD5eORPW3Fe9UwtjQKBgFHLxxn0SX34IBTdQrNFmP4oUK2CW7s86S7XvfzPxgbTj1nAhooJBfjp6OuKPdKlvGKueEwJ+w4ZMPPU8cnXQpSLU2Amifz5LU0vwphAd+Lw2rK878ku1PZSHJPddqbKcpr/swOm0frRWt8jY8RKzADpqmVRZebUleuDmJZ5d25H'
      },
      'public_key': {
        'rsa': 'MIIBCgKCAQEAspp/p8TnTRLao+KeBnz1tvlAC3UKAjXOmfyVJw0Lpe29mxbnAq3uUD8Um5t5jHwYX3P8r+FOL83Yt41Y+dkbWM3hQoYA2Et4ypDRUQ3k+ku6kNHkRhRY9nmAhHM9L8C5VlcYQG197mN/+h9sS1EarYV/BawY4VIcEj7z65Xv6Z0YYvEgjLXjlDmUPEg1wZOA2mcx8RcTyhvok5sl7WWO0J00sZSFqTHCFpZFiBY49cCax2+EuXMdlqlcnKZvWtQVQc5JR4T1vccx+TJUM3JeZpKNAVCI9von2CxXaqCaDwN3D9B6V7cgFW8j+PSSQFjri2341/zkK37IkicJAi/vqQIDAQAB'
      }
    }
    kwargs = {
      'headers': {'Content-Type': 'application/json'},
      'data': json.dumps(self.hub_delegate_user)
    }
    res = requests.request(
      url = '%s/users/%s' % (self.domain, self.hub_delegate_user['name']),
      method = 'PUT',
      **kwargs)
    res.raise_for_status()
    return self

  def __exit__(self, *args, **kwargs):
    self.__couchdb.__exit__()

  @property
  def emailer(self):
    return self.__beyond.emailer

  @emailer.setter
  def emailer(self, emailer):
    setattr(self.__beyond, '_Beyond__emailer', emailer)

  @property
  def hub_delegate_user(self):
    return self.__hub_delegate_user

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

  def __init__(self, name, memo):
    self.name = name
    self.storage = '%s/%s-storage' % (name, name)
    self.network = '%s/%s-network' % (name, name)
    self.volume = '%s/%s-volume' % (name, name)
    self.mountpoint = '%s/%s-mountpoint' % (memo.dir, name)
    self.drive = '%s/%s-drive' % (name, name)
    os.mkdir(self.mountpoint)

    self.memo = memo

  def run(self, cli, **kargs):
    return self.memo.run(
      cli.split(' ') if isinstance(cli, str) else cli,
      env = { 'MEMO_USER': self.name }, **kargs)

  def run_json(self, *args, **kwargs):
    if 'env' in kwargs:
      env['MEMO_USER'] = self.name
    else:
      kwargs['env'] = { 'MEMO_USER': self.name }
    return self.memo.run_json(*args, **kwargs)

  def run_split(self, args, **kargs):
    return self.memo.run(args, env = { 'MEMO_USER': self.name }, **kargs)

  def async(self, cli, **kargs):
    import threading
    from functools import partial
    thread = threading.Thread(
      target = partial(self.run, cli = cli, **kargs))
    thread.daemon = True
    thread.start()
    return thread

  def fail(self, cli, **kargs):
    self.memo.run(cli.split(' '), return_code = 1, **kargs)

class SharedLogicCLITests():

  def __init__(self, entity):
    self.__entity = entity

  def random_sequence(self, count = 10):
    from random import SystemRandom
    import string
    return ''.join(SystemRandom().choice(
      string.ascii_lowercase + string.digits) for _ in range(count))

  def run(self):
    entity = self.__entity
    # Creating and deleting entity.
    with Memo() as bob:
      e_name = self.random_sequence()
      bob.run(['user', 'create',  'bob'])
      bob.run(['network', 'create', 'network', '--as', 'bob'])
      bob.run([entity, 'create', e_name, '-N', 'network',
               '--as', 'bob'])
      bob.run([entity, 'export', 'bob/%s' % e_name, '--as', 'bob'])
      bob.run([entity, 'delete', e_name, '--as', 'bob'])

    # Push to the hub.
    with Beyond() as beyond, \
        Memo(beyond = beyond) as bob, Memo(beyond) as alice:
      e_name = self.random_sequence()
      bob.run(['user', 'signup', 'bob', '--email', 'bob@infinit.sh'])
      bob.run(['network', 'create', 'network', '--as', 'bob',
               '--push'])
      bob.run([entity, 'create', e_name, '-N', 'network',
               '--description', 'something', '--as', 'bob', '--push'])
      try:
        bob.run([entity, '--push', '--name', e_name])
        unreachable()
      except Exception as e:
        pass
      alice.run(['user', 'signup', 'alice',
                 '--email', 'a@infinit.sh'])
      alice.run([entity, 'fetch', 'bob/%s' % e_name,
                 '--as', 'alice'])
      e = alice.run_json([entity, 'export', 'bob/%s' % e_name,
                          '--as', 'alice'])
      assertEq(e['description'], 'something')

    # Pull and delete.
    with Beyond() as beyond, Memo(beyond = beyond) as bob:
      e_name = self.random_sequence()
      e_name2 = e_name
      while e_name2 == e_name:
        e_name2 = self.random_sequence()
      bob.run(['user', 'signup', 'bob', '--email', 'b@infinit.sh'])
      bob.run(['network', 'create', '--as', 'bob', 'n', '--push'])
      # Local and Beyond.
      bob.run([entity, 'create', '--as', 'bob', e_name,
               '-N', 'n', '--push'])
      assertEq(len(bob.run_json([entity, 'list', '-s'])), 1)
      bob.run([entity, 'delete', '--as', 'bob', e_name, '--pull'])
      assertEq(len(bob.run_json([entity, 'list', '-s'])), 0)
      bob.run([entity, 'fetch', '--as', 'bob', e_name],
              return_code = 1)
      # Local only.
      bob.run([entity, 'create', '--as', 'bob', e_name2, '-N', 'n'])
      assertEq(len(bob.run_json([entity, 'list', '-s'])), 1)
      bob.run([entity, 'delete', '--as', 'bob', e_name2, '--pull'])
      assertEq(len(bob.run_json([entity, 'list', '-s'])), 0)

class KeyValueStoreInfrastructure():

  def __init__(self, usr, uname = 'bob', kvname = 'kv'):
    self.__usr = usr
    self.__uname = uname
    self.__kvname = kvname
    self.__proc = None
    self.__stub = None
    self.__endpoint = None

  @property
  def usr(self):
    return self.__usr

  @property
  def uname(self):
    return self.__uname

  @property
  def kvname(self):
    return self.__kvname

  @property
  def stub(self):
    return self.__stub

  @property
  def endpoint(self):
    return self.__endpoint

  def __enter__(self):
    self.usr.run(['user', 'create',  self.uname])
    self.usr.run(['silo', 'create', 'filesystem', 's'])
    self.usr.run(['network', 'create', 'n', '-S', 's',
                  '--as', self.uname])
    self.usr.run(['kvs', 'create', self.kvname,
                  '-N', 'n', '--as', self.uname])
    port_file = '%s/port' % self.usr.dir
    self.__proc = self.usr.spawn(
      ['memo', 'kvs', 'run', self.kvname, '--as', self.uname,
       '--allow-root-creation',
       '--grpc', '127.0.0.1:0', '--grpc-port-file', port_file])
    def comm(self):
      self.out, self.err = self.__proc.communicate()
    import threading
    self.__comm = threading.Thread(target=comm, args=[self])
    self.__comm.start()
    while not os.path.exists(port_file):
      time.sleep(0.1)
    with open(port_file, 'r') as f:
      self.__endpoint = '127.0.0.1:{}'.format(f.readline().strip())
    import grpc
    import memo_kvs_pb2_grpc
    channel = grpc.insecure_channel(self.__endpoint)
    self.__stub = memo_kvs_pb2_grpc.KeyValueStoreStub(channel)
    return self

  def __exit__(self, *args, **kwargs):
    if self.__proc:
      self.__proc.terminate()
      self.__comm.join()
      if os.environ.get('OS') != 'windows':
        try:
          # SIGTERM is not caught on windows. Might be wine related.
          assertEq(0, self.__proc.wait())
        except:
          log('STDOUT: %s' % self.out.decode('utf-8'))
          log('STDERR: %s' % self.err.decode('utf-8'))
          out = self.__proc.stdout.read()
          err = self.__proc.stderr.read()
          log('STDOUT: %s' % out.decode('utf-8'))
          log('STDERR: %s' % err.decode('utf-8'))
          raise

  def client(self):
    import grpc
    import memo_kvs_pb2_grpc
    channel = grpc.insecure_channel(self.__endpoint)
    return memo_kvs_pb2_grpc.KeyValueStoreStub(channel)

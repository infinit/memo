import copy
import json
import os
import pipes
import shutil
import subprocess
import sys
import tempfile
import time

import infinit.beyond
import infinit.beyond.bottle
import infinit.beyond.couchdb

from datetime import timedelta

cr = '\r\n' if os.environ.get('EXE_EXT') else '\n'

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

class Infinit(TemporaryDirectory):

  def __init__(self,
               beyond = None,
               infinit_root = None,
               home = None,
               user = None):
    super().__init__(home)
    self.__beyond = beyond
    self.__infinit_root = infinit_root or ''
    self.__user = user
    self.__env = {}

  def __enter__(self):
    super().__enter__()
    return self

  @property
  def version(self):
    return self.run(['infinit-volume', '--version'])[0]

  @property
  def user(self):
    return self.__user

  @property
  def env(self):
    return self.__env

  @property
  def data_home(self):
    return '%s/.local/share/infinit/filesystem' % self.dir

  @property
  def storages_path(self):
    return '%s/storages' % self.data_home

  @property
  def networks_path(self):
    return '%s/networks' % self.data_home

  @property
  def passports_path(self):
    return '%s/passports' % self.data_home

  @property
  def volumes_path(self):
    return '%s/volumes' % self.data_home

  @property
  def drives_path(self):
    return '%s/drives' % self.data_home

  def spawn(self, args, input = None, return_code = 0, env = {}):
    if isinstance(args, str):
      args = args.split(' ')
    if '/' not in args[0]:
      args[0] = 'bin/%s' % args[0]
    build_dir = os.environ.get('BUILD_DIR')
    if build_dir:
      args[0] = '%s/%s' % (build_dir, args[0])
    args[0] += os.environ.get('EXE_EXT', '')
    env_ = {
      'INFINIT_RDV': '',
      'INFINIT_BACKTRACE': '1',
    }
    if self.dir is not None:
      env_['INFINIT_HOME'] = self.dir
    if self.__user is not None:
      env_['INFINIT_USER'] = self.__user
    env_['WINEDEBUG'] = os.environ.get('WINEDEBUG', '-all')
    if 'ELLE_LOG_LEVEL' in os.environ:
      env_['ELLE_LOG_LEVEL'] = os.environ['ELLE_LOG_LEVEL']
    if self.__beyond is not None:
      env_['INFINIT_BEYOND'] = self.__beyond.domain
    env_.update(env)
    env_.update(self.__env)
    if input is not None:
      args.append('-s')
    pretty = '%s %s' % (
      ' '.join('%s=%s' % (k, v) for k, v in env_.items()),
      ' '.join(pipes.quote(arg) for arg in args))
    if input is not None:
      if isinstance(input, list):
        input = '\n'.join(map(json.dumps, input)) + cr
      elif isinstance(input, dict):
        input = json.dumps(input) + cr
      pretty = 'echo %s | %s' % (
        pipes.quote(input.strip()), pretty)
      input = input.encode('utf-8')
    print(pretty)
    process = subprocess.Popen(
      args,
      env = env_,
      stdin =  subprocess.PIPE,
      stdout =  subprocess.PIPE,
      stderr =  subprocess.PIPE,
    )
    self.process = process
    if input is not None:
      process.stdin.write(input)
    process.pretty = pretty
    return process

  def run(self, args, input = None, return_code = 0, env = {}):
    process = self.spawn(args, input, return_code, env)
    out, err = process.communicate(timeout = 600)
    process.wait()
    out = out.decode('utf-8')
    err = err.decode('utf-8')
    if process.returncode != return_code:
      raise Exception(
        'command failed with code %s: %s\nstdout: %s\nstderr: %s' % \
        (process.returncode, process.pretty, out, err))
    self.last_out = out
    self.last_err = err
    return out, err

  def run_json(self, *args, **kwargs):
    out, err = self.run(*args, **kwargs)
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

  def run_script(self,
                 user = None,
                 volume = 'volume',
                 seq = None,
                 peer = None,
                 **kwargs):
    cmd = ['infinit-volume', '--run', volume]
    if user is not None:
      cmd += ['--as', user]
    if peer is not None:
      cmd += ['--peer', peer]
    response = self.run_json(cmd, input = seq or kwargs)
    return response

def assertEq(a, b):
  if a != b:
    raise AssertionError('%r != %r' % (a, b))

def assertNeq(a, b):
  if a == b:
    raise AssertionError('%r == %r' % (a, b))

def assertIn(a, b):
  if a not in b:
    raise AssertionError('%r not in %r' % (a, b))

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

  def __init__(self, beyond_args = {}, disable_authentication = False):
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
      self.__app = infinit.beyond.bottle.Bottle(
        beyond = self.__beyond,
        gcs = self.__gcs
      )
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
    return self.infinit.run(
      cli.split(' '),
      env = { 'INFINIT_USER': self.name }, **kargs)

  def run_json(self, *args, **kwargs):
    if 'env' in kwargs:
      env['INFINIT_USER'] = self.name
    else:
      kwargs['env'] = { 'INFINIT_USER': self.name }
    return self.infinit.run_json(*args, **kwargs)

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

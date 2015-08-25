import couchdb
import couchdb.client
import json
import os.path
import shutil
import subprocess
import tempfile

import infinit.beyond

class CouchDB:

  def __init__(self, port = 0):
    self.beyond = None
    self.__uri = None
    self.__port = port

  def __path(self, p):
    return '%s/couchdb.%s' % (self.__dir, p)

  def __enter__(self):
    self.__dir = tempfile.mkdtemp()
    config = self.__path('ini')
    pid = self.__path('pid')
    stdout = self.__path('stdout')
    stderr = self.__path('stderr')
    with open(config, 'w') as f:
      print('''\
[couchdb]
database_dir = %(root)s/db-data
view_index_dir = %(root)s/db-data
uri_file = %(root)s/couchdb.uri

[httpd]
port = %(port)s

[log]
file = %(root)s/db.log

[query_servers]
python=python -m couchdb
''' % {'root': self.__dir, 'port': self.__port},
            file = f)
    subprocess.check_call(
      ['couchdb', '-a', config,
       '-b', '-p', pid, '-o', stdout, '-e', stderr])
    while not (os.path.exists(self.__path('uri'))):
      import time
      time.sleep(0.1)
    with open(self.__path('uri'), 'r') as f:
      self.__uri = f.read().strip()
    return couchdb.Server(self.__uri)

  def __exit__(self, *args, **kwargs):
    subprocess.check_call(['couchdb', '-d', '-p', self.__path('pid')])
    shutil.rmtree(self.__dir)

  @property
  def uri(self):
    return self.__uri

def getsource(f):
  import inspect
  lines = inspect.getsource(f).split('\n')
  pad = 0
  while pad < len(lines[0]) and lines[0][pad] == ' ':
    pad += 1
  return '\n'.join(line[min(len(line), pad):] for line in lines)

class CouchDBDatastore:

  def __init__(self, db):
    self.__couchdb = db
    def create(name):
      try:
        self.__couchdb.create(name)
      except couchdb.http.PreconditionFailed as e:
        if e.args[0][0] == 'file_exists':
          pass
        else:
          raise
    create('networks')
    create('users')
    import inspect
    try:
      design = self.__couchdb['users']['_design/beyond']
    except couchdb.http.ResourceNotFound:
      design = couchdb.client.Document()
    design.update(
      {
        '_id': '_design/beyond',
        'language': 'python',
        'updates': {
          name: getsource(update)
          for name, update in [('update', self.__user_update)]
        },
        'views': {
          name: {
            'map': getsource(view),
          }
          for name, view in [('per_name', self.__user_per_name)]
        }
      })
    self.__couchdb['users'].save(design)

  ## ---- ##
  ## User ##
  ## ---- ##

  def user_insert(self, user):
    json = dict(user)
    json['_id'] = json['id']
    del json['id']
    try:
      self.__couchdb['users'].save(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.User.Duplicate()

  def user_fetch(self, id = None, name = None):
    assert any(a is not None for a in [id, name])
    try:
      if id is not None:
        json = self.__couchdb['users'][id]
        if name is not None and json['name'] != name:
          return None
      elif name is not None:
        view = self.__couchdb['users'].view('beyond/per_name')[name]
        json = next(iter(view)).value # Really ?
      json = dict(json)
      json['id'] = json['_id']
      del json['_id']
      return json
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.User.NotFound()


  def user_update(self, id, diff = None):
    args = {
      name: json.dumps(value)
      for name, value in diff.items()
      if value is not None
    }
    try:
      self.__couchdb['users'].update_doc(
        'beyond/update',
        id,
        **args
      )
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.User.NotFound()

  def __user_per_name(user):
    yield user['name'], user

  def __user_update(user, req):
    if user is None:
      return [
        None,
        {
          'code': 404,
        }
      ]
    import json
    update = {
      name: json.loads(value)
      for name, value in req['query'].items()
    }
    for id, account in update.get('dropbox_accounts', {}).items():
      user.setdefault('dropbox_accounts', {})[id] = account
    return [user, {'json': json.dumps(user)}]

  ## ------- ##
  ## Network ##
  ## ------- ##

  def network_insert(self, network):
    json = network.json()
    json['_id'] = network.id
    try:
      self.__couchdb['networks'].save(json)
    except couchdb.ResourceConflict:
      raise infinit.beyond.Network.Duplicate()

  def network_fetch(self, owner_id, name):
    try:
      json = self.__couchdb['networks']['%s/%s' % (owner_id, name)]
      json = {
        k: v
        for k, v in json.items() if k in infinit.beyond.Network.fields
      }
      return infinit.beyond.Network(self.beyond, **json)
    except couchdb.http.ResourceNotFound:
      raise infinit.beyond.Network.NotFound()

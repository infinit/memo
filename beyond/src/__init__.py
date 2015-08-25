import base64
import cryptography
import requests

import infinit.beyond.version

class Beyond:

  def __init__(
      self,
      datastore,
      dropbox_app_key,
      dropbox_app_secret,
  ):
    self.__datastore = datastore
    self.__datastore.beyond = self
    self.__dropbox_app_key    = dropbox_app_key
    self.__dropbox_app_secret = dropbox_app_secret

  ## ------- ##
  ## Network ##
  ## ------- ##

  def network_get(self, owner_id, name):
    return self.__datastore.network_fetch(
      owner_id = owner_id, name = name)

  ## ---- ##
  ## User ##
  ## ---- ##

  def user_get(self, id = None, name = None):
    json = self.__datastore.user_fetch(name = name, id = id)
    return User.from_json(self, json)

class User:

  class Duplicate(Exception):
    pass

  class NotFound(Exception):
    pass

  def __init__(self,
               beyond,
               id,
               name = None,
               public_key = None,
               dropbox_accounts = None):
    self.__beyond = beyond
    self.__id = id
    self.__name = name
    self.__public_key = public_key
    self.__dropbox_accounts = dropbox_accounts or {}
    self.__dropbox_accounts_original = dict(self.dropbox_accounts)

  @classmethod
  def from_json(self, beyond, json):
    return User(beyond,
                id = json['id'],
                name = json['name'],
                public_key = json['public_key'],
                dropbox_accounts = json.get('dropbox_accounts'))

  def to_json(self, private = False):
    res = {
      'id': self.id,
      'name': self.name,
      'public_key': self.public_key,
    }
    if private and self.dropbox_accounts is not None:
      res['dropbox_accounts'] = self.dropbox_accounts
    return res

  def create(self):
    assert all(m is not None
               for m in [self.id, self.name, self.public_key])
    self.__beyond._Beyond__datastore.user_insert(
      self.to_json(private = True))

  def save(self):
    diff = {}
    for id, account in self.dropbox_accounts.items():
      if self.__dropbox_accounts_original.get(id) != account:
        diff.setdefault('dropbox_accounts', {})[id] = account
    self.__beyond._Beyond__datastore.user_update(self.id, diff)
    self.__dropbox_accounts_original = dict(self.__dropbox_accounts)

  @property
  def id(self):
    return self.__id

  @property
  def name(self):
    return self.__name

  @property
  def public_key(self):
    return self.__public_key

  @property
  def dropbox_accounts(self):
    return self.__dropbox_accounts


class Entity(type):

  def __new__(self, name, superclasses, content,
              insert = None, fields = []):
    content['fields'] = fields
    # Init
    def __init__(self, beyond, **kwargs):
      self.__beyond = beyond
      for f in fields:
        setattr(self, '_%s__%s' % (name, f), kwargs.pop(f, None))
      if kwargs:
        raise TypeError('__init__() got an unexpected keyword argument \'%s\'' % next(iter(kwargs)))
    content['__init__'] = __init__
    # JSON
    def json(self):
      assert all(getattr(self, m) is not None for m in fields)
      return {
        m: getattr(self, m) for m in fields
      }
    content['json'] = json
    # Create
    if insert:
      def create(self):
        assert all(getattr(self, m) is not None for m in fields)
        getattr(self.__beyond._Beyond__datastore, insert)(self)
      content['create'] = create
    # Properties
    def make_getter(f):
      return lambda self: getattr(self, '_%s__%s' % (name, f))
    for f in fields:
      content[f] = property(make_getter(f))
    # Exceptions
    content['Duplicate'] = type(
      'Duplicate',
      (Exception,),
      {'__qualname__': '%s.Duplicate' % name},
    )
    content['NotFound'] = type(
      'NotFound',
      (Exception,),
      {'__qualname__': '%s.NotFound' % name},
    )
    return type.__new__(self, name, superclasses, content)

  def __init__(self, name, superclasses, content,
               insert = None,
               fields = []):
    for f in fields:
      content[f] = property(lambda self: getattr(self, '_%s__%s' % (name, f)))
    type.__init__(self, name, superclasses, content)

class Network(metaclass = Entity,
              insert = 'network_insert',
              fields = ['name', 'owner', 'overlay']):

  @property
  def id(self):
    der = base64.b64decode(self.owner['rsa'])
    owner_id = base64.b64encode(cryptography.hash(der))[0:8]
    return '%s/%s' % (owner_id.decode('latin-1'), self.name)

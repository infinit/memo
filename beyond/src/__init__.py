import base64
import cryptography
import requests

import infinit.beyond.version

from copy import deepcopy
from itertools import chain

class Beyond:

  def __init__(
      self,
      datastore,
      dropbox_app_key,
      dropbox_app_secret,
      google_app_key,
      google_app_secret,
  ):
    self.__datastore = datastore
    self.__datastore.beyond = self
    self.__dropbox_app_key    = dropbox_app_key
    self.__dropbox_app_secret = dropbox_app_secret
    self.__google_app_key    = google_app_key
    self.__google_app_secret = google_app_secret

  @property
  def dropbox_app_key(self):
    return self.__dropbox_app_key

  @property
  def dropbox_app_secret(self):
    return self.__dropbox_app_secret

  @property
  def google_app_key(self):
    return self.__google_app_key

  @property
  def google_app_secret(self):
    return self.__google_app_secret

  ## ------- ##
  ## Network ##
  ## ------- ##

  def network_get(self, owner, name):
    return self.__datastore.network_fetch(
      owner = owner, name = name)

  def network_delete(self, owner, name):
    return self.__datastore.network_delete(owner = owner, name = name)

  def network_volumes_get(self, network):
    return self.__datastore.networks_volumes_fetch(networks = [network])

  ## ---- ##
  ## User ##
  ## ---- ##

  def user_get(self, name):
    json = self.__datastore.user_fetch(name = name)
    return User.from_json(self, json)

  def user_delete(self, name):
    return self.__datastore.user_delete(name = name)

  def user_networks_get(self, user):
    return self.__datastore.user_networks_fetch(user = user)

  def user_volumes_get(self, user):
    # XXX: This requires two requests as we cannot combine results across
    # databases.
    networks = self.__datastore.user_networks_fetch(user = user)
    return self.__datastore.networks_volumes_fetch(networks = networks)

  ## ------ ##
  ## Volume ##
  ## ------ ##

  def volume_get(self, owner, name):
    return self.__datastore.volume_fetch(
      owner = owner, name = name)

  def volume_delete(self, owner, name):
    return self.__datastore.volume_delete(
      owner = owner, name = name)

class User:

  class Duplicate(Exception):
    pass

  class NotFound(Exception):
    pass

  def __init__(self,
               beyond,
               name = None,
               public_key = None,
               dropbox_accounts = None,
               google_accounts = None,
  ):
    self.__beyond = beyond
    self.__id = id
    self.__name = name
    self.__public_key = public_key
    self.__dropbox_accounts = dropbox_accounts or {}
    self.__dropbox_accounts_original = deepcopy(self.dropbox_accounts)
    self.__google_accounts = google_accounts or {}
    self.__google_accounts_original = deepcopy(self.google_accounts)

  @classmethod
  def from_json(self, beyond, json):
    return User(beyond,
                name = json['name'],
                public_key = json['public_key'],
                dropbox_accounts = json.get('dropbox_accounts', []),
                google_accounts = json.get('google_accounts', []),
    )

  def json(self, private = False):
    res = {
      'id': self.id,
      'name': self.name,
      'public_key': self.public_key,
    }
    if private and self.dropbox_accounts is not None:
      res['dropbox_accounts'] = self.dropbox_accounts
    if private and self.google_accounts is not None:
      res['google_accounts'] = self.google_accounts
    return res

  def create(self):
    self.__beyond._Beyond__datastore.user_insert(self)

  def save(self):
    diff = {}
    for id, account in self.dropbox_accounts.items():
      if self.__dropbox_accounts_original.get(id) != account:
        diff.setdefault('dropbox_accounts', {})[id] = account
    for id, account in self.google_accounts.items():
      if self.__google_accounts_original.get(id) != account:
        diff.setdefault('google_accounts', {})[id] = account
    self.__beyond._Beyond__datastore.user_update(self.name, diff)
    self.__dropbox_accounts_original = dict(self.__dropbox_accounts)
    self.__google_accounts_original = dict(self.__google_accounts)

  @property
  def id(self):
    der = base64.b64decode(self.public_key['rsa'].encode('latin-1'))
    id = base64.urlsafe_b64encode(cryptography.hash(der))[0:8]
    return id.decode('latin-1')

  @property
  def name(self):
    return self.__name

  @property
  def public_key(self):
    return self.__public_key

  @property
  def dropbox_accounts(self):
    return self.__dropbox_accounts

  @property
  def google_accounts(self):
    return self.__google_accounts

class MissingField(Exception):

  def __init__(self, field):
    self.__field = field

  def __str__(self):
    return 'missing field: %r' % self.field

  @property
  def field(self):
    return self.__field


class Entity(type):

  def __new__(self, name, superclasses, content,
              insert = None,
              update = None,
              fields = {}):
    self_type = None
    content['fields'] = fields
    # Init
    def __init__(self, beyond, **kwargs):
      self.__beyond = beyond
      for f, default in fields.items():
        v = kwargs.pop(f, None)
        if v is None:
          v = default
        setattr(self, '_%s__%s' % (name, f), v)
        if isinstance(v, dict):
          setattr(self, '_%s__%s_original' % (name, f), deepcopy(v))
      if kwargs:
        raise TypeError(
          '__init__() got an unexpected keyword argument %r' %
          next(iter(kwargs)))
    content['__init__'] = __init__
    # JSON
    def json(self):
      assert all(getattr(self, m) is not None for m in fields)
      return {
        m: getattr(self, m) for m in fields
      }
    content['json'] = json
    def from_json(beyond, json):
      missing = next((m for m in fields if m not in json), None)
      if missing is not None:
        raise Exception(
          'missing mandatory JSON key for '
          '%s: %s' % (self.__name__, missing))
      json = {
        k: v
        for k, v in json.items() if k in fields
      }
      return self_type(beyond, **json)
    content['from_json'] = from_json
    # Create
    if insert:
      def create(self):
        missing = next((f for f, d in fields.items()
                        if getattr(self, f) is None and d is None),
                       None)
        if missing is not None:
          raise MissingField(f)
        getattr(self.__beyond._Beyond__datastore, insert)(self)
      content['create'] = create
    # Save
    if update:
      def save(self):
        diff = {}
        for field in fields:
          import sys
          v = getattr(self, field)
          if isinstance(v, dict):
            original_field = \
              '_%s__%s_original' % (self.__class__.__name__, field)
            original = getattr(self, original_field)
            for k, v in v.items():
              if original.get(k) != v:
                diff.setdefault(field, {})[k] = v
            setattr(self, original_field, deepcopy(v))
        updater = getattr(self.__beyond._Beyond__datastore, update)
        import sys
        sys.stdout = sys.stderr
        updater(self.id, diff)
      content['save'] = save
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
    self_type = type.__new__(self, name, superclasses, content)
    return self_type

  def __init__(self, name, superclasses, content,
               insert = None,
               update = None,
               fields = []):
    for f in fields:
      content[f] = property(lambda self: getattr(self, '_%s__%s' % (name, f)))
    type.__init__(self, name, superclasses, content)


def fields(*args, **kwargs):
  return dict(chain(((k, None) for k in args), kwargs.items()))


class Network(metaclass = Entity,
              insert = 'network_insert',
              update = 'network_update',
              fields = fields('name', 'owner', 'consensus', 'overlay',
                              passports = {},
                              endpoints = {})):

  @property
  def id(self):
    return self.name

class Volume(metaclass = Entity,
             insert = 'volume_insert',
             fields = fields('name', 'network')):

  @property
  def id(self):
    return self.name

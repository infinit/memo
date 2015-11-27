import base64
import cryptography
import requests

import infinit.beyond.version

from infinit.beyond import validation, emailer

from copy import deepcopy
from itertools import chain

# Email templates.
templates = {
  'Drive/Joined': {
    'noop': 'Drive/Joined',
    'swu': 'tem_RFSDrp7nzCbsBRSUts7MsU',
  },
  'Drive/Invitation': {
    'noop': 'Drive/Invitation',
    'swu': 'tem_UwwStKnWCWNU5VP4HBS7Xj',
  },
  'User/Welcome': {
    'noop': 'User/Welcome',
    'swu': 'tem_Jsd948JkLqhBQs3fgGZSsS',
  }
}

class Beyond:

  def __init__(
      self,
      datastore,
      dropbox_app_key,
      dropbox_app_secret,
      google_app_key,
      google_app_secret,
      sendwithus_api_key = None,
      validate_email_address = True,
  ):
    self.__datastore = datastore
    self.__datastore.beyond = self
    self.__dropbox_app_key    = dropbox_app_key
    self.__dropbox_app_secret = dropbox_app_secret
    self.__google_app_key    = google_app_key
    self.__google_app_secret = google_app_secret
    self.__validate_email_address = validate_email_address
    if sendwithus_api_key is not None:
      self.__emailer = emailer.SendWithUs(sendwithus_api_key)
    else:
      self.__emailer = emailer.NoOp()

  @property
  def now(self):
    return self.__now()

  def __now(self):
    import datetime
    return datetime.datetime.utcnow()

  @property
  def dropbox_app_key(self):
    return self.__dropbox_app_key

  @property
  def emailer(self):
    return self.__emailer

  def template(self, name):
    if isinstance(self.__emailer, emailer.SendWithUs):
      return templates[name]['swu']
    else:
      return templates[name]['noop']

  @property
  def dropbox_app_secret(self):
    return self.__dropbox_app_secret

  @property
  def google_app_key(self):
    return self.__google_app_key

  @property
  def google_app_secret(self):
    return self.__google_app_secret

  @property
  def validate_email_address(self):
    return self.__validate_email_address

  ## ------- ##
  ## Pairing ##
  ## ------- ##

  def pairing_information_get(self, owner, password_hash):
    json = self.__datastore.pairing_fetch(owner)
    pairing = PairingInformation.from_json(self, json)
    import sys
    print(file = sys.stderr)
    if password_hash != pairing.password_hash:
      raise ValueError("password_hash")
    self.pairing_information_delete(owner)
    if self.now > pairing.expiration:
      raise exceptions.NoLongerAvailable("%s pairing information" % owner)
    return pairing

  def pairing_information_delete(self, owner):
    self.__datastore.pairing_delete(owner)

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

  def network_stats_get(self, network):
    return self.__datastore.network_stats_fetch(network = network)

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

  def user_drives_get(self, user):
    return self.__datastore.user_drives_fetch(user)

  ## ------ ##
  ## Volume ##
  ## ------ ##

  def volume_get(self, owner, name):
    return self.__datastore.volume_fetch(
      owner = owner, name = name)

  def volume_delete(self, owner, name):
    return self.__datastore.volume_delete(
      owner = owner, name = name)

  ## ----- ##
  ## Drive ##
  ## ----- ##

  def drive_get(self, owner, name):
    return self.__datastore.drive_fetch(
      owner = owner, name = name)

  def drive_delete(self, owner, name):
    return self.__datastore.drive_delete(
        owner = owner, name = name)

class User:
  fields = {
    'mandatory': [
      ('name', validation.Name('user', 'name')),
      ('email', validation.Email('user')),
      ('public_key', None),
    ],
    'optional': [
      ('dropbox_accounts', None),
      ('fullname', None),
      ('google_accounts', None),
      ('password_hash', None),
      ('private_key', None),
    ]
  }
  class Duplicate(Exception):
    pass

  class NotFound(Exception):
    pass

  def __init__(self,
               beyond,
               name = None,
               email = None,
               fullname = None,
               public_key = None,
               password_hash = None,
               private_key = None,
               dropbox_accounts = None,
               google_accounts = None,
  ):
    self.__beyond = beyond
    self.__id = id
    self.__name = name
    self.__email = email
    self.__fullname = fullname
    self.__public_key = public_key
    self.__password_hash = password_hash
    self.__private_key = private_key
    self.__dropbox_accounts = dropbox_accounts or {}
    self.__dropbox_accounts_original = deepcopy(self.dropbox_accounts)
    self.__google_accounts = google_accounts or {}
    self.__google_accounts_original = deepcopy(self.google_accounts)

  @classmethod
  def from_json(self, beyond, json, check_integrity = False):
    if check_integrity:
      for (key, validator) in User.fields['mandatory']:
        if key == 'email' and not beyond.validate_email_address:
          continue
        if key not in json:
          raise exceptions.MissingField('user', key)
        validator and validator(json[key])
      for (key, validator) in User.fields['optional']:
        if key in json and validator is not None:
          validator(json[key])
    return User(beyond,
                name = json['name'],
                public_key = json['public_key'],
                email = json.get('email', None),
                fullname = json.get('fullname', None),
                password_hash = json.get('password_hash', None),
                private_key = json.get('private_key', None),
                dropbox_accounts = json.get('dropbox_accounts', []),
                google_accounts = json.get('google_accounts', []),
    )

  def json(self, private = False):
    res = {
      'name': self.name,
      'public_key': self.public_key,
    }
    if private:
      if self.email is not None:
        res['email'] = self.email
      if self.fullname is not None:
        res['fullname'] = self.fullname
      if self.dropbox_accounts is not None:
        res['dropbox_accounts'] = self.dropbox_accounts
      if self.google_accounts is not None:
        res['google_accounts'] = self.google_accounts
      if self.private_key is not None:
        res['private_key'] = self.private_key
      if self.private_key is not None:
        res['password_hash'] = self.password_hash
    return res

  def create(self):
    self.__beyond._Beyond__datastore.user_insert(self)
    if self.email is not None:
      self.__beyond.emailer.send_one(
        template = self.__beyond.template("User/Welcome"),
        recipient_email = self.email,
        recipient_name = self.name,
        variables = {
          'email': self.email,
          'name': self.name,
        }
    )

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
  def email(self):
    return self.__email

  @property
  def fullname(self):
    return self.__fullname

  @property
  def public_key(self):
    return self.__public_key

  @property
  def private_key(self):
    return self.__private_key

  @property
  def password_hash(self):
    return self.__password_hash

  @property
  def dropbox_accounts(self):
    return self.__dropbox_accounts

  @property
  def google_accounts(self):
    return self.__google_accounts

  def __eq__(self, other):
    if self.name != other.name or self.public_key != other.public_key:
      return False
    return True

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
          raise exceptions.MissingField(type(self).__name__.lower(), missing)
        getattr(self.__beyond._Beyond__datastore, insert)(self)
      content['create'] = create
    # Save
    if update:
      def save(self):
        diff = {}
        for field in fields:
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

class PairingInformation(metaclass = Entity,
                         insert = 'pairing_insert',
                         fields = fields('name', 'password_hash', 'data', 'expiration')):

  def create(self):
    assert False
    if self.expiration < self.now + datetime.timedelta(seconds = 60 * 5):
      raise Exception("Lifespan too long")
    self.create()

  @property
  def id(self):
    return self.name

class Network(metaclass = Entity,
              insert = 'network_insert',
              update = 'network_update',
              fields = fields('name', 'owner', 'consensus', 'overlay',
                              passports = {},
                              endpoints = {},
                              storages = {})):

  @property
  def id(self):
    return self.name

  def __eq__(self, other):
    if self.name != other.name or self.owner != other.owner or \
        self.consensus != other.consensus or self.overlay != other.overlay:
      return False
    return True

  class Statistics(metaclass = Entity,
                   fields = fields('usage', 'capacity')):
    pass

class Passport(metaclass = Entity,
               fields = fields('user', 'network', 'signature')):
  pass

class Volume(metaclass = Entity,
             insert = 'volume_insert',
             fields = fields('name', 'network')):

  @property
  def id(self):
    return self.name

  def __eq__(self, other):
    if self.name != other.name or self.network != other.network:
      return False
    return True

class Drive(metaclass = Entity,
            insert = 'drive_insert',
            update = 'drive_update',
            fields = fields('name', 'owner', 'network', 'volume', 'description',
                            users = {})):

  @property
  def id(self):
    return self.name

  def __eq__(self, other):
    if self.name != other.name or self.network != other.network or \
        self.volume != other.volume or self.description != other.description:
      return False
    return True

  class Invitation(metaclass = Entity,
                   fields = fields('permissions', 'status', 'create_home')):
    statuses = ['pending', 'ok']

    # XXX: Shouldn't work.
    def __init__(self, beyond, **json):
      super().__init__(beyond, **json)
      if self['status'] not in statuses:
        raise exceptions.InvalidFormat('invitation', status)

    def save(self, beyond, drive, owner, invitee, invitation):
      confirm = not invitation
      if invitation:
        if invitee.name in drive.users and drive.users[invitee.name] == 'pending':
          return
        elif drive.users.get(invitee.name, None) == 'ok':
          raise Exception("ALREADY CONFIRMED")
      if confirm:
        if invitee.name not in drive.users:
          raise Exception("NOT INVITED")
        elif drive.users.get(invitee.name, None) == 'ok':
          return
      drive.users[invitee.name] = self.json()
      drive.save()
      variables = {
        'owner': { x: getattr(owner, x) for x in ['name', 'email'] },
        'invitee': { x: getattr(invitee, x) for x in ['name', 'email'] },
        'drive': { x: getattr(drive, x) for x in ['name', 'description'] },
      }
      if invitation and invitee.email is not None:
        beyond.emailer.send_one(
          template = beyond.template("Drive/Invitation"),
          recipient_email = invitee.email,
          recipient_name = invitee.name,
          variables = variables
        )
      if confirm and owner.email is not None:
        beyond.emailer.send_one(
          template = beyond.template("Drive/Joined"),
          recipient_email = owner.email,
          recipient_name = owner.name,
          variables = variables
        )

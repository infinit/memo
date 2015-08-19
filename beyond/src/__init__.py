import bottle
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
    self.__dropbox_app_key    = dropbox_app_key
    self.__dropbox_app_secret = dropbox_app_secret

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


class Bottle(bottle.Bottle):

  def __init__(self, beyond):
    super().__init__()
    self.__beyond = beyond
    self.install(bottle.CertificationPlugin())
    self.route('/')(self.root)
    self.route('/users/<id>', method = 'GET')(self.user_get)
    self.route('/users/<id>/dropbox-accounts', method = 'GET')(self.user_dropbox_accounts_get)
    self.route('/users/<id>', method = 'PUT')(self.user_put)
    self.route('/users/<id>/dropbox-oauth')(self.oauth_dropbox_get)
    self.route('/oauth/dropbox')(self.oauth_dropbox)
    self.route('/debug', method = 'GET')(self.debug)

  def authenticate(self, user):
    pass

  def root(self):
    return {
      'version': infinit.beyond.version.version,
    }

  def user_put(self, id):
    try:
      json = bottle.request.json
      json['id'] = id
      user = User.from_json(self.__beyond, json)
      user.create()
    except User.Duplicate:
      bottle.response.status = 409
      return {
        'error': 'user/conflict',
        'reason': 'user %r already exists' % id,
        'id': id,
      }

  def user_get(self, id):
    return self.__beyond.user_get(id = id).to_json()

  def user_dropbox_accounts_get(self, id):
    try:
      user = self.__beyond.user_get(id = id)
      self.authenticate(user)
      return {
        'dropbox_accounts': list(user.dropbox_accounts.values()),
      }
    except User.NotFound:
      bottle.response.status = 404
      return {
        'error': 'user/not_found',
        'reason': 'user %r does not exist' % id,
        'id': id,
      }

  def host(self):
    return '%s://%s' % bottle.request.urlparts[0:2]

  def oauth_dropbox_get(self, id):
    params = {
      'client_id': self.beyond._Beyond__dropbox_app_key,
      'response_type': 'code',
      'redirect_uri': '%s/oauth/dropbox' % self.host(),
      'state': id,
    }
    req = requests.Request(
      'GET',
      'https://www.dropbox.com/1/oauth2/authorize',
      params = params,
    )
    url = req.prepare().url
    bottle.redirect(url)

  def oauth_dropbox(self):
    code = bottle.request.query['code']
    uid = bottle.request.query['state']
    query = {
      'code': code,
      'grant_type': 'authorization_code',
      'client_id': self.beyond._Beyond__dropbox_app_key,
      'client_secret': self.beyond._Beyond__dropbox_app_secret,
      'redirect_uri': '%s/oauth/dropbox' % self.host(),
    }
    response = requests.post('https://api.dropbox.com/1/oauth2/token',
                             params = query)
    contents = response.json()
    access_token = contents['access_token']
    id = contents['uid']
    r = requests.get('https://api.dropbox.com/1/account/info',
                     params = {'access_token': access_token})
    account_info = r.json()
    user = User(self.__beyond, id = uid)
    user.dropbox_accounts[account_info['uid']] = {
      'uid': account_info['uid'],
      'display_name': account_info['display_name'],
      'token': access_token,
    }
    try:
      user.save()
      return {
        'uid': account_info['uid'],
        'display_name': account_info['display_name'],
      }
    except User.NotFound:
      bottle.response.status = 404
      return {
        'error': 'user/not_found',
        'reason': 'user %r does not exist' % uid,
        'id': uid,
      }

  def debug(self):
    if hasattr(bottle.request, 'certificate') and \
       bottle.request.certificate in [
         'antony.mechin@infinit.io',
         'baptiste.fradin@infinit.io',
         'christopher.crone@infinit.io',
         'gaetan.rochel@infinit.io',
         'julien.quintard@infinit.io',
         'matthieu.nottale@infinit.io',
         'patrick.perlmutter@infinit.io',
         'quentin.hocquet@infinit.io',
       ]:
      return True
    else:
      return super().debug()

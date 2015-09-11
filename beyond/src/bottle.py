import bottle
import cryptography

from infinit.beyond import *

class Bottle(bottle.Bottle):

  __oauth_services = {
    'dropbox': {
      'form_url': 'https://www.dropbox.com/1/oauth2/authorize',
      'exchange_url': 'https://api.dropbox.com/1/oauth2/token',
      'info_url': 'https://api.dropbox.com/1/account/info',
      'info': lambda info: {
        'uid': str(info['uid']),
        'display_name': info['display_name'],
      },
    },
    'google': {
      'form_url': 'https://accounts.google.com/o/oauth2/auth',
      'exchange_url': 'https://www.googleapis.com/oauth2/v3/token',
      'params': {
        'scope': 'https://www.googleapis.com/auth/drive.file',
        'access_type': 'offline',
      },
      'info_url': 'https://www.googleapis.com/drive/v2/about',
      'info': lambda info: {
        'uid': info['user']['emailAddress'],
        'display_name': info['name'],
      },
    },
  }

  def __init__(self, beyond):
    super().__init__()
    self.__beyond = beyond
    self.install(bottle.CertificationPlugin())
    self.route('/')(self.root)
    # OAuth
    for s in Bottle.__oauth_services:
      self.route('/oauth/%s' % s)(getattr(self, 'oauth_%s' % s))
      self.route('/users/<username>/%s-oauth' % s)(
        getattr(self, 'oauth_%s_get' % s))
      self.route('/users/<username>/credentials/%s' %s,
                 method = 'GET')(
        getattr(self, 'user_%s_credentials_get' % s))
    # User
    self.route('/users/<name>', method = 'GET')(self.user_get)
    self.route('/users/<name>', method = 'PUT')(self.user_put)
    # Network
    self.route('/networks/<owner>/<name>',
               method = 'GET')(self.network_get)
    self.route('/networks/<owner>/<name>',
               method = 'PUT')(self.network_put)
    self.route('/networks/<owner>/<name>/passports/<invitee>',
               method = 'GET')(self.network_passport_get)
    self.route('/networks/<owner>/<name>/passports/<invitee>',
               method = 'PUT')(self.network_passport_put)
    # Volume
    self.route('/volumes/<owner>/<name>',
               method = 'GET')(self.volume_get)
    self.route('/volumes/<owner>/<name>',
               method = 'PUT')(self.volume_put)

  def authenticate(self, user):
    pass

  def root(self):
    return {
      'version': infinit.beyond.version.version,
    }

  def host(self):
    return '%s://%s' % bottle.request.urlparts[0:2]

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

  ## ---- ##
  ## User ##
  ## ---- ##

  def user_put(self, name):
    try:
      json = bottle.request.json
      user = User.from_json(self.__beyond, json)
      user.create()
      bottle.response.status = 201
      return {}
    except User.Duplicate:
      if user.public_key == self.__beyond.user_get(user.name).public_key:
          bottle.response.status = 200
      else:
        bottle.response.status = 409
        return {
          'error': 'user/conflict',
          'reason': 'user %r already exists' % name,
          'id': name,
        }

  def user_get(self, name):
    try:
      return self.__beyond.user_get(name = name).json()
    except User.NotFound:
      return self.__user_not_found(name)

  def __user_not_found(self, name):
    bottle.response.status = 404
    return {
      'error': 'user/not_found',
      'reason': 'user %r does not exist' % name,
      'name': name,
    }

  ## ------- ##
  ## Network ##
  ## ------- ##

  def __not_found(self, type, name):
    bottle.response.status = 404
    return {
      'error': '%s/not_found' % type,
      'reason': '%s %r does not exist' % (type, name),
      'name': name,
    }


  def network_get(self, owner, name):
    try:
      return self.__beyond.network_get(
        owner = owner, name = name).json()
    except Network.NotFound:
      return self.__not_found('network', '%s/%s' % (owner, name))

  def network_put(self, owner, name):
    try:
      json = bottle.request.json
      network = Network(self.__beyond, **json)
      network.create()
      bottle.response.status = 201
      return {}
    except Network.Duplicate:
      bottle.response.status = 409
      return {
        'error': 'network/conflict',
        'reason': 'network %r already exists' % name,
      }

  def network_passport_get(self, owner, name, invitee):
    user = self.__beyond.user_get(name = owner)
    self.authenticate(user)
    network = self.__beyond.network_get(
      owner = owner, name = name)
    passport = network.passports.get(invitee)
    if passport is None:
      return self.__not_found(
        'passport', '%s/%s/%s' % (owner, name, invitee))
    else:
      return passport

  def network_passport_put(self, owner, name, invitee):
    try:
      user = self.__beyond.user_get(name = owner)
      self.authenticate(user)
      network = Network(self.__beyond, owner = owner, name = name)
      network.passports[invitee] = bottle.request.json
      network.save()
      bottle.response.status = 201
      return {}
    except Network.NotFound:
      return self.__not_found('network', '%s/%s' % (owner, name))

  ## ------ ##
  ## Volume ##
  ## ------ ##

  def volume_get(self, owner, name):
    return self.__beyond.volume_get(
      owner = owner, name = name).json()

  def volume_put(self, owner, name):
    try:
      json = bottle.request.json
      volume = Volume(self.__beyond, **json)
      volume.create()
    except Volume.Duplicate:
      bottle.response.status = 409
      return {
        'error': 'volume/conflict',
        'reason': 'volume %r already exists' % name,
      }

for name, conf in Bottle._Bottle__oauth_services.items():
  def oauth_get(self, username, name = name, conf = conf):
    beyond = self._Bottle__beyond
    params = {
      'client_id': getattr(beyond, '%s_app_key' % name),
      'response_type': 'code',
      'redirect_uri': '%s/oauth/%s' % (self.host(), name),
      'state': username,
    }
    params.update(conf.get('params', {}))
    req = requests.Request('GET', conf['form_url'], params = params)
    url = req.prepare().url
    bottle.redirect(url)
  oauth_get.__name__ = 'oauth_%s_get' % name
  setattr(Bottle, oauth_get.__name__, oauth_get)
  def oauth(self, name = name, conf = conf):
    beyond = self._Bottle__beyond
    code = bottle.request.query['code']
    username = bottle.request.query['state']
    query = {
      'code': code,
      'grant_type': 'authorization_code',
      'client_id':
        getattr(self._Bottle__beyond, '%s_app_key' % name),
      'client_secret':
        getattr(self._Bottle__beyond, '%s_app_secret' % name),
      'redirect_uri': '%s/oauth/%s' % (self.host(), name),
    }
    response = requests.post(conf['exchange_url'], params = query)
    if response.status_code // 100 != 2:
      bottle.response.status = response.status_code
      return response.text
    contents = response.json()
    access_token = contents['access_token']
    user = User(beyond, name = username)
    response = requests.get(
      conf['info_url'], params = {'access_token': access_token})
    if response.status_code // 100 != 2:
      bottle.response.status = response.status_code
      return response.text
    info = conf['info'](response.json())
    getattr(user, '%s_accounts' % name)[info['uid']] = \
      dict(info, token = access_token)
    try:
      user.save()
      return info
    except User.NotFound:
      return self._Bottle__user_not_found(username)
  oauth.__name__ = 'oauth_%s' % name
  setattr(Bottle, oauth.__name__, oauth)
  def user_credentials_get(self, username, name = name):
    beyond = self._Bottle__beyond
    try:
      user = beyond.user_get(name = username)
      self.authenticate(user)
      return {
        'credentials':
          list(getattr(user, '%s_accounts' % name).values()),
      }
    except User.NotFound:
      return self._Bottle__user_not_found(username)
  user_credentials_get.__name__ = 'user_%s_credentials_get' % name
  setattr(Bottle, user_credentials_get.__name__, user_credentials_get)

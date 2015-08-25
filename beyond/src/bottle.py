import bottle
import cryptography

from infinit.beyond import *

class Bottle(bottle.Bottle):

  def __init__(self, beyond):
    super().__init__()
    self.__beyond = beyond
    self.install(bottle.CertificationPlugin())
    self.route('/')(self.root)
    self.route('/oauth/dropbox')(self.oauth_dropbox)
    # User
    self.route('/users/<id>', method = 'GET')(self.user_get)
    self.route('/users/<id>/dropbox-accounts', method = 'GET')(self.user_dropbox_accounts_get)
    self.route('/users/<id>', method = 'PUT')(self.user_put)
    self.route('/users/<id>/dropbox-oauth')(self.oauth_dropbox_get)
    # Network
    self.route('/networks/<owner_id>/<name>', method = 'GET')(self.network_get)
    self.route('/networks/<owner_id>/<name>', method = 'PUT')(self.network_put)

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

  ## ------- ##
  ## Network ##
  ## ------- ##

  def network_get(self, owner_id, name):
    return self.__beyond.network_get(
      owner_id = owner_id, name = name).json()

  def network_put(self, owner_id, name):
    try:
      json = bottle.request.json
      network = Network(self.__beyond, **json)
      network.create()
    except Network.Duplicate:
      bottle.response.status = 409
      return {
        'error': 'network/conflict',
        'reason': 'network %r already exists' % name,
      }

  ## ------- ##
  ## Dropbox ##
  ## ------- ##

  def oauth_dropbox_get(self, id):
    params = {
      'client_id': self.__beyond._Beyond__dropbox_app_key,
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
      'client_id': self.__beyond._Beyond__dropbox_app_key,
      'client_secret': self.__beyond._Beyond__dropbox_app_secret,
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

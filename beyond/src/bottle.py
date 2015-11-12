import bottle
import cryptography
import datetime
import hashlib
import json
import time

import Crypto.Hash.SHA
import Crypto.Hash.SHA256
import Crypto.PublicKey
import Crypto.Signature.PKCS1_v1_5

from copy import deepcopy
from requests import Request, Session

from infinit.beyond import *
from infinit.beyond.gcs import GCS

## -------- ##
## Response ##
## -------- ##

class Response(Exception):

  def __init__(self, status = 200, body = None):
    self.__status = status
    self.__body = body

  @property
  def status(self):
    return self.__status

  @property
  def body(self):
    return self.__body

class ResponsePlugin(object):

  '''Bottle plugin to generate throw a response.'''

  name = 'meta.short-circuit'
  api  = 2

  def apply(self, f, route):
    def wrapper(*args, **kwargs):
      try:
        return f(*args, **kwargs)
      except exceptions.MissingField as exception:
        bottle.response.status = 400
        return {
          'error': '%s/missing_field/%s' % (
            exception.field.type, exception.field.name),
          'reason': 'missing field %s' % exception.field.name
        }
      except exceptions.InvalidFormat as exception:
        bottle.response.status = 422
        return {
          'error': '%s/invalid_format/%s' % (
            exception.field.type, exception.field.name),
          'reason': '%s has an invalid format' % exception.field.name
        }
      except Response as response:
        bottle.response.status = response.status
        return response.body
    return wrapper

## ------ ##
## Bottle ##
## ------ ##

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

  def __init__(
      self,
      beyond,
      gcs = None,
  ):
    super().__init__()
    self.__beyond = beyond
    self.install(bottle.CertificationPlugin())
    self.install(ResponsePlugin())
    self.route('/')(self.root)
    # GCS
    self.__gcs = gcs
    # OAuth
    for s in Bottle.__oauth_services:
      self.route('/oauth/%s' % s)(getattr(self, 'oauth_%s' % s))
      self.route('/users/<username>/%s-oauth' % s)(
        getattr(self, 'oauth_%s_get' % s))
      self.route('/users/<username>/credentials/%s' % s,
                 method = 'GET')(
        getattr(self, 'user_%s_credentials_get' % s))
    self.route('/users/<username>/credentials/google/refresh',
               method = 'GET')(
    getattr(self, 'user_credentials_google_refresh'))
    # User
    self.route('/users/<name>', method = 'GET')(self.user_get)
    self.route('/users/<name>', method = 'PUT')(self.user_put)
    self.route('/users/<name>', method = 'DELETE')(self.user_delete)
    self.route('/users/<name>/avatar', method = 'GET')(
      self.user_avatar_get)
    self.route('/users/<name>/avatar', method = 'PUT')(
      self.user_avatar_put)
    self.route('/users/<name>/avatar', method = 'DELETE')(
      self.user_avatar_delete)
    self.route('/users/<name>/networks',
               method = 'GET')(self.user_networks_get)
    self.route('/users/<name>/passports',
               method = 'GET')(self.user_passports_get)
    self.route('/users/<name>/volumes',
               method = 'GET')(self.user_volumes_get)
    self.route('/users/<name>/login', method = 'POST')(self.login)
    # Network
    self.route('/networks/<owner>/<name>',
               method = 'GET')(self.network_get)
    self.route('/networks/<owner>/<name>',
               method = 'PUT')(self.network_put)
    self.route('/networks/<owner>/<name>',
               method = 'DELETE')(self.network_delete)
    self.route('/networks/<owner>/<name>/passports',
               method = 'GET')(self.network_passports_get)
    self.route('/networks/<owner>/<name>/passports/<invitee>',
               method = 'GET')(self.network_passport_get)
    self.route('/networks/<owner>/<name>/passports/<invitee>',
               method = 'PUT')(self.network_passport_put)
    self.route('/networks/<owner>/<name>/passports/<invitee>',
               method = 'DELETE')(self.network_passport_delete)
    self.route('/networks/<owner>/<name>/endpoints',
               method = 'GET')(self.network_endpoints_get)
    self.route('/networks/<owner>/<name>/endpoints/<user>/<node_id>',
               method = 'PUT')(self.network_endpoint_put)
    self.route('/networks/<owner>/<name>/endpoints/<user>/<node_id>',
               method = 'DELETE')(self.network_endpoint_delete)
    self.route('/networks/<owner>/<name>/users',
               method = 'GET')(self.network_users_get)
    self.route('/networks/<owner>/<name>/volumes',
               method = 'GET')(self.network_volumes_get)
    # Volume
    self.route('/volumes/<owner>/<network>/<name>',
               method = 'GET')(self.volume_get)
    self.route('/volumes/<owner>/<network>/<name>',
               method = 'PUT')(self.volume_put)
    self.route('/volumes/<owner>/<network>/<name>',
               method = 'DELETE')(self.volume_delete)

    # Drive
    self.route('/drives/<owner>/<name>',
               method = 'GET')(self.drive_get)
    self.route('/drives/<owner>/<name>',
               method = 'PUT')(self.drive_put)
    self.route('/drives/<owner>/<name>',
               method = 'DELETE')(self.drive_delete)
    self.route('/drives/<owner>/<name>/invite/<user>',
               method = 'PUT')(self.drive_invite_put)

  def __not_found(self, type, name):
    return Response(404, {
      'error': '%s/not_found' % type,
      'reason': '%s %r does not exist' % (type, name),
      'name': name,
    })

  def __user_not_found(self, name):
    return self.__not_found('user', name)

  def __ensure_names_match(self, type, name, entity):
    if 'name' not in entity:
      raise exceptions.MissingField(type, 'name')
    if entity['name'] != name:
      raise Response(409, {
        'error': '%s/names_do_not_match' % type,
        'reason': 'entity name and route must match',
      })

  def authenticate(self, user):
    remote_signature_raw = bottle.request.headers.get('infinit-signature')
    if remote_signature_raw is None:
      raise Response(401, 'Missing signature header')
    request_time = bottle.request.headers.get('infinit-time')
    if request_time is None:
      raise Response(400, 'Missing time header')
    if abs(time.time() - int(request_time)) > 300: # UTC
      raise Response(401, 'Time too far away: got %s, current %s' % \
                     (request_time, time.time()))
    rawk = user.public_key['rsa']
    der = base64.b64decode(rawk.encode('latin-1'))
    k = Crypto.PublicKey.RSA.importKey(der)
    to_sign = bottle.request.method + ';' + bottle.request.path[1:] + ';'
    to_sign += base64.b64encode(
      hashlib.sha256(bottle.request.body.getvalue()).digest()).decode('utf-8') + ";"
    to_sign += request_time
    local_hash = Crypto.Hash.SHA256.new(to_sign.encode('utf-8'))
    remote_signature_crypted = base64.b64decode(remote_signature_raw.encode('utf-8'))
    verifier = Crypto.Signature.PKCS1_v1_5.new(k)
    if not verifier.verify(local_hash, remote_signature_crypted):
      raise Response(403, 'Authentication error')
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

  def user_from_name(self, name, throws = True):
    try:
      return self.__beyond.user_get(name)
    except User.NotFound as e:
      if throws:
        raise self.__user_not_found(name)
      return None

  def user_put(self, name):
    try:
      json = bottle.request.json
      self.__ensure_names_match('user', name, json)
      user = User.from_json(self.__beyond, json)
      user.create()
      raise Response(201, {})
    except User.Duplicate:
      if user.public_key == self.user_from_name(user.name).public_key:
        return {}
      else:
        raise Response(409, {
          'error': 'user/conflict',
          'reason': 'user %r already exists' % name,
          'id': name,
        })

  def user_get(self, name):
    return self.user_from_name(name = name).json()

  def user_delete(self, name):
    user = self.user_from_name(name = name)
    self.authenticate(user)
    self.__beyond.user_delete(name)

  def user_avatar_put(self, name):
    return self.__user_avatar_manipulate(
      name, self.__cloud_image_upload)

  def user_avatar_get(self, name):
    return self.__user_avatar_manipulate(
      name, self.__cloud_image_get)

  def user_avatar_delete(self, name):
    return self.__user_avatar_manipulate(
      name, self.__cloud_image_delete)

  def __user_avatar_manipulate(self, name, f):
    return f('users', '%s/avatar' % name)

  def user_networks_get(self, name):
    user = self.user_from_name(name = name)
    self.authenticate(user)
    networks = self.__beyond.user_networks_get(user = user)
    return {'networks': list(map(lambda n: n.json(), networks))}

  def user_passports_get(self, name):
    user = self.user_from_name(name = name)
    self.authenticate(user)
    networks = self.__beyond.user_networks_get(user = user)
    return {'passports': list(map(lambda n: n.passports[name], networks))}

  def user_volumes_get(self, name):
    user = self.user_from_name(name = name)
    self.authenticate(user)
    volumes = self.__beyond.user_volumes_get(user = user)
    return {'volumes': list(map(lambda v: v.json(), volumes))}

  def login(self, name):
    json = bottle.request.json
    if 'password_hash' not in json:
      raise Response(400, 'Missing password_hash')
    try:
      return self.__beyond.user_login(name, json['password_hash']).json(
        private = True)
    except exceptions.Mismatch as e:
      raise Response(403,
                     {
                       'error': 'users/invalid_password',
                       'reason': e.args[0]
                     })
    except exceptions.NotOptIn as e:
      raise Response(404,
                     {
                       'error': 'users/...',
                       'reason': e.args[0],
                     })
    except User.NotFound as e:
      raise self.__user_not_found(name)

  ## ------- ##
  ## Network ##
  ## ------- ##

  def network_from_name(self, owner, name, throws = True):
    try:
      return self.__beyond.network_get(owner = owner, name = name)
    except Network.NotFound:
      if throws:
        raise self.__not_found('network', '%s/%s' % (owner, name))
      return None

  def network_get(self, owner, name):
    return self.network_from_name(owner = owner, name = name).json()

  def network_put(self, owner, name):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    json = bottle.request.json
    network = Network(self.__beyond, **json)
    try:
      network.create()
      raise Response(201, {})
    except Network.Duplicate:
      raise Response(409, {
        'error': 'network/conflict',
        'reason': 'network %r already exists' % name,
      })

  def network_passports_get(self, owner, name):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    network = self.network_from_name(owner = owner, name = name)
    return network.passports

  def network_passport_get(self, owner, name, invitee):
    user = self.user_from_name(name = owner)
    network = self.network_from_name(owner = owner, name = name)
    passport = network.passports.get(invitee)
    if passport is None:
      raise self.__not_found(
        'passport', '%s/%s/%s' % (owner, name, invitee))
    else:
      return passport

  def network_passport_put(self, owner, name, invitee):
    user = self.user_from_name(name = owner)
    try:
      self.authenticate(user)
    except Exception:
      u_invitee = self.user_from_name(name = invitee)
      self.authenticate(u_invitee)
    network = self.network_from_name(owner = owner, name = name)
    network.passports[invitee] = bottle.request.json
    network.save()
    raise Response(201, {})

  def network_passport_delete(self, owner, name, invitee):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    network = self.network_from_name(owner = owner, name = name)
    network.passports[invitee] = None
    network.save()
    return {}

  def network_endpoints_get(self, owner, name):
    network = self.network_from_name(owner = owner, name = name)
    return network.endpoints

  def network_endpoint_put(self, owner, name, user, node_id):
    user = self.user_from_name(name = user)
    self.authenticate(user)
    network = self.network_from_name(owner = owner, name = name)
    json = bottle.request.json
    # FIXME
    # if 'port' not in json or 'addresses' not in json
    network.endpoints.setdefault(user.name, {})[node_id] = json
    network.save()
    raise Response(201, {}) # FIXME: 200 if existed

  def network_endpoint_delete(self, owner, name, user, node_id):
    user = self.user_from_name(name = user)
    self.authenticate(user)
    network = self.network_from_name(owner = owner, name = name)
    network.endpoints.setdefault(user.name, {})[node_id] = None
    network.save()
    return {}

  def network_delete(self, owner, name):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    self.network_from_name(owner = owner, name = name)
    self.__beyond.network_delete(owner, name)

  def network_users_get(self, owner, name):
    network = self.network_from_name(owner = owner, name = name)
    res = [owner]
    res.extend(network.passports.keys())
    return {
      'users': res,
    }

  def network_volumes_get(self, owner, name):
    network = self.network_from_name(owner = owner, name = name)
    volumes = self.__beyond.network_volumes_get(network = network)
    return {'volumes': list(map(lambda v: v.json(), volumes))}

  ## ------ ##
  ## Volume ##
  ## ------ ##

  def volume_from_name(self, owner, network, name, throws = True):
    try:
      return self.__beyond.volume_get(owner = owner, network = network, name = name)
    except Volume.NotFound:
      raise self.__not_found('volume', '%s/%s/%s' % (owner, network, name))

  def volume_get(self, owner, network, name):
    return self.volume_from_name(owner = owner, network = network, name = name).json()

  def volume_put(self, owner, network, name):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    network = self.network_from_name(owner = owner, name = network)
    try:
      json = bottle.request.json
      volume = Volume(self.__beyond, **json)
      volume.create()
      raise Response(201, {})
    except Volume.Duplicate:
      raise Response(409, {
        'error': 'volume/conflict',
        'reason': 'volume %r already exists' % name,
      })

  def volume_delete(self, owner, network, name):
    import sys
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    self.volume_from_name(owner = owner, network = network, name = name)
    self.__beyond.volume_delete(owner = owner, network = network, name = name)

  ## ----- ##
  ## DRIVE ##
  ## ----- ##

  def drive_from_name(self, owner, name, throws = True):
    try:
      return self.__beyond.drive_get(owner = owner, name = name)
    except Drive.NotFound:
      raise self.__not_found('drive', '%s/%s' % (owner, name))

  def drive_put(self, owner, name):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    try:
      json = bottle.request.json
      drive = Drive(self.__beyond, **json)
      drive.create()
    except Drive.Duplicate:
      raise Response(409, {
        'error': 'drive/conflict',
        'reason': 'drive %s already exists' % name,
      })
    raise Response(201, {})

  def drive_delete(self, owner, name):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    self.drive_from_name(owner = owner, name = name)
    self.__beyond.drive_delete(owner = owner, name = name)

  def drive_get(self, owner, name):
    return self.drive_from_name(owner, name).json()

  def drive_invite_put(self, owner, name, user):
    try:
      self.user_from_name(name = user)
    except:
      raise Response(404, {
        'error': 'user/not_found',
        'reason': 'User %s not found' % user
      })

    owner_user = self.user_from_name(name = owner)
    self.authenticate(owner_user)
    drive = None
    try:
      drive = self.drive_from_name(owner = owner, name = name)
    except:
      raise Response(404, {
        'error': 'drive/not_found',
        'reason': 'Drive %s not found' % drive
      })
    json = bottle.request.json
    drive.users[user] = json
    drive.save()
    raise Response(201, {}) # FIXME: 200 if existed


  ## --- ##
  ## GCS ##
  ## --- ##

  def __check_gcs(self):
    if self.__gcs is None:
      raise Response(501, {
        'reason': 'GCS support not enabled',
      })

  def __cloud_image_upload(self, bucket, name):
    self.__check_gcs()
    t = bottle.request.headers['Content-Type']
    l = bottle.request.headers['Content-Length']
    if t not in ['image/gif', 'image/jpeg', 'image/png']:
      bottle.response.status = 415
      return {
        'reason': 'invalid image format: %s' % t,
        'mime-type': t,
      }
    url = self.__gcs.upload_url(
      bucket,
      name,
      content_type = t,
      content_length = l,
      expiration = datetime.timedelta(minutes = 3),
    )
    bottle.response.status = 307
    bottle.response.headers['Location'] = url

  def __cloud_image_get(self, bucket, name):
    self.__check_gcs()
    url = self.__gcs.download_url(
      bucket,
      name,
      expiration = datetime.timedelta(minutes = 3),
    )
    bottle.response.status = 307
    bottle.response.headers['Location'] = url

  def __cloud_image_delete(self, bucket, name):
    self.__check_gcs()
    url = self.__gcs.delete_url(
      bucket,
      name,
      expiration = datetime.timedelta(minutes = 3),
    )
    bottle.response.status = 307
    bottle.response.headers['Location'] = url
    # self.gcs.delete(
    #   bucket,
    #   name)
    # bottle.response.status = 204

for name, conf in Bottle._Bottle__oauth_services.items():
  def oauth_get(self, username, name = name, conf = conf):
    beyond = self._Bottle__beyond
    params = {
      'client_id': getattr(beyond, '%s_app_key' % name),
      'response_type': 'code',
      'redirect_uri': '%s/oauth/%s' % (self.host(), name),
      'state': username,
    }
    if name == 'google':
      params['approval_prompt'] = 'force'
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
    if 'refresh_token' in contents:
      refresh_token = contents['refresh_token']
    else:
      refresh_token = ''
    user = User(beyond, name = username)
    response = requests.get(
      conf['info_url'], params = {'access_token': access_token})
    if response.status_code // 100 != 2:
      bottle.response.status = response.status_code
      return response.text
    info = conf['info'](response.json())
    getattr(user, '%s_accounts' % name)[info['uid']] = \
      dict(info, token = access_token, refresh_token = refresh_token)
    try:
      user.save()
      return info
    except User.NotFound:
      raise self.__user_not_found(username)
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
      raise self.__user_not_found(username)
  user_credentials_get.__name__ = 'user_%s_credentials_get' % name
  setattr(Bottle, user_credentials_get.__name__, user_credentials_get)

# This function first checks if the google account `token` field is
# valid.  If not it asks google for another access_token and updates
# the client, else it return to the client the access_token of the
# database.
def user_credentials_google_refresh(self, username):
  try:
    beyond = self._Bottle__beyond
    user = beyond.user_get(name = username)
    refresh_token = bottle.request.query.refresh_token
    for id, account in user.google_accounts.items():
      google_account = user.google_accounts[id]
      # https://developers.google.com/identity/protocols/OAuth2InstalledApp
      # The associate google account.
      if google_account['refresh_token'] == refresh_token:
        google_url = "https://www.googleapis.com/oauth2/v3/token"
        # Get a new token and update the db and the client
        query = {
          'client_id': beyond.google_app_key,
          'client_secret': beyond.google_app_secret,
          'refresh_token': google_account['refresh_token'],
          'grant_type': 'refresh_token',
        }
        res = requests.post(google_url, params=query)
        if res.status_code != 200:
          raise HTTPError(status=400)
        else:
          token = res.json()['access_token']
          user.google_accounts[id]['token'] = token
          user.save()
          return token
  except User.NotFound:
    raise self.__user_not_found(username)

setattr(Bottle,
        user_credentials_google_refresh.__name__,
        user_credentials_google_refresh)

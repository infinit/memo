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
from functools import partial

from infinit.beyond import *
from infinit.beyond.response import Response
from infinit.beyond.gcs import GCS
from infinit.beyond.plugins.jsongo import Plugin as JsongoPlugin
from infinit.beyond.plugins.max_size import Plugin as MaxSizePlugin
from infinit.beyond.plugins.response import Plugin as ResponsePlugin

bottle.BaseRequest.MEMFILE_MAX = 2.5 * 1000 * 1000

def str2bool(v):
  return v.lower() in ("yes", "true", "t", "1")

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
    'gcs': {
      'form_url': 'https://accounts.google.com/o/oauth2/auth',
      'exchange_url': 'https://www.googleapis.com/oauth2/v3/token',
      'params': {
        'scope': 'https://www.googleapis.com/auth/devstorage.read_write https://www.googleapis.com/auth/userinfo.email https://www.googleapis.com/auth/userinfo.profile',
        'access_type': 'offline',
      },
      'info_url': 'https://www.googleapis.com/oauth2/v2/userinfo',
      'info': lambda info: {
        'uid': info['email'],
        'display_name': info['name'],
      },
    },
  }

  def __init__(
      self,
      beyond,
      gcs = None,
      production = True,
  ):
    super().__init__(catchall = not production)
    self.__beyond = beyond
    self.__ban_list = ['demo', 'root', 'admin']
    self.install(bottle.CertificationPlugin())
    self.install(ResponsePlugin())
    self.install(JsongoPlugin())
    self.install(MaxSizePlugin(bottle.BaseRequest.MEMFILE_MAX))
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

    # Email confirmation
    self.route('/users/<name>/confirm_email', method = 'POST')(
      self.user_confirm_email)
    self.route('/users/<name>/send_confirmation_email', method = 'POST')(
      self.user_send_confirmation_email)
    self.route('/users/<name>/send_confirmation_email/<email>', method = 'POST')(
      self.user_send_confirmation_email)

    # Avatar
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
    self.route('/users/<name>/drives',
               method = 'GET')(self.user_drives_get)
    self.route('/users/<name>/login', method = 'POST')(self.login)
    self.route('/users/<name>/pairing',
               method = 'PUT')(self.store_pairing_information)
    self.route('/users/<name>/pairing',
               method = 'GET')(self.get_pairing_information)
    self.route('/users/<name>/pairing/status',
               method = 'GET')(self.get_pairing_status)
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
    self.route('/networks/<owner>/<name>/volumes',
               method = 'GET')(self.network_volumes_get)
    self.route('/networks/<owner>/<name>/stat',
               method = 'GET')(self.network_stats_get)
    self.route('/networks/<owner>/<name>/stat/<user>/<node_id>',
               method = 'PUT')(self.network_stats_put)
    # Volume
    self.route('/volumes/<owner>/<name>',
               method = 'GET')(self.volume_get)
    self.route('/volumes/<owner>/<name>',
               method = 'PUT')(self.volume_put)
    self.route('/volumes/<owner>/<name>',
               method = 'DELETE')(self.volume_delete)
    # Drive
    self.route('/drives/<owner>/<name>',
               method = 'GET')(self.drive_get)
    self.route('/drives/<owner>/<name>',
               method = 'PUT')(self.drive_put)
    self.route('/drives/<owner>/<name>',
               method = 'DELETE')(self.drive_delete)
    self.route('/drives/<owner>/<name>/invitations/<user>',
               method = 'PUT')(self.drive_invitation_put)
    self.route('/drives/<owner>/<name>/invitations',
               method = 'PUT')(self.drive_invitations_put)
    self.route('/drives/<owner>/<name>/invitations/<user>',
               method = 'DELETE')(self.drive_invitation_delete)
    self.route('/drives/<owner>/<name>/icon', method = 'GET')(
      self.drive_icon_get)
    self.route('/drives/<owner>/<name>/icon', method = 'PUT')(
      self.drive_icon_put)
    self.route('/drives/<owner>/<name>/icon', method = 'DELETE')(
      self.drive_icon_delete)
    # Crash reports
    self.route('/crash/report', method = 'PUT')(self.crash_report_put)

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
    if user.name in self.__ban_list:
      raise Response(403, {
        'error': 'user/forbidden',
        'reason': 'this user cannot perform any operation'
      })
    remote_signature_raw = bottle.request.headers.get('infinit-signature')
    if remote_signature_raw is None:
      raise Response(401, {
        'error': 'user/unauthorized',
        'reason': 'authentication required',
      })
    request_time = bottle.request.headers.get('infinit-time')
    if request_time is None:
      raise Response(400, {
        'error': 'user/unauthorized',
        'reason': 'missing time header',
      })
    delay = abs(time.time() - int(request_time)) # UTC
    if delay > 300:
      raise Response(401, {
        'error': 'user/unauthorized',
        'reason': 'too late: request was issued %ss ago' % delay,
      })
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
    try:
      if not verifier.verify(local_hash, remote_signature_crypted):
        raise Response(403, {
          'error': 'user/unauthorized',
          'reason': 'invalid authentication',
        })
    # XXX: Sometimes, verify fails if the keys used differ, raising:
    # > ValueError('Plaintext to large')
    # This happens ONLY if the keys are different so we can consider it as an
    # AuthenticationError.
    # To reproduce, remove this try block and run 'tests/auth'.
    except ValueError as e:
      if e.args[0] == 'Plaintext too large':
        raise Response(403, {
          'error': 'user/unauthorized',
          'reason': 'invalid authentication',
        })
      raise
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

  ## -------------- ##
  ## Key encryption ##
  ## -------------- ##
  def encrypt_key(self, key):
    return key

  def decrypt_key(self, key):
    return key

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

  def users_from_email(self, email, throws = True):
    try:
      return self.__beyond.users_by_email(email)
    except User.NotFound as e:
      if throws:
        raise self.__user_not_found(email)
      return []

  def user_put(self, name):
    try:
      json = bottle.request.json
      self.__ensure_names_match('user', name, json)
      if 'private_key' in json:
        json['private_key'] = self.encrypt_key(json['private_key'])
      user = User.from_json(self.__beyond, json, check_integrity = True)
      user.create()
      raise Response(201, {})
    except User.Duplicate:
      if user == self.user_from_name(name = name):
        # When update is available, uncomment next line:
        # self.authenticate(user)
        return {}
      else:
        raise Response(409, {
          'error': 'user/conflict',
          'reason': 'user %r already exists' % name,
          'id': name,
        })

  def user_confirm_email(self, name):
    user = self.user_from_name(name = name)
    json = bottle.request.json
    confirmation_code = json.get('confirmation_code')
    email = json.get('email', user.email)
    if user.emails.get(email) == True:
      raise Response(410, {
        'error': 'user/email/alread_confirmed',
        'reason': '\'%s\' as already been confirmed' % email
      })
    if email is None or confirmation_code is None or \
       user.emails.get(email) != confirmation_code:
      raise Response(404, {
        'error': 'user/email/XXX',
        'reason': 'confirmation codes don\'t match.' # XXX
      })
    user.emails[email] = True
    user.save()
    errors = []
    # Look for plain invites.
    drives = self.__beyond.user_drives_get(name = email)
    if len(drives):
      errors = self.__beyond.process_invitations(user, email, drives)
    raise Response(200, {
      'errors': errors
    })

  def user_send_confirmation_email(self, name, email = None):
    user = self.user_from_name(name = name)
    json = bottle.request.json
    email = email or json.get('email')
    try:
      user.send_confirmation_email(email)
    except Exception as e:
      raise Response(404, {
        'error': 'user/email/unknown',
        'reason': 'unknown email address'
      })
    raise Response(200, {})

  def user_get(self, name):
    return self.user_from_name(name = name).json()

  def user_delete(self, name):
    user = self.user_from_name(name = name)
    self.authenticate(user)
    self.__beyond.user_delete(name)

  def user_avatar_put(self, name):
    user = self.user_from_name(name = name)
    self.authenticate(user)
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
    return {'passports': list(
      map(lambda n: n.passports[name],
          filter(lambda x: name in x.passports,
                 networks)))}

  def user_volumes_get(self, name):
    user = self.user_from_name(name = name)
    self.authenticate(user)
    volumes = self.__beyond.user_volumes_get(user = user)
    return {'volumes': list(map(lambda v: v.json(), volumes))}

  def user_drives_get(self, name):
    user = self.user_from_name(name = name)
    self.authenticate(user)
    drives = self.__beyond.user_drives_get(name = user.name)
    return {'drives': list(map(lambda d: d.json(), drives))}

  def login(self, name):
    json = bottle.request.json
    if 'password_hash' not in json:
      raise Response(401, {
        'error': 'user/unauthorized',
        'reason': 'missing password hash',
      })
    try:
      user = self.__beyond.user_get(name)
      if user.password_hash is None:
        raise Response(404,
                     {
                       'error': 'users/not_in', # Better name.
                       'reason': 'User doesn\'t use the hub to login',
                       'name': name
                     })
      if json['password_hash'] != user.password_hash:
        raise Response(403,
                       {
                         'error': 'users/invalid_password',
                         'reason': 'password do not match',
                       })
      user = user.json(private = True)
      if 'private_key' in user:
        user['private_key'] = self.decrypt_key(user['private_key'])
      return user
    except User.NotFound as e:
      raise self.__user_not_found(name)

  def store_pairing_information(self, name):
    user = self.user_from_name(name = name)
    self.authenticate(user)
    json = bottle.request.json
    # XXX: This should be in __init__.py but metaclasses make it difficult to
    # extend with specific behaviour.
    json['expiration'] = self.__beyond.now + datetime.timedelta(
      seconds = json.get('lifespan', 5) * 60)
    if 'lifespan' in json:
      del json['lifespan']
    json['name'] = user.name
    pairing = PairingInformation(self.__beyond, **json)
    pairing.create()
    raise Response(201, {})

  def get_pairing_information(self, name):
    user = self.user_from_name(name = name)
    paring_passphrase_hash = \
      bottle.request.headers.get('infinit-pairing-passphrase-hash', '')
    try:
      pairing = \
        self.__beyond.pairing_information_get(user.name, paring_passphrase_hash)
    except PairingInformation.NotFound:
      raise self.__not_found('pairing_information', user.name)
    except ValueError as e:
      if e.args[0] == 'passphrase_hash':
        raise Response(403, {
           'error': 'pairing/invalid_passphrase',
           'reason': 'passphrases do not match',
        })
      raise e
    except exceptions.NoLongerAvailable as e:
      raise Response(410, {
        'error': 'pairing/gone',
        'reason': e.args[0],
      })
    return pairing.json()

  def get_pairing_status(self, name):
    user = self.user_from_name(name = name)
    self.authenticate(user)
    try:
      if self.__beyond.pairing_information_status(user.name):
        return {}
    except PairingInformation.NotFound:
      raise self.__not_found('pairing_information', user.name)
    except exceptions.NoLongerAvailable as e:
      raise Response(410, {
        'error': 'pairing/gone',
        'reason': e.args[0],
      })
    raise self.__not_found('pairing_information', user.name)

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
      network.overwrite()
      raise Response(200, {})

  def network_passports_get(self, owner, name):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    network = self.network_from_name(owner = owner, name = name)
    return network.passports

  def network_passport_get(self, owner, name, invitee):
    user = self.user_from_name(name = owner)
    try:
      self.authenticate(user)
    except Exception:
      self.authenticate(self.user_from_name(name = invitee))
    network = self.network_from_name(owner = owner, name = name)
    passport = network.passports.get(invitee)
    if passport is None:
      raise self.__not_found(
        'passport', '%s/%s: %s' % (owner, name, invitee))
    else:
      return passport

  def network_passport_put(self, owner, name, invitee):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    network = self.network_from_name(owner = owner, name = name)
    json = bottle.request.json
    passport = Passport(self.__beyond, **json)
    network.passports[invitee] = passport.json()
    limit = self.__beyond.limits.get('networks', {}).get('passports', None)
    if limit and len(network.passports) > limit:
      raise Response(402, {
        'error': 'account/payment_required',
        'reason': 'You can not store more than %s passports' % limit
      })
    network.save()
    raise Response(201, {})

  def network_passport_delete(self, owner, name, invitee):
    user = self.user_from_name(name = owner)
    try:
      self.authenticate(user)
    except Exception:
      self.authenticate(self.user_from_name(name = invitee))
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

  def network_volumes_get(self, owner, name):
    network = self.network_from_name(owner = owner, name = name)
    volumes = self.__beyond.network_volumes_get(network = network)
    return {'volumes': list(map(lambda v: v.json(), volumes))}

  def network_stats_get(self, owner, name):
    try:
      network = self.__beyond.network_get(owner = owner, name = name)
    except Network.NotFound:
      raise self.__not_found('network', '%s/%s' % (owner, name))
    ret = self.__beyond.network_stats_get(network)
    return ret

  def network_stats_put(self, owner, name, user, node_id):
    try:
      user = self.__beyond.user_get(name = user)
      self.authenticate(user)
      network = self.__beyond.network_get(owner = owner, name = name)
      json = bottle.request.json
      # XXX: Fix that...
      if not 'capacity' in json or json['capacity'] == None:
        json['capacity'] = 0
      stats = Network.Statistics(self.__beyond, **json)
      network.storages.setdefault(user.name, {})[node_id] = stats.json()
      network.save()
      raise Response(201, {}) # FIXME: 200 if existed
    except Network.NotFound:
      raise self.__not_found('network', '%s/%s' % (owner, name))

  ## ------ ##
  ## Volume ##
  ## ------ ##

  def volume_from_name(self, owner, name, throws = True):
    try:
      return self.__beyond.volume_get(owner = owner, name = name)
    except Volume.NotFound:
      raise self.__not_found('volume', '%s/%s' % (owner, name))

  def volume_get(self, owner, name):
    return self.volume_from_name(owner = owner, name = name).json()

  def volume_put(self, owner, name):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    # network = self.network_from_name(owner = owner, name = network)
    try:
      json = bottle.request.json
      volume = Volume(self.__beyond, **json)
      volume.create()
      raise Response(201, {})
    except Volume.Duplicate:
      if volume == self.volume_from_name(owner = owner, name = name):
        return {}
      raise Response(409, {
        'error': 'volume/conflict',
        'reason': 'volume %r already exists' % name,
      })

  def volume_delete(self, owner, name):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    self.volume_from_name(owner = owner, name = name)
    self.__beyond.volume_delete(owner = owner, name = name)

  ## ----- ##
  ## Drive ##
  ## ----- ##

  def __drive_integrity(self, drive, passport = None):
    drive = drive.json()
    try:
      # Make sure network exists.
      network_owner =  drive['network'].split('/')[0]
      network_name =  drive['network'].split('/')[1]
      network = self.network_from_name(owner = network_owner,
                                       name = network_name)
    except IndexError:
      raise Response(400, {
        'error': 'drive/invalid',
        'reason': 'invalid network name',
      })
    try:
      # Make sure volume exists.
      self.volume_from_name(owner = drive['volume'].split('/')[0],
                            name = drive['volume'].split('/')[1])
    except IndexError:
      raise Response(400, {
        'error': 'volume/invalid',
        'reason': 'invalid volume name',
      })
    if passport is not None:
      self.network_passport_get(owner = network_owner,
                                name = network_name,
                                invitee = passport)

  def drive_from_name(self, owner, name, throws = True):
    try:
      return self.__beyond.drive_get(owner = owner, name = name)
    except Drive.NotFound:
      if throws:
        raise self.__not_found('drive', '%s/%s' % (owner, name))

  def drive_put(self, owner, name):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    try:
      json = bottle.request.json
      drive = Drive(self.__beyond, **json)
      self.__drive_integrity(drive)
      drive.create()
    except Drive.Duplicate:
      if drive == self.drive_from_name(owner = owner, name = name):
        return {}
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

  def __drive_invitation_put(self, drive, owner, invitee, invitation,
                             **body):
    key = invitee.name if isinstance(invitee, User) else invitee
    i = Drive.Invitation(self.__beyond, **body)
    try:
      return i.save(beyond = self._Bottle__beyond,
                    drive = drive,
                    owner = owner,
                    invitee = invitee,
                    invitation = invitation)
    except Drive.Invitation.AlreadyConfirmed:
      raise Response(409, {
        'error': 'drive/invitation/conflict',
        'reason': '%s\'s invitation already confirmed' % key,
      })
    except Drive.Invitation.NotInvited:
      raise Response(404, {
        'error': 'drive/invitation/not_found',
        'reason': 'you have not been invited to this drive',
      })

  # XXX: Do something better.
  def __user_from_name_or_email(self, user, throws = True):
    if self.__beyond.is_email(user):
      users = self.users_from_email(email = user, throws = throws)
      if len(users) == 0:
        return None
      return users[0] # Handle more than one user.
    else:
      return self.user_from_name(name = user, throws = throws)

  def drive_invitation_put(self, owner, name, user):
    as_owner = None
    owner = self.user_from_name(name = owner)
    invitee = self.__user_from_name_or_email(user, throws = False)
    if self.__beyond.is_email(user):
      if invitee is None:
        as_owner = True
        invitee = user
    if invitee is None:
      raise self.__not_found('user', user)
    try:
      self.authenticate(owner)
      as_owner = True
    except Exception as e:
      if as_owner is not None:
        raise e
      as_owner = False
      self.authenticate(invitee)
    drive = self.drive_from_name(owner = owner.name, name = name)
    self.__drive_integrity(
      drive,
      passport = invitee.name if isinstance(invitee, User) else None)
    json = bottle.request.json
    if self.__drive_invitation_put(
      drive = drive,
      owner = owner,
      invitation = as_owner,
      invitee = invitee,
      **json):
      raise Response(201, {}) # FIXME: 200 if existed
    else:
      raise Response(200, {}) # FIXME: 200 if existed

  def drive_invitations_put(self, owner, name):
    owner = self.user_from_name(name = owner)
    self.authenticate(owner)
    drive = self.drive_from_name(owner = owner.name, name = name)
    json = bottle.request.json
    # Use 2 separate loops so you don't put anything before checks are done.
    invitees = {}
    for name, value in json.items():
      if name == owner.name:
        continue
      invitee = self.__user_from_name_or_email(name, throws = False)
      if invitee is None:
        raise self.__not_found('user', name)
      invitees[name] = invitee
      if isinstance(invitees[name], User):
        self.__drive_integrity(drive, passport = name)
    for name, value in json.items():
      if name == owner.name:
        continue
      self.__drive_invitation_put(
        drive = drive,
        owner = owner,
        invitee = invitees[name],
        invitation = True,
        **value)
    raise Response(201, {}) # FIXME: 200 if existed

  def drive_invitation_delete(self, owner, name, user):
    owner = self.user_from_name(name = owner)
    drive = self.drive_from_name(owner = owner.name, name = name)
    if self.__beyond.is_email(user) and user in drive.users:
      self.authenticate(owner)
      drive.users[user] = None
    elif user in drive.users:
      try:
        self.authenticate(owner)
      except:
        user = self.user_from_name(name = owner)
        self.authenticate(user)
      drive.users[user.name] = None
    else:
      raise self.__not_found('invitation', user)
    drive.save()
    raise Response(200, {})

  def __qualified_name(self, owner, name):
    return '%s/%s' % (owner, name)

  def drive_icon_put(self, owner, name):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    return self.__drive_icon_manipulate(
      self.__qualified_name(owner, name), self.__cloud_image_upload)

  def drive_icon_get(self, owner, name, redirect = False):
    return self.__drive_icon_manipulate(
      self.__qualified_name(owner, name),
      self.__cloud_image_get)

  def drive_icon_delete(self, owner, name):
    user = self.user_from_name(name = owner)
    self.authenticate(user)
    return self.__drive_icon_manipulate(
       self.__qualified_name(owner, name), self.__cloud_image_delete)

  def __drive_icon_manipulate(self, name, f):
    return f('users', '%s/icon' % name)

  ## ------------ ##
  ## Crash Report ##
  ## ------------ ##
  def crash_report_put(self):
    self.__beyond.crash_report_send(bottle.request.body)
    return {}

  ## --- ##
  ## GCS ##
  ## --- ##

  def __check_gcs(self):
    if self.__gcs is None:
      raise Response(501, {
        'error': 'GCS/not_implemented',
        'reason': 'GCS support not enabled',
      })

  @staticmethod
  def content_type(image):
    from PIL import JpegImagePlugin, PngImagePlugin, GifImagePlugin, TiffImagePlugin
    if isinstance(image, JpegImagePlugin.JpegImageFile):
      return 'image/jpeg'
    elif isinstance(image, PngImagePlugin.PngImageFile):
      return 'image/png'
    elif isinstance(image, GifImagePlugin.GifImageFile):
      return 'image/gif'
    elif isinstance(image, TiffImagePlugin.TiffImageFile):
      return 'image/tiff'
    raise ValueError('Image type is not recognized')

  def __cloud_image_upload(self, bucket, name):
    self.__check_gcs()
    content_type = None
    try:
      import PIL.Image
      image = PIL.Image.open(bottle.request.body)
      content_type = Bottle.content_type(image)
    except (ValueError, IOError, OSError) as e:
      raise Response(415, {
        'error': '%s/invalid' % name,
        'reason': 'invalid image format: %s' % e
      })
    image = image.resize((256, 256), PIL.Image.ANTIALIAS)
    from io import BytesIO
    bs = BytesIO()
    image.save(bs, "PNG")
    self.__gcs.upload(
      bucket,
      name,
      data = bs.getvalue(),
      content_type = content_type
    )
    raise Response(201, {})

  def __cloud_image_get_url(self, bucket, name):
    self.__check_gcs()
    url = self.__gcs.download_url(
      bucket,
      name,
      expiration = datetime.timedelta(minutes = 3),
    )
    return url

  def __cloud_image_get(self, bucket, name):
    bottle.redirect(self.__cloud_image_get_url(bucket, name))

  def __cloud_image_delete(self, bucket, name):
    self.__check_gcs()
    self.__gcs.delete(
      bucket,
      name,
    )
    raise Response(200, {})

for name, conf in Bottle._Bottle__oauth_services.items():
  def oauth_get(self, username, name = name, conf = conf):
    beyond = self._Bottle__beyond
    params = {
      'client_id': getattr(beyond, '%s_app_key' % name),
      'response_type': 'code',
      'redirect_uri': '%s/oauth/%s' % (self.host(), name),
      'state': username,
    }
    if name == 'google' or name == 'gcs':
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
      raise self._Bottle__user_not_found(username)
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
      raise self._Bottle__user_not_found(username)
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
    for kind in ['google', 'gcs']:
      for id, account in getattr(user, '%s_accounts' % kind).items():
        # https://developers.google.com/identity/protocols/OAuth2InstalledApp
        # The associate google account.
        if account['refresh_token'] == refresh_token:
          google_url = "https://www.googleapis.com/oauth2/v3/token"
          # Get a new token and update the db and the client
          query = {
            'client_id':     getattr(beyond, '%s_app_key' % kind),
            'client_secret': getattr(beyond, '%s_app_secret' % kind),
            'refresh_token': account['refresh_token'],
            'grant_type':    'refresh_token',
          }
          res = requests.post(google_url, params = query)
          if res.status_code != 200:
            # Should forward the actual code received from Google
            # (res.status_code) but the linter doesn't like this.
            raise Response(400, {
              'error': 'credentials/refresh_failure',
              'reason': res.text
            })
          else:
            token = res.json()['access_token']
            account['token'] = token
            user.save()
            return token
  except User.NotFound:
    raise self._Bottle__user_not_found(username)

setattr(Bottle,
        user_credentials_google_refresh.__name__,
        user_credentials_google_refresh)

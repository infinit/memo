import infinit.beyond
import infinit.beyond.bottle
import infinit.beyond.couchdb

import time
import bottle
import requests
import threading

from functools import partial
from itertools import chain

import base64
from Crypto.Signature import PKCS1_v1_5
from Crypto.Hash import SHA256
from Crypto.PublicKey import RSA
import hashlib

def __enter__(self):
  thread = threading.Thread(
    target = partial(bottle.run, app = self, port = 0))
  thread.daemon = True
  thread.start()
  while not hasattr(self, 'port'):
    import time
    time.sleep(.1)
  return self

def __exit__(self, *args):
  pass

@property
def host(self):
  return 'http://127.0.0.1:%s' % self.port

bottle.Bottle.__enter__ = __enter__
bottle.Bottle.__exit__ = __exit__
bottle.Bottle.host = host

class Beyond:

  def __init__(self):
    super().__init__()
    self.__app = None
    self.__beyond = None
    self.__bottle = None
    self.__couchdb = infinit.beyond.couchdb.CouchDB()
    self.__datastore = None

    setattr(self, 'get',
            lambda url, **kw: self.request(url = url, method = 'GET', **kw))
    setattr(self, 'put',
            lambda url, **kw: self.request(url = url, method = 'PUT', **kw))
    setattr(self, 'delete',
            lambda url, **kw: self.request(url = url, method = 'DELETE', **kw))
    setattr(self, 'post',
            lambda url, **kw: self.request(url = url, method = 'POST', **kw))

  def __enter__(self):
    couchdb = self.__couchdb.__enter__()
    self.__datastore = \
      infinit.beyond.couchdb.CouchDBDatastore(couchdb)
    self.__beyond = infinit.beyond.Beyond(
      datastore = self.__datastore,
      dropbox_app_key = 'db_key',
      dropbox_app_secret = 'db_secret',
      google_app_key = 'google_key',
      google_app_secret = 'google_secret',
    )
    self.__app = infinit.beyond.bottle.Bottle(self.__beyond)
    self.__app.__enter__()
    return self

  def request(self, url, throws = True, json = {}, auth = None, **kwargs):
    # Older requests don't have json parameter
    if json is not None:
      j = json
      import json
      kwargs['data'] = json.dumps(j)
      kwargs['headers'] = {'Content-Type': 'application/json'}
      if auth is not None:
        der = base64.b64decode(auth.encode('utf-8'))
        k = RSA.importKey(der)
        h = base64.b64encode(hashlib.sha256(kwargs['data'].encode('latin-1')).digest())
        t = str(int(time.time()))
        string_to_sign = kwargs['method'] + ';' + url + ';'
        string_to_sign += h.decode('latin-1') + ';' + t

        raw_sig = PKCS1_v1_5.new(k).sign(SHA256.new(string_to_sign.encode('latin-1')))
        sig = base64.b64encode(raw_sig)
        kwargs['headers']['infinit-signature'] = sig
        kwargs['headers']['infinit-time'] = t
        import sys
    response = requests.request(url = '%s/%s' % (self.host, url),
                                allow_redirects = False,
                                **kwargs)
    if throws:
      if int(response.status_code / 100) != 2:
        response.raise_for_status()
    return response

  def __exit__(self, *args):
    self.__app.__exit__()
    self.__app = None
    self.__beyond = None
    self.__datastore = None
    self.__couchdb.__exit__(*args)

  @property
  def host(self):
    return 'http://127.0.0.1:%s' % self.__app.port

def throws(function, expected = None, json = True):
  try:
    function()
    assert False
  except requests.exceptions.HTTPError as e:
    if expected:
      assert e.response.status_code == expected
    if not json:
      return e.response
    response = e.response.json()
    assert 'reason' in response
    assert 'error' in response
    return response

def random_sequence(count = 10):
  from random import SystemRandom
  import string
  return ''.join(SystemRandom().choice(
    string.ascii_uppercase + string.digits) for _ in range(count))

def password_hash(password):
  salt = 'z^$P;:`a~F'
  from hashlib import pbkdf2_hmac
  import binascii
  dk = pbkdf2_hmac('sha256', bytes(password, encoding = 'ascii'), bytes(salt, encoding = 'ascii'), 100000)
  return str(binascii.hexlify(dk), encoding = 'ascii')

class User(dict):

  def __init__(self, name = None, password = None, email = None):
    self['name'] = random_sequence(10) if name is None else name
    self['email'] = self['name'] + '@infinit.io' if email is None else email
    self.__password = random_sequence(50) if password is None else password
    self.__password_hash = password_hash(self.__password)
    # Keys.
    from Crypto.PublicKey import RSA
    key = RSA.generate(2048, e=65537)
    def cleanup(key):
      import re
      r = re.compile('-+(BEGIN)?(END)? (RSA )?(PUBLIC)?(PRIVATE)? KEY-+')
      key = key.decode('ascii').replace('\n', '')
      import sys
      res = r.sub('', key)
      return res
    self['public_key'] = {'rsa': cleanup(key.publickey().exportKey("PEM"))}
    self.__private_key = cleanup(key.exportKey("PEM"))

  @property
  def private_key(self):
    return self.__private_key

  @property
  def password_hash(self):
    return self.__password_hash

  def put(self, hub, opt_out = True):
    from copy import deepcopy
    user = deepcopy(self)
    if opt_out is False:
      user['private_key'] = self.__private_key
      user['password_hash'] = self.__password_hash
    return hub.put('users/%s' % self['name'], json = user)

class Network(dict):
  kelips = {
    'type': 'kelips',
    'config': {
      'query_get_retries': 30,
      'file_timeout_ms': 120000,
      'k': 1,
      'ping_interval_ms': 1000,
      'query_put_insert_ttl': 3,
      'query_get_ttl': 10,
      'gossip': {
        'other_target': 3,
        'interval_ms': 2000,
        'group_target': 3,
        'bootstrap_group_target': 12,
        'old_threshold_ms': 40000,
        'contacts_other': 3,
        'files': 6,
        'bootstrap_other_target': 12,
        'new_threshold': 5,
        'contacts_group': 3
      }
    }
  }
  paxos = {
    'type': 'paxos',
    'replication-factor': 3,
  }

  def __init__(self, name, owner):
    self.__owner = owner
    self['overlay'] = Network.kelips
    self['consensus'] = Network.paxos
    self['owner'] = self.__owner['public_key']
    self.__short_name = name
    self['name'] = owner['name'] + '/' + self.__short_name

  @property
  def shortname(self):
    return self.__short_name

  @property
  def owner(self):
    return self.__owner

  def put(self, hub, owner = None):
    if owner is None:
      owner = self.owner
    return hub.put('networks/%s' % self['name'], json = self,
                   auth = owner.private_key)

class Volume(dict):

  def __init__(self, name, network):
    self.__short_name = name
    self.__network = network
    self['name'] = self.__network['name'] + '/' + name
    self['network'] = self.__network['name']

  @property
  def network(self):
    return self.__network

  def put(self, hub, owner = None):
    if owner is None:
      owner = self.network.owner
    return hub.put('volumes/%s' % self['name'], json = self,
                   auth = owner.private_key)

class Drives(dict):

  def __init__(self, name, volume, description = "Lorem ipsum", members = {}):
    self.__short_name = name
    self.__volume = volume
    self['name'] == volume.network.owner['name'] / self.__short_name

  @property
  def volume(self):
    return self.__volume

  def put(self, hub, owner = None):
    if owner is None:
      owner = self.volume.network.owner
    return hub.put('drives/%s' % self['name'], json = self,
                   auth = owner.private_key)

mefyl_priv = 'MIIEpAIBAAKCAQEAwxxSboENxD303yFLJq74qXHxry5CwoihdLqILuhwIEpx6yfUhgA/O7fbfroiyv5ZPfv268G1ZyfzkB+07qGxpg6XrPHgRvKX1ugPrH9i4W21jMzOXrg9NTq7MioWg8wQoqf11B483mpjkfwEx/ShlI5HsxaGQg0HqjICC23m3l7HpyX2A8R6L9vE68zRcJEGvatFXlGfqsxXJBqnbc/AgsWiHLz9H4HA2OuehJdlEHs4uNjDMhJGoXJr2ihC7hFxq7CdrvLnwf1oIdd94sDSQFL1jYLPXZYOHzrpGv+FLUqwuxMy5gQ8eJmirjXUs7yqegGqxVmk5ROzQN1QCFgm7wIDAQABAoIBAADxmSB5tVRWrGGL6q4kOIWxTGb5hU8llApZgKEhdLFjSsvFZIzFYYjrab9iLRroQgw/tMENLdBy7AWtcZWZ6J8SAP/QJ7KQJ9XdR34hG5xViIRG1VS19W3Ve+RROcynZwkyYMkG4Gp+/z5MhsVk1IdAbO5b1IhrQbc8CLB/dpdqwcicWqiR6t3mc5HkQ4mDOBSjrU11voiv8dIc0G6aOWEpqzoTYw7ewxoXugezmJnkOsObRWno2OZO83IVFXEmAjMe73+jhPKPbMD7WcT7ktKynPfFYh2kTrkhDDaTTs4I2UBCP6UlUaAt3IPFIZEJXCGplRTOldL3W+VEuTcXpZECgYEA58zLVUhgqFkpvxI7unu6ijjvLipgsBefbDseKfuHTHx9T7Unb2ESHW551e5lF5yuRmCjE2vJXFCQidyq0mwbhAaQK4PWuea5+RrPmYDpOQP3kQQAfsOrwauQR6sX2HxxIWwYcet4bO2dDEOOgN8d7fS6DpWw8yYSqiiUrXMAdPkCgYEA13rzCpXGzFU6H2pBmfamXFR56q0J+bmNanXMZ044UPyNdVYhJ8hFqs0bRjjR9QiHxg9/94Cfer7VKQePlxVh3JTduZQUcANXXowyFi6wLzmVABaM55jEDE0WkZcOIYMZIJWka92AsHP7ODp8QR752zC3Qwn48RlN3clzC7dfPScCgYEA5r0oZqNefBYNhUKMLCy/2pmkFStgBcnuCxmqBBZ6bvu47aAhOjDBjISNSRQ+k0uG+010538y+O7FgkYj0MSGe1zhJD/ffjwbQcmbf20gO34kcLkwGP+EOIwkWgMJAJmXL7Lffn7r6Fp7K1sQPl5a96TVlHETrGZoy/MLVMEWYlkCgYAqYZpP6KmTIugtqZ6Bg8uwuUTJbYNaxK4V1FmBsBbPhvzjqS8YPgHF2FWW+DIDecwKnp3Stk+nusT+LuiFFMWMtxLtHzzt0xpqFDT9u+0XPMIbpFPOcXON39Oiiw1SdhCJIiWWuZhIHGe65XXu8QK/o9NHsjxuX0W7a5XfJg/rXQKBgQDEo+kYUAW6JM2tod+4OxF8+s7q1E7fzZ7jgACoNzJ0RJVW9hGAlUhuXRkHIjnnhd2mDqYry7KNA5kvIS+oSH+wKjIpB6ZiBOhvgjww16LE6+aDoSLqmgwfHh2T2LpNfsc2UupCDp5W7jI4LPGbStiAeMLTtXU/XQ35Ov1BMWXN8g=='

mefyl = {
  'name': 'mefyl',
  'email': 'mefyl@infinit.io',
  'public_key': {'rsa': 'MIIBCgKCAQEAwxxSboENxD303yFLJq74qXHxry5CwoihdLqILuhwIEpx6yfUhgA/O7fbfroiyv5ZPfv268G1ZyfzkB+07qGxpg6XrPHgRvKX1ugPrH9i4W21jMzOXrg9NTq7MioWg8wQoqf11B483mpjkfwEx/ShlI5HsxaGQg0HqjICC23m3l7HpyX2A8R6L9vE68zRcJEGvatFXlGfqsxXJBqnbc/AgsWiHLz9H4HA2OuehJdlEHs4uNjDMhJGoXJr2ihC7hFxq7CdrvLnwf1oIdd94sDSQFL1jYLPXZYOHzrpGv+FLUqwuxMy5gQ8eJmirjXUs7yqegGqxVmk5ROzQN1QCFgm7wIDAQAB'},
}

mefyl2_priv = 'MIIEpAIBAAKCAQEAzonW3m0tPEkMfVfIRHIh7YEIMjSgewwTHcLlL47S/tXjkZ4fFXGIZPnvuqnva+NtBLhSv9roz/lxpYyqNikjdCPUjVZem14Xkj5imEi3ACCX03cdFKmfGmGFCi7gF1zZYtH2S3yql823yXudFDjp1iVqxky5gnkFyEYhImsanrpuhs2EIHfkV5mNFToB6U4+VkJgbeugURQXeTmlJ8BPKl+rPWABigPd+U1KCB/UO0EQO7eDdLqC2WBGQp2afTaX/j5KrMivt6sfI0eQft8WlNsUtbcunpRP+ak+tRoTL81edHMNmDVWU95vQTGw+iSeIE5o8ao/F7BT6IWC7Sw3XQIDAQABAoIBAAtHdceB2NWQ+7CgqZwrS3UH9eWgAB+YIjce3JtDRnyKO7pJE1N9dsBk8dWU0DFpIxv94O7/SnWJHs62ptj8WCZQipwJWnNLqSfgZkwAtJW6MfBncdweA0VSjAxpUO2VsX13D+dBcKOHpYDIUmS3UvXR50nbCMp6R3mPcuHJTZPba2JTBUk6ooG/UqaliY0iLveGnnbv1subhY0+X5hTbV5uxGfjSsYG5jA9eGR4lZneEAg8T7w3k/sa32oh483uc9RVmEHFqGTLinQlYInHWXkCk0xAFy7s8SSI34kZR+3t50Y9mg/Efu1U5KzYGLmNsHpXWmJ8oAIO4USLTFjQI5UCgYEA9HSG5g1lwbrycMg2Q7VzQ8f/ZbeS/ydlCNksj1PoxGgnV8aItT1wzq1NIouqMmvc9qsCbvN1sk9ozS53wk0LfrQgd5up1jJ7Z8uH9spBtsi7qLlWxxQ9BXukjX8hHaTeCc8YF7BErLEFD2E/6A2L0+AbGXlemVR+Z/wugM+MUxMCgYEA2ErluXO2GNtS5Kdpum9izcy8SsjHX+WjPln1a8Er5VLXM7r1Jkt0EpPEmO8xUTyjjX3MnXGnOTUk/9Z39qwJf2AwPIuC+j9zb4z6RrizLCDPEs1tGwD/YLQXtrh0DrSl20CCWeBRdlzV8M9dwIJGrFmIl4Kqmg3zPUrJ0a7dKc8CgYEAm6loDS4S0d49a1vSUiNFFrBQDXFsBVYMnCnOmiYQXqEEDHy7qM1K/BCWwZy18A2HUvtqPUSCedzfG2ivkeaFn1UMJ53T9DWJJ3sPRTNdzQrdlH8QpwxYHxmwmvmNGNdXHF/nM45m7KB8XGLM0vNtSqm2F+6VMoX/SC/pXNTMwkkCgYBuSVpzwo2ihQrybm7Z0nv15iRImbIXstcXLvWWGSyRxTjYNsdT3Ht2EYTYWnayLpJSzkdsLIyQ/gk8rpYC4FwDZ/+Qj66cfYgV5DOlpf4uTRhpRPgSIeMV4x6IW+tJqFE9x9nvjBLdoJ6yKHpsc8Enlouwfb8RyHUz1pOr6Fb7PwKBgQDmoiOqLuztGLcGHLI/ShwTkb78okd67jr2Ix8IuW+J/oOnYKA6Rj3hdo5D7iTIall+wSDzp/Sv7TsFF70dCN6O7XgYLAwcuJOBY1Z3mo5HOyOGt1gWAhu9Hi2OFKWUMrhl7HLNGX2X9DUDJQyHm5aO48seoofEgqkhktkiclasrg=='

mefyl2 = {
  'name': 'mefyl',
  'email': 'mefyl@infinit.io',
  'public_key':
  {'rsa': 'MIIBCgKCAQEAzonW3m0tPEkMfVfIRHIh7YEIMjSgewwTHcLlL47S/tXjkZ4fFXGIZPnvuqnva+NtBLhSv9roz/lxpYyqNikjdCPUjVZem14Xkj5imEi3ACCX03cdFKmfGmGFCi7gF1zZYtH2S3yql823yXudFDjp1iVqxky5gnkFyEYhImsanrpuhs2EIHfkV5mNFToB6U4+VkJgbeugURQXeTmlJ8BPKl+rPWABigPd+U1KCB/UO0EQO7eDdLqC2WBGQp2afTaX/j5KrMivt6sfI0eQft8WlNsUtbcunpRP+ak+tRoTL81edHMNmDVWU95vQTGw+iSeIE5o8ao/F7BT6IWC7Sw3XQIDAQAB'},
}

network = {
  'consensus': {
    'type': 'paxos',
    'replication-factor': 3,
  },
  'overlay': {
    'type': 'kelips',
    'config': {
      'query_get_retries': 30,
      'file_timeout_ms': 120000,
      'k': 1,
      'ping_interval_ms': 1000,
      'query_put_insert_ttl': 3,
      'query_get_ttl': 10,
      'gossip': {
        'other_target': 3,
        'interval_ms': 2000,
        'group_target': 3,
        'bootstrap_group_target': 12,
        'old_threshold_ms': 40000,
        'contacts_other': 3,
        'files': 6,
        'bootstrap_other_target': 12,
        'new_threshold': 5,
        'contacts_group': 3
      },
      'ping_timeout_ms': 1000,
      'node_id': 'SVyRYERs4s675ceW/Jt/hlBSfvWrjwZwwp+lhXJVq7Y=',
      'encrypt': True,
      'wait': 0,
      'max_other_contacts': 6,
      'query_put_retries': 12,
      'accept_plain': False,
      'bootstrap_nodes': [ ],
      'rpc_protocol': 'all',
      'query_put_ttl': 10,
      'query_timeout_ms': 1000,
      'contact_timeout_ms': 120000
    }
  },
  'owner': {
    'rsa': 'MIIBCAKCAQEAq1pXuFI8l8MopHufZ4S3fe+WoR5wgeaPtZhw9IFuHZ+3F7V7fCzy76gKp5EPz5sk2Dowd90d+TuEUjUUkI0fRLJipRPjo2reFsuOAZ244ee/NLtG601vQUS/sV8ow2QZEAoNAiNZQGr4jEqvmjIB+rwOmx9eUgs887KjUYlX+wH5984EAr/qd62VddYXga8o4T2QX4GlYik/s/yKm0dlCQgZXQPYM5Wogv6KluGdLFKBaNc2HYkGEArZE51sATRcDOSQcycg2sGuwfL/LfClsCkx2LSYjJh9qkiBNUsAg+LeRt/9Hv3S32tcMszCph3nSX5u+1yz8VURHjVGh9ptAwIBIw=='
  },
  'name': 'mefyl/infinit',
}

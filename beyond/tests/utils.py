import infinit.beyond
import infinit.beyond.bottle
import infinit.beyond.couchdb

import bottle
import requests
import threading

from functools import partial
from itertools import chain

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

  def request(self, url, **kwargs):
    return requests.request(url = '%s/%s' % (self.host, url),
                            allow_redirects = False,
                            **kwargs)

  def get(self, url, **kwargs):
    return self.request(url = url, method = 'GET', **kwargs)

  def put(self, url, **kwargs):
    return self.request(url = url, method = 'PUT', **kwargs)

  def __exit__(self, *args):
    self.__app.__exit__()
    self.__app = None
    self.__beyond = None
    self.__datastore = None
    self.__couchdb.__exit__(*args)

  @property
  def host(self):
    return 'http://127.0.0.1:%s' % self.__app.port

mefyl = {
  'name': 'mefyl',
  'public_key': {'rsa': 'MIIBCAKCAQEAq1pXuFI8l8MopHufZ4S3fe+WoR5wgeaPtZhw9IFuHZ+3F7V7fCzy76gKp5EPz5sk2Dowd90d+TuEUjUUkI0fRLJipRPjo2reFsuOAZ244ee/NLtG601vQUS/sV8ow2QZEAoNAiNZQGr4jEqvmjIB+rwOmx9eUgs887KjUYlX+wH5984EAr/qd62VddYXga8o4T2QX4GlYik/s/yKm0dlCQgZXQPYM5Wogv6KluGdLFKBaNc2HYkGEArZE51sATRcDOSQcycg2sGuwfL/LfClsCkx2LSYjJh9qkiBNUsAg+LeRt/9Hv3S32tcMszCph3nSX5u+1yz8VURHjVGh9ptAwIBIw=='},
}

mefyl2 = {
  'name': 'mefyl',
  'public_key':
  {'rsa': 'MIIBCgKCAQEA7nhbx8j2hGaMjSrFGCToNcduUX1uHwB8GbmhjkMhspq57YTJ5Krx3N21BAbZnUpSjR/t36YtsvUgPSRGMmKl8W0YouKe18z2ihmO502zzOqloazZeVcOxrF5jRx2mCk+3DszDGUJgOETyY/5lJnb/ToNCvyKEF5epW0suyw4tFZoGI5unBXN7V90dPq+h7zli7XQ23JAExXFLzOzdiCD6DVWoYN13sROg2SUqMTtyFN8047A7rR/J87Son0x5f/cIIIS4imQVE0PLFaQOpB45RxE9yI3rHy+8MDlWyTG74BUFpxOVLM2FjLvGhIg/mv0TM9awrmyY+YPgGbRDmm/BQIDAQAB'},
}

network = {
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
  'name': 'mefyl/infinit'
}

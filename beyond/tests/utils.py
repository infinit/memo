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

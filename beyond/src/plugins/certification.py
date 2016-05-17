import bottle

class Plugin(object):

  '''Bottle plugin to handle SSL client certificates.'''

  name = 'one.infinit.ssl-client-certificate'
  api  = 2
  key_dn = 'SSL_CLIENT_DN'
  key_ok = 'SSL_CLIENT_VERIFIED'

  def __init__(self):
    pass

  def apply(self, callback, route):
    def wrapper(*args, **kwargs):
      environ = bottle.request.environ
      bottle.request.certificate = None
      if Plugin.key_ok in environ and Plugin.key_dn in environ:
        if environ[Plugin.key_ok] == 'SUCCESS':
          try:
            dn = environ[Plugin.key_dn]
            field = dn.split('/')[-1]
            email = field.split('=')[-1]
          except:
            pass
          else:
            bottle.request.certificate = email
      return callback(*args, **kwargs)
    return wrapper

import bottle
from infinit.beyond.response import Response

class Plugin(object):

  '''Bottle plugin that makes sure that the request size is less than given
  parameter.

  bottle.request.MEMFILE_MAX does that ONLY if the content type is
  'application/json', otherwise, it copys the body to a file by chuncks of
  MEMFILE_MAX.
  '''

  name = 'beyond.MaxSize'
  api  = 2

  def __init__(self, size):
    self.__size = size

  def apply(self, callback, route):
    def wrapper(*args, **kwargs):
      if bottle.request.content_length > self.__size:
        raise Response(413, {
          'error': 'too_large',
          'reason': 'request can not be larger than %sB' % self.__size
        })
      return callback(*args, **kwargs)
    return wrapper

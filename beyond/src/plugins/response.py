import bottle

from infinit.beyond import exceptions
from infinit.beyond.response import Response

class Plugin(object):

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
            exception.field.object, exception.field.name),
          'reason': 'missing field %s' % exception.field.name
        }
      except exceptions.InvalidFormat as exception:
        bottle.response.status = 422
        return {
          'error': '%s/invalid_format/%s' % (
            exception.field.object, exception.field.name),
          'reason': '%s has an invalid format' % exception.field.name
        }
      except Response as response:
        bottle.response.status = response.status
        if response.headers is not None:
          for e in response.headers.items():
            bottle.response.set_header(e[0], e[1])
        return response.body
    return wrapper

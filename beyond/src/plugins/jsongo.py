import calendar
import datetime
import uuid

def jsonify(value):
  import collections
  if isinstance(value, uuid.UUID):
    return str(value)
  elif isinstance(value, dict):
    return dict((key, jsonify(value)) for key, value in value.items())
  elif isinstance(value, collections.Iterable) \
       and not isinstance(value, str):
    return value.__class__(jsonify(sub) for sub in value)
  elif isinstance(value, datetime.datetime):
    return calendar.timegm(value.timetuple())
  else:
    return value

def jsonify_dict(value):
  if isinstance(value, dict):
    return jsonify(value)
  else:
    return value

class Plugin(object):

  '''Bottle plugin to automatically convert return types to serializable formats.'''

  name = 'beyond.jsongo'
  api  = 2

  def apply(self, callback, route):
    def wrapper(*args, **kwargs):
      res = callback(*args, **kwargs)
      res = jsonify_dict(res)
      return res
    return wrapper

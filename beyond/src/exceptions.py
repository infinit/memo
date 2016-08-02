class Field:

  def __init__(self, object, name):
    self.__object = object
    self.__name = name

  @property
  def object(self):
    return self.__object

  @property
  def name(self):
    return self.__name

class MissingField(Exception):

  def __init__(self, object, field_name):
    self.field = Field(object, field_name)
    super().__init__('Missing field %s (object %s)' % (field_name, object))

class InvalidFormat(Exception):

  def __init__(self, object, field_name):
    self.field = Field(object, field_name)
    super().__init__('Invalid format for %s (object %s)' % (field_name, object))

class NoLongerAvailable(Exception):

  def __init__(self, object):
    super().__init__('Resource %s is not longer available' % object)

class AuthenticationException(Exception):
  pass

class MissingCertificate(AuthenticationException):

  def __init__(self):
    super().__init__('Missing certificate for authentication')

class UserNotAdmin(AuthenticationException):

  def __init__(self, user):
    self.user = user
    super().__init__('User (%s) is not an administrator' % user)

class BannedUser(AuthenticationException):

  def __init__(self, user):
    self.user = user
    super().__init__('User (%s) is banned' % user)

class MissingSignature(AuthenticationException):

  def __init__(self):
    super().__init__('Missing signature for authentication')

class MissingTimeHeader(AuthenticationException):

  def __init__(self):
    super().__init__('Missing time header for authentication')

class ClockSkew(AuthenticationException):

  def __init__(self, delay):
    self.delay = delay
    super().__init__('Request signed outside (+/- %ds) of window' % delay)

class InvalidAuthentication(AuthenticationException):

  def __init__(self):
    super().__init__('Invalid authentication')

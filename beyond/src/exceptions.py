class Field:

  def __init__(self, type, name):
    self.__type = type
    self.__name = name

  @property
  def type(self):
    return self.__type

  @property
  def name(self):
    return self.__name

class MissingField(Exception):

  def __init__(self, type, field_name):
    self.field = Field(type, field_name)
    super().__init__('Missing field %s (object %s)' % (field_name, type))

class InvalidFormat(Exception):

  def __init__(self, type, field_name):
    self.field = Field(type, field_name)
    super().__init__('Invalid format for %s (object %s)' % (field_name, type))

class NotOptIn(Exception):

  def __init__(self, operation):
    super().__init__('User choosed not to use the hub for %s' % operation)

class Mismatch(Exception):

  def __init__(self, field):
    super().__init__('%s do not match the storad one' % field)

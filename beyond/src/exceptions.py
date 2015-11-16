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

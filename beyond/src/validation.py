from infinit.beyond.exceptions import *

## ------------------ ##
## Field verification ##
## ------------------ ##
class Validator:

  def __init__(self, object, field):
    self.field = Field(object, field)

class Name(Validator):

  def __call__(self, name):
    import re
    if len(name) == 0 or re.compile('^[^\/]+$').match(name) is None:
      raise InvalidFormat(self.field.object, self.field.name)

class Email(Validator):

  def __init__(self, object):
    super().__init__(object, 'email')

  def __call__(self, email):
    import re
    validator = re.compile('^[^@]+@[^@]+\.[^@]+$')
    if validator.match(email) is None:
      raise InvalidFormat(self.field.object, self.field.name)

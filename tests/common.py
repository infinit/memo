# These bits are use in memo tests, and in beyond tests.

import sys

def here():
  '''The "file:line" of the top-level call.'''
  import inspect
  frame = inspect.currentframe()
  while frame.f_back:
    frame = frame.f_back
  finfo = inspect.getframeinfo(frame)
  return finfo.filename + ":" + str(finfo.lineno)


def log(*args, level='info'):
  print(here() + ':',
        # pass "info:" so that Emacs' compilation-mode don't believe
        # all these lines are errors.
        level + ':' if level else '',
        *args,
        file=sys.stderr, flush=True)

  
class Unreachable(BaseException):

  def __init__(self):
    super().__init__("Unreachable code reached")

def unreachable():
  raise Unreachable()


class Emailer:

  def __init__(self):
    self.emails = {}

  def send_one(self, template, recipient_email, variables = {}, *args, **kwargs):
    self.__store(template, recipient_email, variables)

  def __store(self, template, recipient_email, variables):
    self.get_specifics(recipient_email, template).append(variables)

  def get(self, email):
    return self.emails.setdefault(email, {})

  def get_specifics(self, email, template):
    return self.get(email).setdefault(template, [])

  def __str__(self):
    return "Emailer: %s" % self.emails


class FakeGCS:

  def __init__(self):
    self.__store = {}

  def upload(self, bucket, path, *args, **kwargs):
    self.__store[path] = 'url'

  def upload_url(self, bucket, path, *args, **kwargs):
    return bucket + '/' + path

  def delete(self, bucket, path):
    if path in self.__store:
      del self.__store[path]

  def download_url(self, bucket, path, *args, **kwargs):
    return self.__store.get(path,  None)

# These bits are used in memo tests, and in beyond tests.

import sys

from difflib import unified_diff as udiff

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

def assertEq2(a, b):
  if a == b:
    log('PASS: {} == {}'.format(a, b))
  else:
    def lines(s):
      s = str(s)
      if s[:-1] != '\n':
        s += '\n'
      return s.splitlines(1)

    diff = ''.join(udiff(lines(a),
                         lines(b),
                         fromfile='a', tofile='b'))
    raise AssertionError('%s: %r != %r\n%s' % (here(), a, b, diff))

def assertEq(a, *bs):
  for b in bs:
    assertEq2(a, b)

def assertNeq(a, b):
  if a != b:
    log('PASS: {} != {}'.format(a, b))
  else:
    raise AssertionError('%r == %r' % (a, b))

def assertIn(a, b):
  if a in b:
    log('PASS: {} in {}'.format(a, b))
  else:
    raise AssertionError('%r not in %r' % (a, b))

def random_sequence(count = 10):
  from random import SystemRandom
  import string
  return ''.join(SystemRandom().choice(
    string.ascii_lowercase + string.digits) for _ in range(count))



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

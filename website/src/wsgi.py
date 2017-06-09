import infinit.website

class Website(infinit.website.Website):

  def match(self, environ):
    if environ['SERVER_NAME'] == 'memo.infinit.sh':
      environ = dict(environ)
      environ['PATH_INFO'] = '/memo{}'.format(environ['PATH_INFO'])
    return super().match(environ)

  def reverse(self, *args, **kwargs):
    res = super().reverse(*args, **kwargs)
    prefix = '/memo'
    if res.startswith(prefix):
      res = 'https://memo.infinit.sh{}'.format(res[len(prefix):])
    else:
      res = 'https://infinit.sh{}'.format(res)
    return res

application = Website()

import bottle

from infinit.website.utils import route, static_file, view

class Website(bottle.Bottle):

  def __init__(self):
    super().__init__()
    route.apply(self)

  @route('/', name = 'home')
  @view('pages/home')
  def root():
    return {
      'description': 'Infinit Filesystem',
    }

  @route('/css/<path:path>')
  @route('/images/<path:path>')
  @route('/js/<path:path>')
  def images(path):
    d = bottle.request.urlparts.path.split('/')[1]
    return static_file('%s/%s' % (d,  path))

import bottle

from infinit.website.utils import route, static_file, view

class Website(bottle.Bottle):

  def __init__(self):
    super().__init__()
    route.apply(self)

  @route('/')
  @view('pages/home')
  def root():
    return {}

  @route('/images/<path:path>')
  def images(path):
    return static_file('images/%s' % path)

  @route('/css/<path:path>')
  def images(path):
    return static_file('css/%s' % path)

  @route('/js/<path:path>')
  def images(path):
    return static_file('js/%s' % path)
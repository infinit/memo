import bottle

from infinit.website.utils import route, view

class Website(bottle.Bottle):

  def __init__(self):
    super().__init__()
    route.apply(self)

  @route('/')
  @view('pages/root')
  def root():
    return {}

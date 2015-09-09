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

  @route('/overview', name = 'doc_overview')
  @view('pages/doc/overview.html')
  def root():
    return {
      'title': 'Overview',
      'description': 'Infinit',
    }

  @route('/get-started', name = 'doc_get_started')
  @view('pages/doc/get_started.html')
  def root():
    return {
      'title': 'Get Started',
      'description': 'Infinit',
    }

  @route('/documentation/technology', name = 'doc_technology')
  @view('pages/doc/technology.html')
  def root():
    return {
      'title': 'Technology',
      'description': 'Infinit',
    }

  @route('/documentation/comparisons', name = 'doc_comparisons')
  @view('pages/doc/comparisons.html')
  def root():
    return {
      'title': 'Comparisons',
      'description': 'Infinit',
    }

  @route('/documentation/deployments/unlimited-personal-drive', name = 'doc_deployment_personal_cloud')
  @view('pages/doc/deployments/personal_cloud.html')
  def root():
    return {
      'title': 'Personal Cloud',
      'description': 'Infinit',
    }

  @route('/roadmap', name = 'doc_roadmap')
  @view('pages/doc/roadmap.html')
  def root():
    return {
      'title': 'Roadmap',
      'description': 'Infinit',
    }

  @route('/about', name = 'about')
  @view('pages/about.html')
  def root():
    return {
      'title': 'About',
      'description': 'Infinit',
    }

  @route('/legal', name = 'legal')
  @view('pages/legal.html')
  def root():
    return {
      'title': 'Legal',
      'description': 'Infinit',
    }

  @route('/css/<path:path>')
  @route('/fonts/<path:path>')
  @route('/images/<path:path>')
  @route('/js/<path:path>')
  def images(path):
    d = bottle.request.urlparts.path.split('/')[1]
    return static_file('%s/%s' % (d,  path))

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
      'title': 'An Overview of the Infinit File System',
      'description': 'Discover the benefits of the Infinit file system through its innovative technology.',
    }

  @route('/get-started', name = 'doc_get_started')
  @view('pages/doc/get_started.html')
  def root():
    return {
      'title': 'Get Started with Infinit',
      'description': 'A step by step guide to getting started with the Infinit file system platform.',
    }

  @route('/documentation/technology', name = 'doc_technology')
  @view('pages/doc/technology.html')
  def root():
    return {
      'title': 'Technology Behind Infinit',
      'description': 'Discover the different layers composing the Infinit technology, from the reactor, the distributed hash table up to the file system.',
    }

  @route('/documentation/comparisons', name = 'doc_comparisons')
  @view('pages/doc/comparisons.html')
  def root():
    return {
      'title': 'Comparison Between Infinit and Other File Systems',
      'description': 'Compare the Infinit file system against existing centralized, distributed and decentralized file systems.',
    }

  @route('/deployments/unlimited-personal-drive', name = 'doc_deployment_personal_cloud')
  @view('pages/doc/deployments/personal_cloud.html')
  def root():
    return {
      'title': 'Unlimited Personal Drive with Infinit',
      'description': 'Create a personal drive of unlimited capacity by aggregating the storage resources from various cloud services.',
    }

  @route('/deployments/decentralized-collaborative-file-system', name = 'doc_deployment_file_system')
  @view('pages/doc/deployments/personal_cloud.html')
  def root():
    return {
      'title': 'Decentralized Collaborative File System with Infinit',
      'description': 'Create a private or hybrid cloud storage infrastructure by relying on cloud storage resources or commodity on-premise hardware.',
    }

  @route('/roadmap', name = 'doc_roadmap')
  @view('pages/doc/roadmap.html')
  def root():
    return {
      'title': 'Roadmap',
      'description': 'Discover the next developments of the Infinit platform.',
    }

  @route('/about', name = 'about')
  @view('pages/about.html')
  def root():
    return {
      'title': 'About Infinit',
      'description': 'Learn about the company and people behind Infinit',
    }

  @route('/contact', name = 'contact')
  @view('pages/contact.html')
  def root():
    return {
      'title': 'Contact Us',
      'description': 'Get in touch with a sales representative of Infinit.',
    }

  @route('/legal', name = 'legal')
  @view('pages/legal.html')
  def root():
    return {
      'title': 'Legal Terms',
      'description': 'All the legal terms related to the use of the Infinit products and services.',
    }

  @route('/css/<path:path>')
  @route('/fonts/<path:path>')
  @route('/images/<path:path>')
  @route('/js/<path:path>')
  def images(path):
    d = bottle.request.urlparts.path.split('/')[1]
    return static_file('%s/%s' % (d,  path))

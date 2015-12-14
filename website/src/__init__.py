import bottle
import sendwithus

from infinit.website.utils import route, static_file, view

class Website(bottle.Bottle):

  def __init__(self):
    super().__init__()
    self.install(bottle.CertificationPlugin())
    route.apply(self)
    self.__swu = sendwithus.api(api_key = 'live_f237084a19cbf6b2373464481155d953a4d86e8d')

  def debug(self):
    if hasattr(bottle.request, 'certificate') and \
       bottle.request.certificate in [
         'antony.mechin@infinit.io',
         'baptiste.fradin@infinit.io',
         'christopher.crone@infinit.io',
         'gaetan.rochel@infinit.io',
         'julien.quintard@infinit.io',
         'matthieu.nottale@infinit.io',
         'patrick.perlmutter@infinit.io',
         'quentin.hocquet@infinit.io',
       ]:
      return True
    else:
      return super().debug()

  @route('/', name = 'home')
  @view('pages/home')
  def root(self):
    return {
      'description': 'Infinit allows for the creation of flexible, secure and controlled file storage infrastructure on top of public, private or hybrid cloud resources.',
    }

  @route('/documenation/overview', name = 'doc_overview')
  @view('pages/docs/overview.html')
  def root(self):
    return {
      'title': 'An Overview of the Infinit File System',
      'description': 'Discover the benefits of the Infinit file system through its innovative technology.',
    }

  @route('/drive', name = 'drive')
  @view('pages/drive')
  def root(self):
    return {
      'title': 'Infinit Drive',
      'description': 'The Infinit Drive allows any small and medium business to securely store and access files from anywhere through an easy-to-use virtual disk drive interface.',
    }

  @route('/get-started', name = 'doc_get_started')
  @view('pages/docs/get_started.html')
  def root(self):
    return {
      'title': 'Get Started with Infinit',
      'description': 'A step by step guide to getting started with the Infinit file system platform.',
    }

  @route('/get-started/windows', name = 'doc_get_started_windows')
  @view('pages/docs/get_started_windows.html')
  def root(self):
    return {
      'title': 'Get Started with Infinit - Windows Guide',
      'description': 'A step by step guide for Windows to getting started with the Infinit file system platform.',
    }

  @route('/documentation/technology', name = 'doc_technology')
  @view('pages/docs/technology.html')
  def root(self):
    return {
      'title': 'Technology Behind Infinit',
      'description': 'Discover the different layers composing the Infinit technology, from the reactor, the distributed hash table up to the file system.',
    }

  @route('/documentation/comparisons', name = 'doc_comparisons')
  @view('pages/docs/comparisons.html')
  def root(self):
    return {
      'title': 'Comparison Between Infinit and Other File Systems',
      'description': 'Compare the Infinit file system against existing centralized, distributed and decentralized file systems.',
    }

  @route('/deployments/unlimited-personal-drive', name = 'doc_deployment_personal_cloud')
  @view('pages/docs/personal_cloud.html')
  def root(self):
    return {
      'title': 'Unlimited Personal Drive with Infinit',
      'description': 'Create a personal drive of unlimited capacity by aggregating the storage resources from various cloud services.',
    }

  @route('/deployments/decentralized-collaborative-file-system', name = 'doc_deployment_file_system')
  @view('pages/docs/decentralized_fs.html')
  def root(self):
    return {
      'title': 'Decentralized Collaborative File System with Infinit',
      'description': 'Create a private or hybrid cloud storage infrastructure by relying on cloud storage resources or commodity on-premise hardware.',
    }

  @route('/documentation/reference', name = 'doc_reference')
  @view('pages/docs/reference.html')
  def root(self):
    return {
      'title': 'Reference Documentation',
      'description': 'Read the reference document detailing how to use the Infinit command-line tools, hub and more.',
    }

  @route('/roadmap', name = 'doc_roadmap')
  @view('pages/docs/roadmap.html')
  def root(self):
    return {
      'title': 'Roadmap',
      'description': 'Discover the next developments of the Infinit platform.',
    }

  @route('/open-source', name = 'opensource')
  @view('pages/opensource.html')
  def root(self):
    return {
      'title': 'Contribute to the Infinit File System',
      'description': 'Check out our open source projects and join a growing community of developers.',
    }

  @route('/pricing', name = 'pricing')
  @view('pages/pricing.html')
  def root(self):
    return {
      'title': 'Plans',
      'description': 'Discover the different plans for small, medium and large enterprises.',
    }

  @route('/solutions', name = 'solutions')
  @view('pages/solutions.html')
  def root(self):
    return {
      'title': 'Business solutions',
      'description': 'Infinit is used by device manufacturers, network operators and other businesses throughout the world to provide value to their customers.',
    }

  @route('/about', name = 'about')
  @view('pages/about.html')
  def root(self):
    return {
      'title': 'About Infinit',
      'description': 'Learn about the company and people behind Infinit',
    }

  @route('/contact', name = 'contact')
  @view('pages/contact.html')
  def root(self):
    return {
      'title': 'Contact Us',
      'description': 'Get in touch with a sales representative of Infinit.',
    }

  @route('/contact', method = 'POST')
  def root(self):
    fields = ['first_name', 'last_name', 'email', 'message']
    if not all(bottle.request.forms.get(field) for field in fields):
      bottle.response.status = 400
      return {}
    else:
      response = self.__swu.send(
        email_id = 'tem_XvZ5rnCzWqiTv6NLawEET4',
        recipient = {'address': 'contact@infinit.sh'},
        email_data = {
          f: bottle.request.forms.get(f) for f in ['first_name', 'last_name', 'email', 'message', 'phone', 'company', 'country'] if bottle.request.forms.get(f)
        })
      if response.status_code // 100 != 2:
        bottle.response.status = 503
        return {}
      else:
        return {}

  @route('/legal', name = 'legal')
  @view('pages/legal.html')
  def root(self):
    return {
      'title': 'Legal Terms',
      'description': 'All the legal terms related to the use of the Infinit products and services.',
    }

  @route('/slack', name = 'slack', method = 'POST')
  def root(self):
    email = bottle.request.forms.get('email')

    if not email:
      bottle.response.status = 400
      return {}
    else:
      response = self.__swu.send(
        email_id = 'tem_XvZ5rnCzWqiTv6NLawEET4',
        recipient = {'address': 'contact@infinit.sh'},
        email_data = {
          'email': email,
          'message': '%s wants to join the Slack community.' % (email),
        })
      if response.status_code // 100 != 2:
        bottle.response.status = 503
        return {}
      else:
        return {}
    return {}

  @route('/css/<path:path>')
  @route('/fonts/<path:path>')
  @route('/images/<path:path>')
  @route('/js/<path:path>')
  def images(self, path):
    d = bottle.request.urlparts.path.split('/')[1]
    return static_file('%s/%s' % (d,  path))

  @route('/robots.txt')
  def file(self):
    return static_file('robots.txt')
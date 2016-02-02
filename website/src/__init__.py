import bottle
import sendwithus
import requests
import os

from infinit.website.utils import route, static_file, view

def error(code, reason = ''):
  bottle.response.status = code
  if not reason:
    reason = requests.status_codes._codes.get(code, '')[0]
  return {
    'reason': reason
  }

class Website(bottle.Bottle):

  def __init__(self):
    super().__init__()
    self.install(bottle.CertificationPlugin())
    route.apply(self)
    self.__swu = sendwithus.api(api_key = 'live_f237084a19cbf6b2373464481155d953a4d86e8d')
    self.__hub = os.environ.get('INFINIT_BEYOND', 'https://infinit.sh')

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

  @route('/documentation', name = 'doc_overview')
  @route('/documentation/overview', name = 'doc_overview')
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

  @route('/faq', name = 'doc_faq')
  @view('pages/faq.html')
  def root(self):
    return {
      'title': 'FAQ',
      'description': 'Frequently Asked Questions about how to use our file system, how it compares to others and more.',
    }

  @route('/get-started', name = 'doc_get_started')
  @view('pages/docs/get_started.html')
  def root(self):
    return {
      'title': 'Get Started with Infinit',
      'description': 'A step by step guide to getting started with the Infinit file system platform.',
    }

  @route('/get-started/mac', name = 'doc_get_started_mac')
  @view('pages/docs/get_started.html')
  def root(self):
    return {
      'title': 'Get Started with Infinit - Mac Guide',
      'description': 'A step by step guide to getting started with the Infinit file system platform.',
    }

  @route('/get-started/windows', name = 'doc_get_started_windows')
  @view('pages/docs/get_started_windows.html')
  def root(self):
    return {
      'title': 'Get Started with Infinit - Windows Guide',
      'description': 'A step by step guide for Windows to getting started with the Infinit file system platform.',
    }

  @route('/get-started/linux', name = 'doc_get_started_linux')
  @view('pages/docs/get_started.html')
  def root(self):
    return {
      'title': 'Get Started with Infinit - Linux Guide',
      'description': 'A step by step guide for Windows to getting started with the Infinit file system platform.',
    }

  @route('/documentation/technology', name = 'doc_technology')
  @view('pages/docs/technology.html')
  def root(self):
    return {
      'title': 'Technology Behind Infinit',
      'description': 'Discover the different layers composing the Infinit technology, from the reactor, the distributed hash table up to the file system.',
    }

  # @route('/documentation/comparisons', name = 'doc_comparisons')
  # @view('pages/docs/comparisons.html')
  # def root(self):
  #   return {
  #     'title': 'Comparison Between Infinit and Other File Systems',
  #     'description': 'Compare the Infinit file system against existing centralized, distributed and decentralized file systems.',
  #   }

  # @route('/deployments/unlimited-personal-drive', name = 'doc_deployment_personal_cloud')
  # @view('pages/docs/personal_cloud.html')
  # def root(self):
  #   return {
  #     'title': 'Unlimited Personal Drive with Infinit',
  #     'description': 'Create a personal drive of unlimited capacity by aggregating the storage resources from various cloud services.',
  #   }

  # @route('/deployments/decentralized-collaborative-file-system', name = 'doc_deployment_file_system')
  # @view('pages/docs/decentralized_fs.html')
  # def root(self):
  #   return {
  #     'title': 'Decentralized Collaborative File System with Infinit',
  #     'description': 'Create a private or hybrid cloud storage infrastructure by relying on cloud storage resources or commodity on-premise hardware.',
  # }

  @route('/documentation/deployments', name = 'doc_deployments')
  @view('pages/docs/deployments.html')
  def root(self):
    return {
      'title': 'Examples of Deployments with Infinit',
      'description': 'Discover how Infinit can be used to deploy various types of storage infrastructure.',
    }

  @route('/documentation/reference', name = 'doc_reference')
  @view('pages/docs/reference.html')
  def root(self):
    return {
      'title': 'Reference Documentation',
      'description': 'Read the reference document detailing how to use the Infinit command-line tools, hub and more.',
    }

  @route('/documentation/roadmap', name = 'doc_roadmap')
  @view('pages/docs/roadmap.html')
  def root(self):
    return {
      'title': 'Roadmap',
      'description': 'Discover the next developments of the Infinit platform.',
    }

  @route('/documentation/changelog', name = 'doc_changelog')
  @view('pages/docs/changelog.html')
  def root(self):
    return {
      'title': 'Change Log',
      'description': 'Have a look at all the recent changes of the Infinit platform.',
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
      'description': 'Learn about the company and people behind Infinit.',
    }

  @route('/press', name = 'press')
  @view('pages/press.html')
  def root(self):
    return {
      'title': 'Press Releases',
      'description': 'See all our press releases and download our press kit.',
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
      return error(400)
    else:
      response = self.__swu.send(
        email_id = 'tem_XvZ5rnCzWqiTv6NLawEET4',
        recipient = {'address': 'contact@infinit.sh'},
        email_data = {
          f: bottle.request.forms.get(f) for f in ['first_name', 'last_name', 'email', 'message', 'phone', 'company', 'country'] if bottle.request.forms.get(f)
        })
      if response.status_code // 100 != 2:
        return error(503)
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
      return error(400, reason = 'missing mandatory email')
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

  @route('/users/confirm_email', name = 'confirm_email', method = 'GET')
  @view('pages/users/confirm_email.html')
  def root(self):
    for field in ['name', 'confirmation_code']:
      if bottle.request.params.get(field) is None:
        return error(400, 'missing mandatory %s' % field)
    email = bottle.request.params.get('email')
    confirmation_code = bottle.request.params.get('confirmation_code')
    name = bottle.request.params.get('name')
    url = '%s/users/%s/confirm_email' % (self.__hub, name)
    if email is not None:
      import urllib.parse
      url += '/%s' % urllib.parse.quote_plus(email)
    import json
    response = requests.post(
      url = url,
      data = json.dumps({
        'confirmation_code': confirmation_code
      }),
      headers = {'Content-Type': 'application/json'},
    )
    if (response.status_code // 100 != 2 and response.status_code != 410):
      return error(response.status_code,
                   reason = 'server error %s' % response.status_code)
    errors = []
    try:
      errors = response.json()['errors']
    except Exception:
      pass
    return {
      'title': 'Confirm Email',
      'description': 'Confirm your email and start using Infinit.',
    }

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

  @route('/sitemap.xml')
  def file(self):
    return static_file('sitemap.xml')

import bottle
import functools
import httpagentparser
import os
import os.path
import sys

from infinit.website.version import version
from infinit.website.tarball_version import tarball_version

ROOT = os.path.dirname(os.path.realpath(__file__))
PREFIX = ROOT
for i in range(4):
  PREFIX = os.path.dirname(PREFIX)

from bottle import mako_view as view, mako_template as template

def url(url):
  host = '://'.join(bottle.request.urlparts[:2])
  if url and url[0] != '/':
    host += '/'
  return host + url

def find_route(name, **params):
  for route in bottle.request.app.routes:
    if route.name == name:
      return route.rule

def detect_os():
  agent = bottle.request.environ.get('HTTP_USER_AGENT')
  os = httpagentparser.detect(agent)
  if not os:
    os = agent.split('/')[0]
  else:
    os = os['os']['name']
  return os

def view(name):
  def res(f):
    lookup = '%s/share/infinit/website/templates' % PREFIX
    return bottle.mako_view(
      name,
      template_lookup = [lookup],
      request = bottle.request,
      response = bottle.response,
      route = find_route,
      url = url,
      os = detect_os,
      version = version,
      tarball_version = tarball_version,
    )(f)
  return res

def static_file(path):
  return bottle.static_file(
    path, root = '%s/share/infinit/website/resources' % PREFIX)

class Routes:

  def __init__(self):
    self.__routes = []

  def __call__(self, *args, **kwargs):
    def res(f):
      self.__routes.append((f, args, kwargs))
      return f
    return res

  def apply(self, bottle):
    for f, args, kwargs in self.__routes:
      bottle.route(*args, **kwargs)(functools.partial(f, self = bottle))
route = Routes()

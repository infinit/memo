import bottle
import os
import os.path
import sys

ROOT = os.path.dirname(os.path.realpath(__file__))
PREFIX = ROOT
for i in range(4):
  PREFIX = os.path.dirname(PREFIX)

from bottle import mako_view as view, mako_template as template

def view(name):
  def res(f):
    lookup = '%s/share/infinit/website/templates' % PREFIX
    print(lookup)
    return bottle.mako_view(name, template_lookup = [lookup])(f)
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
    return res

  def apply(self, bottle):
    for f, args, kwargs in self.__routes:
      bottle.route(*args, **kwargs)(f)
route = Routes()

import bottle
import infinit.website

bottle.DEBUG = True
bottle.run(app = infinit.website.Website(), host = '0.0.0.0', reloader = True)

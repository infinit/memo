#!/usr/bin/env python3

import argparse
import mako
import os
import subprocess

from utils.nginx_conf import NGINXConfig
from utils.tools import *

def parse_options():
  parser = argparse.ArgumentParser(description = 'Infinit Hub Server')
  parser.add_argument(
    '--server-name',
    type = str,
    help = 'Server hostname',
    required = True,
  )
  parser.add_argument(
    '--ssl-certificate',
    type = str,
    help = 'SSL certificate file name',
  )
  parser.add_argument(
    '--ssl-certificate-key',
    type = str,
    help = 'SSL certificate key file name',
  )
  parser.add_argument(
    '--ssl-client-certificate',
    type = str,
    help = 'SSL client certificate file name',
  )
  parser.add_argument(
    '--ldap-server',
    type = str,
    help = 'Hostname or URL of the ldap server')
  parser.add_argument(
    '--admin-users',
    type = str,
    nargs = '+',
    help = 'Comma-separated list of admin users')
  parser.add_argument(
    '--keep-deleted-users',
    type = bool,
    default = False,
    help = 'Retain data from deleted users')
  parser.add_argument(
    '--disable-logs',
    action = 'store_true',
    help = 'Disable all logging',
  )

  return parser.parse_args()

args = parse_options()
disable_logs = args.disable_logs
ssl = True if args.ssl_certificate else False

log_root = '/var/log/beyond'
log_couchdb = '%s/couchdb' % log_root
log_nginx = '%s/nginx' % log_root
log_uwsgi = '%s/uwsgi' % log_root

if not disable_logs:
  def _enforce_log_dir(dir):
    if os.path.exists(dir) and not os.path.isdir(dir):
      raise Exception('file exists where there should be a folder: %s' % dir)
    if not os.path.isdir(dir):
      os.makedirs(dir)
  _enforce_log_dir(log_couchdb)
  _enforce_log_dir(log_nginx)
  _enforce_log_dir(log_uwsgi)

nginx_options = [
  'server_name',
  'ssl_certificate',
  'ssl_certificate_key',
  'ssl_client_certificate',
]
script_options = [
  'disable_logs',
]

beyond_exceptions = [] \
  + nginx_options \
  + script_options

print('Starting beyond...')

render_file('/scripts/templates/couchdb.ini.tmpl',
            '/etc/couchdb/local.d/beyond.ini', {
  'log_file': '/dev/null' if disable_logs else '%s/log' % log_couchdb,
  'log_level': 'log',
})

uwsgi_conf = '/etc/uwsgi/apps-enabled/beyond.json'
render_file('/scripts/templates/uwsgi.json.tmpl',
            uwsgi_conf, {
  'log_file': '/dev/null' if disable_logs else '%s/log' % log_uwsgi,
  'options': beyond_opts_from_args(args, beyond_exceptions),
})

nginx_conf = '/etc/nginx/sites-enabled/beyond'
with open(nginx_conf, 'w') as f:
  nginx_server_conf = NGINXConfig(
    server_name = args.server_name,
    listen = 80,
    log_folder = None if disable_logs else log_nginx,
  )
  print(nginx_server_conf, file = f)
  if args.ssl_certificate:
    cert_folder = '/etc/nginx/certs'
    client_cert_path = '%s/%s' % (cert_folder, args.ssl_client_certificate) \
      if args.ssl_client_certificate else None
    nginx_server_ssl_conf = NGINXConfig(
      server_name = args.server_name,
      listen = 443,
      log_folder = None if disable_logs else log_nginx,
      ssl_certificate = '%s/%s' % (cert_folder, args.ssl_certificate),
      ssl_certificate_key = '%s/%s' % (cert_folder, args.ssl_certificate_key),
      ssl_client_certificate = client_cert_path,
    )
    print(nginx_server_ssl_conf, file = f)

couchdb_pid = '/run/couchdb/pid'
if os.path.exists(couchdb_pid):
  os.remove(couchdb_pid)
couchdb_uri = '/run/couchdb/uri'
if os.path.exists(couchdb_uri):
  os.remove(couchdb_uri)
uwsgi_pid = '/run/uwsgi/app/beyond/pid'
if os.path.exists(uwsgi_pid):
  os.remove(uwsgi_pid)

try:
  couchdb_cmd = [
    '/usr/bin/couchdb', '-b',
    '-p', couchdb_pid,
    '-o', '/dev/null' if disable_logs else '%s/stdout' % log_couchdb,
    '-e', '/dev/null' if disable_logs else '%s/stderr' % log_couchdb,
  ]
  subprocess.check_output(couchdb_cmd)
  wait_service(couchdb_uri, 'CouchDB')
  subprocess.run([
    '/usr/bin/uwsgi',
    '--json', uwsgi_conf,
  ])
  wait_service(uwsgi_pid, 'uWSGI')
  subprocess.run(['/usr/sbin/nginx'])
except KeyboardInterrupt as e:
  print('Stopping...')
finally:
  if os.path.exists(uwsgi_pid):
    subprocess.run(['uwsgi', '--stop', uwsgi_pid])
  if os.path.exists(couchdb_pid):
    subprocess.run(['couchdb', '-d', '-p', couchdb_pid])

print('Stopped')

# TODO:
# x Handle passing options to beyond and bottle
# x Ensure LDAP works
# - Drake docker file generator should keep order
# - Add simple emailer (required for verifying email address)
# - Ensure avatars work (perhaps store them locally if no GCS is given)
# - External CouchDB

# python3 /scripts/entry.py --server-name beyond.infinit.sh --ssl-certificate server.crt --ssl-certificate-key server.key --ldap-server ldap-server

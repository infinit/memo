#!/usr/bin/env python3

# Script used to launch a Beyond server.
# eg:
# python3 /scripts/entry.py --server-name beyond.infinit.sh --ssl-certificate server.crt --ssl-certificate-key server.key --ldap-server ldap-server

import argparse
import mako
import os
import signal
import subprocess
import sys

from utils.nginx_conf import NGINXConfig
from utils.tools import *

couchdb_pid = '/run/couchdb/pid'
if os.path.exists(couchdb_pid):
  os.remove(couchdb_pid)
uwsgi_pid = '/run/uwsgi/app/beyond/pid'
if os.path.exists(uwsgi_pid):
  os.remove(uwsgi_pid)

def signal_handler(signum, frame):
  print('Stopping...')
  if os.path.exists(uwsgi_pid):
    subprocess.run(['uwsgi', '--stop', uwsgi_pid])
  if os.path.exists(couchdb_pid):
    subprocess.run(['couchdb', '-d', '-p', couchdb_pid])
  print('Stopped')
  sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

def parse_options():
  parser = argparse.ArgumentParser(
    add_help = False,
    prog = 'python3 /scripts/entry.py',
    description = 'Infinit Hub Server',
  )
  parser.add_argument(
    '-h', '--help',
    action = 'help',
    default = argparse.SUPPRESS,
    help = 'Show this help message and exit',
  )
  parser.add_argument(
    '--server-name',
    type = str,
    help = 'Server host name',
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
    help = argparse.SUPPRESS,#'SSL client certificate file name',
  )
  parser.add_argument(
    '--ldap-server',
    type = str,
    help = 'Hostname or URL of the ldap server',
  )
  parser.add_argument(
    '--admin-users',
    type = str,
    nargs = '+',
    help = argparse.SUPPRESS,#'List of admin users',
  )
  parser.add_argument(
    '--keep-deleted-users',
    type = bool,
    default = False,
    help = 'Retain data from deleted users',
  )
  parser.add_argument(
    '--disable-logs',
    action = 'store_true',
    help = 'Disable all logging',
  )

  return parser.parse_args()

args = parse_options()
disable_logs = args.disable_logs
ssl = True if args.ssl_certificate else False

log_root = '/log'
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

if ssl:
  cert_folder = '/certificates'
  if not args.ssl_certificate_key:
    raise Exception(
      'Missing certificate key, specify with "--ssl-certificate-key"')
  def _cert_file_path(filename, desc):
    res = '%s/%s' % (cert_folder, filename)
    if not os.path.exists(res):
      raise Exception('%s not found with file name: %s' % (desc, filename))
    return res
  ssl_certificate = _cert_file_path(args.ssl_certificate, 'SSL certificate')
  ssl_certificate_key = \
    _cert_file_path(args.ssl_certificate_key, 'SSL certificate key')
  if args.ssl_client_certificate:
    ssl_client_certificate = _cert_file_path(args.ssl_client_certificate)
  else:
    ssl_client_certificate = None

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

couchdb_uri = '/run/couchdb/uri'
if os.path.exists(couchdb_uri):
  os.remove(couchdb_uri)

render_file('/scripts/templates/couchdb.ini.tmpl',
            '/etc/couchdb/local.d/beyond.ini', {
  'datastore': '/couchdb/db-data',
  'log_file': '/dev/null' if disable_logs else '%s/log' % log_couchdb,
  'log_level': 'log',
  'uri_file': couchdb_uri,
})

uwsgi_socket_path = '/run/uwsgi/app/beyond/socket'

uwsgi_conf = '/etc/uwsgi/apps-enabled/beyond.json'
render_file('/scripts/templates/uwsgi.json.tmpl',
            uwsgi_conf, {
  'log_file': '/dev/null' if disable_logs else '%s/log' % log_uwsgi,
  'options': beyond_opts_from_args(args, beyond_exceptions),
  'pid_file': uwsgi_pid,
  'uwsgi_socket': uwsgi_socket_path,
})

nginx_conf = '/etc/nginx/sites-enabled/beyond'
with open(nginx_conf, 'w') as f:
  nginx_server_conf = NGINXConfig(
    server_name = args.server_name,
    listen = 80,
    log_folder = None if disable_logs else log_nginx,
    uwsgi_socket_path = uwsgi_socket_path,
  )
  print(nginx_server_conf, file = f)
  if args.ssl_certificate:
    client_cert_path = '%s/%s' % (cert_folder, args.ssl_client_certificate) \
      if args.ssl_client_certificate else None
    nginx_server_ssl_conf = NGINXConfig(
      server_name = args.server_name,
      listen = 443,
      log_folder = None if disable_logs else log_nginx,
      ssl_certificate = ssl_certificate,
      ssl_certificate_key = ssl_certificate_key,
      ssl_client_certificate = ssl_client_certificate,
      uwsgi_socket_path = uwsgi_socket_path,
    )
    print(nginx_server_ssl_conf, file = f)

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

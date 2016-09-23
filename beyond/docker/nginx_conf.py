
class NGINXConfig:

  def __init__(self,
               server_name,
               listen = None,
               log_folder = None,
               ssl_certificate = None,
               ssl_certificate_key = None,
               ssl_client_certificate = None,
               uwsgi_socket_path = None,
  ):
    self.__server_name = server_name
    self.__ssl_certificate = ssl_certificate
    self.__ssl = True if ssl_certificate else False
    if listen is None:
      self.__listen = 443 if self.__ssl else 80
    else:
      self.__listen = listen
    self.__ssl_certificate_key = ssl_certificate_key
    self.__ssl_client_certificate = ssl_client_certificate
    self.__log_folder = log_folder
    self.__uwsgi_socket_path = uwsgi_socket_path

  def _log_file(self, log_type):
    if not self.__log_folder:
      return '/dev/null'
    suffix = '-ssl' if self.__ssl else ''
    return '%s/%s%s.log' % (self.__log_folder, log_type, suffix)

  def _ssl_server_text(self):
    if not self.__ssl:
      return ''
    res = 'ssl on;'
    res += '\n  ssl_certificate "%s";' % self.__ssl_certificate
    if self.__ssl_certificate_key:
      res += '\n  ssl_certificate_key "%s";' % self.__ssl_certificate_key
    if self.__ssl_client_certificate:
      res += '\n  ssl_client_certificate "%s";' % self.__ssl_client_certificate
      res += '\n  ssl_verify_client optional;'
    return res

  def _ssl_location_text(self):
    if not self.__ssl:
      return ''
    return '''
    uwsgi_param SSL_CLIENT_VERIFIED $ssl_client_verify;
    uwsgi_param SSL_CLIENT_DN $ssl_client_s_dn;
'''

  def __str__(self):
    return '''server
{
  listen %(port)s;
  server_name %(server_name)s;

  access_log %(access_log)s;
  error_log %(error_log)s;
  client_max_body_size 15M;

  %(ssl_server)s

  location /
  {
    uwsgi_pass unix://%(uwsgi_socket_path)s;
    include uwsgi_params;
    uwsgi_param UWSGI_SCHEME $scheme;
    uwsgi_param SERVER_SOFTWARE nginx/$nginx_version;
    %(ssl_location)s
  }
}

''' % {
  'port': self.__listen,
  'server_name': self.__server_name,
  'access_log': self._log_file('access'),
  'error_log': self._log_file('error'),
  'ssl_server': self._ssl_server_text(),
  'ssl_location': self._ssl_location_text(),
  'uwsgi_socket_path': self.__uwsgi_socket_path,
}

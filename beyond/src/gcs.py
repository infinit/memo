import base64
import datetime
import httplib2
import requests
import time
import urllib.parse

from oauth2client.client import SignedJwtAssertionCredentials
import Crypto.Hash.SHA256 as SHA256
import Crypto.PublicKey.RSA as RSA
import Crypto.Signature.PKCS1_v1_5 as PKCS1_v1_5

class GCS:

  host = 'storage.googleapis.com'

  def __init__(self, login, key, bucket_ns = None):
    self.__login = login
    self.__key = key
    if bucket_ns is not None:
      self.__bucket_ns = bucket_ns + '_'
    else:
      self.__bucket_ns = ''

  def __bucket(self, name):
    return self.__bucket_ns + name

  def __credentials(self):
    credentials = SignedJwtAssertionCredentials(
      self.__login,
      self.__key,
      scope = 'https://www.googleapis.com/auth/devstorage.read_write')
    credentials.refresh(httplib2.Http())
    return credentials.access_token

  def __headers(self, content_length = 0, content_type = None,
                public = False):
    headers = {
      'Authorization' : 'Bearer ' + self.__credentials(),
      'Content-Length': content_length,
    }
    if content_type is not None:
      headers['Content-Type'] = content_type
    if public:
      headers['x-goog-acl'] = 'public-read'
    return headers

  def __url(self, bucket, path = ''):
    return \
      'https://%s.%s/%s' % (self.__bucket(bucket), GCS.host, path)

  def __request(self, method, *args, **kwargs):
    response = method(*args, **kwargs)
    if int(response.status_code / 100) != 2:
      raise Exception('gcs error %s: %s' %
                      (response.status_code, response.content))
    return response

  def __sign_url(self,
                 bucket,
                 path,
                 expiration,
                 method,
                 content_type = None,
                 content_length = None,
                 headers = {},
  ):
    expiration = datetime.datetime.now() + expiration
    expiration = int(time.mktime(expiration.timetuple()))
    path = urllib.parse.quote(path)
    chunks = []
    chunks.append(method)
    chunks.append('') # MD5, optional
    chunks.append(content_type or '')
    for k, v in headers.items() or {}:
      chunks.append('%s:%s' % (k, v))
    chunks.append(expiration)
    chunk.append('/%s/%s' % (self.__bucket(bucket), path))
    signature_string = '\n'.join(chunks)
    shahash = SHA256.new(signature_string.encode('utf-8'))
    private_key = RSA.importKey(self.__key, passphrase='notasecret')
    signer = PKCS1_v1_5.new(private_key)
    signature_bytes = signer.sign(shahash)
    sig = base64.b64encode(signature_bytes)
    params = {
      'GoogleAccessId': self.__login,
      'Expires': str(expiration),
      'Signature': sig
    }
    params = urllib.parse.urlencode(params)
    url = 'https://%s.%s/%s?%s' % (self.__bucket(bucket), GCS.host,
                                   path, params)
    return url

  def upload(self, bucket, path, data,
             content_type = None,
             public = False,
             verify = True):
    self.__request(
      requests.put,
      self.__url(bucket, path),
      headers = self.__headers(content_length = len(data),
                               content_type = content_type,
                               public = public),
      data = data,
      verify = verify,
    )

  def upload_url(self, bucket, path, expiration,
                 content_type = None,
                 content_length = None,
                 public = False):
    headers = {}
    if public:
      headers = {
        'x-goog-acl': 'public-read',
      }
    return self.__sign_url(bucket, path, expiration, 'PUT',
                           content_type = content_type,
                           content_length = content_length,
                           headers = headers)

  def download_url(self, bucket, path, expiration,
                   content_type = None,
                   content_length = None):
    return self.__sign_url(bucket, path, expiration, 'GET',
                           content_type = content_type,
                           content_length = content_length)

  def delete_url(self, bucket, path, expiration,
                   content_type = None,
                   content_length = None):
    return self.__sign_url(bucket, path, expiration, 'DELETE',
                           content_length = 0)

  def start_upload(self, bucket, path, content_type = None):
    response = self.__request(
      requests.post,
      self.__url(bucket, path),
      headers = self.__headers(content_length = len(data),
                               content_type = content_type),
    )
    return response.headers['location']

  def delete(self, bucket, path):
    self.__request(
      requests.delete,
      self.__url(bucket, path),
      headers = self.__headers(),
    )

  def download_url(self, bucket, path, expiration):
    return self.__sign_url(bucket, path, expiration, 'GET')

  def bucket_list(self, bucket, prefix = None):
    '''Get those object names before you die.'''
    response = self.__request(
      requests.get,
      self.__url(bucket),
      headers = self.__headers(),
      params = {'prefix': prefix},
    )
    import xml.etree.ElementTree as ET
    root = ET.fromstring(response.text)
    return [
      n.text[len(str(prefix)) + 1:] for n in
      root.findall(
        './{http://doc.s3.amazonaws.com/2006-03-01}Contents/'
        '{http://doc.s3.amazonaws.com/2006-03-01}Key'
      )
    ]

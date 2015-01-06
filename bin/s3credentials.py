#! /usr/bin/env python3


import binascii
import bson
from datetime import datetime
import time
import hashlib
import hmac
import base64
import json
import urllib.parse
import urllib.request
import xml.etree.ElementTree as etree
import optparse


def _aws_urlquote(data):
  # bit.ly and goo.gl shorteners unencode brackets which breaks the signing.
  return urllib.parse.quote(data, safe='()')

aws_default_region = 'us-east-1'

class CloudBufferToken:

  aws_host = 'sts.amazonaws.com'
  aws_uri = '/'

  default_headers = {
    'content-type': 'application/json',
    'host': aws_host
  }

  common_parameters = {
    'Version': '2011-06-15',
    'X-Amz-Algorithm': 'AWS4-HMAC-SHA256'
  }

  def _aws_urlencode(self, data):
    return urllib.parse.urlencode(data).replace('+', '%20')

  def _list_to_dict(self, data):
    res = {}
    for element in data:
      res[element[0]] = element[1]
    return res

  def __init__(self, user_id, transaction_id, http_action,
               aws_secret,
               aws_id,
               aws_region = None,
               bucket_name = None):
    assert http_action in ['PUT', 'GET', 'ALL']
    self.user_id = user_id
    self.transaction_id = transaction_id
    self.http_action = http_action
    self.aws_region = aws_region
    self.aws_sts_service = 'sts'
    self.aws_s3_service = 's3'
    self.aws_secret = aws_secret
    self.aws_id = aws_id

    if aws_region is None:
      aws_region = aws_default_region
    if bucket_name is None:
      bucket_name = aws_default_bucket
    self.bucket_name = bucket_name

  def generate_s3_token(self):
    self.request_time = datetime.utcnow()
    self.headers = self._make_headers()
    self.key = self._make_key(self.aws_secret, self.aws_sts_service)
    self.parameters = self._make_parameters()
    self.request_url = self._generate_url()

    headers_dict = self._list_to_dict(self.headers)
    request = urllib.request.Request(self.request_url, headers=headers_dict)
    try:
      res_xml = urllib.request.urlopen(request).read()
      self.credentials = self._get_credentials(res_xml)
    except urllib.error.HTTPError as e:
      print('%s: unable to fetch token (%s): %s' % (self, e, e.read()))
      raise e
    except urllib.error.URLError as e:
      print('%s: unable to fetch token with %s: %s' % (request, self, e))
      raise e
    return self.credentials

  def _get_credentials(self, xml_str):
    root = etree.fromstring(xml_str)
    aws_xml_ns = '{https://%s/doc/%s/}' % (
      CloudBufferToken.aws_host, CloudBufferToken.common_parameters['Version'])
    search_str = '%sGetFederationTokenResult/%s' % (aws_xml_ns, aws_xml_ns)
    credentials = {}
    credentials['SessionToken'] = root.find(
      '%sCredentials/%sSessionToken' % (search_str, aws_xml_ns)).text
    credentials['SecretAccessKey'] = root.find(
      '%sCredentials/%sSecretAccessKey' % (search_str, aws_xml_ns)).text
    credentials['Expiration'] = root.find(
      '%sCredentials/%sExpiration' % (search_str, aws_xml_ns)).text
    credentials['AccessKeyId'] = root.find(
      '%sCredentials/%sAccessKeyId' % (search_str, aws_xml_ns)).text
    credentials['FederatedUserId'] = root.find(
      '%sFederatedUser/%sFederatedUserId' % (search_str, aws_xml_ns)).text
    credentials['Arn'] = root.find(
      '%sFederatedUser/%sArn' % (search_str, aws_xml_ns)).text
    return credentials


  # http://docs.aws.amazon.com/STS/latest/UsingSTS/sts-controlling-feduser-permissions.html
  #http://docs.aws.amazon.com/AmazonS3/latest/dev/UsingResOpsConditions.html
  def _make_policy(self):
    object_actions = []
    bucket_actions = None
    if self.http_action == 'PUT':
      object_actions.extend(['s3:GetObject', 's3:PutObject', 's3:ListMultipartUploadParts', 's3:AbortMultipartUpload'])
      bucket_actions = ['s3:ListBucket']
    elif self.http_action == 'GET':
      object_actions.extend(['s3:GetObject', 's3:DeleteObject'])
      bucket_actions = ['s3:ListBucket']
    elif self.http_action == 'ALL':
      object_actions.extend(['s3:DeleteObject', 's3:GetObject', 's3:PutObject', 's3:ListMultipartUploadParts', 's3:AbortMultipartUpload'])
      bucket_actions = ['s3:ListBucket']

    object_statement = {
      'Effect': 'Allow',
      'Action': object_actions,
      'Resource': 'arn:aws:s3:::%s/%s/*' % (self.bucket_name,
                                            self.transaction_id)
    }
    bucket_statement = None
    if bucket_actions:
      bucket_statement = {
        'Effect': 'Allow',
        'Action': bucket_actions,
        'Resource': 'arn:aws:s3:::%s' % (self.bucket_name)
      }
    statements = [object_statement]
    if bucket_statement:
      statements.extend([bucket_statement])
    policy = {
      'Version': '2012-10-17',
      'Statement': statements
    }
    return policy

  def _make_headers(self):
    headers = CloudBufferToken.default_headers.copy()
    headers['x-amz-date'] = self.request_time.strftime('%Y%m%dT%H%M%SZ')
    headers = sorted(headers.items())
    return headers

  def _make_headers_str(self):
    headers_str = ''
    for key, value in self.headers:
      headers_str += '%s:%s\n' % (key, value)
    return headers_str[:-1]

  def _make_signed_headers_str(self):
    signed_headers = ''
    for key, value in self.headers:
      signed_headers += '%s;' % key
    return signed_headers[:-1]

  # http://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
  def _make_request(self, method, uri, parameters):
    request = '%s\n%s\n%s\n%s\n\n%s\n%s' % (
      method,                                          # HTTP Method.
      uri,                       # Canonical URI.
      self._aws_urlencode(parameters),           # Canonical Query String.
      self._make_headers_str(),                       # Canonical Headers.
      self._make_signed_headers_str(),                # Signed Headers.
      hashlib.sha256(''.encode('utf-8')).hexdigest(), # Payload Hash.
    )
    return request

  # http://docs.aws.amazon.com/general/latest/gr/sigv4-create-string-to-sign.html
  def _make_string_to_sign(self, request, aws_service):
    res = '%s\n%s\n%s\n%s' % (
      'AWS4-HMAC-SHA256',                               # Algorithm.
      self.request_time.strftime('%Y%m%dT%H%M%SZ'),     # Timestamp.
      '%s/%s/%s/%s' % (                                 # Credential Scope.
        self.request_time.strftime('%Y%m%d'),   # Date.
        self.aws_region,                        # AWS Region.
        aws_service,                       # AWS Service.
        'aws4_request',                         # AWS Request Type.
      ),
      hashlib.sha256(request.encode('utf-8)')).hexdigest(), # Canonical Request Hash.
    )
    return res

  def _make_initial_parameters(self, token, aws_service):
    parameters = CloudBufferToken.common_parameters.copy()
    parameters['X-Amz-Credential'] = '%s/%s/%s/%s/%s' % (
      token,
      self.request_time.strftime('%Y%m%d'),
      self.aws_region,
      aws_service,
      'aws4_request',
    )
    parameters['X-Amz-Date'] = self.request_time.strftime('%Y%m%dT%H%M%SZ')
    parameters['X-Amz-SignedHeaders'] = self._make_signed_headers_str()
    return parameters

  # http://stackoverflow.com/questions/12092518/signing-amazon-getfederationtoken-in-python
  # The above code is python2 and uses the old signing method so had to be adjusted
  def _make_parameters(self):
    parameters = self._make_initial_parameters(self.aws_id,
                                               self.aws_sts_service)
    parameters['Action'] = 'GetFederationToken'
    parameters['DurationSeconds'] = str(36 * 60 * 60) # 36 hrs is AWS max.
    parameters['Policy'] = json.dumps(self._make_policy())
    parameters['Name'] = self.user_id
    parameters = sorted(parameters.items())
    return parameters

  def _aws_sign(self, key, message):
    return hmac.new(key, message.encode('utf-8'), hashlib.sha256).digest()

  # http://docs.aws.amazon.com/general/latest/gr/sigv4-calculate-signature.html
  def _make_key(self, secret, aws_service):
    key_str = 'AWS4%s' % secret
    k_date = self._aws_sign(bytes(key_str, 'utf-8'), self.request_time.strftime('%Y%m%d'))
    k_region = self._aws_sign(k_date, self.aws_region)
    k_service = self._aws_sign(k_region, aws_service)
    k_signing = self._aws_sign(k_service, 'aws4_request')
    return k_signing

  # http://docs.aws.amazon.com/general/latest/gr/sigv4-calculate-signature.html
  def _generate_url(self):
    parameters = self.parameters
    string_to_sign = self._make_string_to_sign(
      self._make_request(uri = CloudBufferToken.aws_uri,
                         method = 'GET', parameters = parameters),
      self.aws_sts_service)
    signed_request = binascii.hexlify(
      self._aws_sign(self.key, string_to_sign))
    completed_request = '%s&X-Amz-Signature=%s' % (
      self._aws_urlencode(parameters),
      urllib.parse.quote(signed_request),
    )
    url_string = 'https://%s:443?%s' % (CloudBufferToken.aws_host, completed_request)
    return url_string

usage = "usage: %prog [options]"
parser = optparse.OptionParser(usage = usage)
parser.add_option('-f', '--folder',
                  action='store',
                  help='name of the folder to give access to'
                  )
parser.add_option('-b', '--bucket',
                  action='store',
                  help='Name of the bucket to give access to'
                  )
parser.add_option('-i', '--aws-id',
                  action='store',
                  help='AWS credentials ID'
                  )
parser.add_option('-s', '--aws-secret',
                  action='store',
                  help='AWS credentials secret'
                  )
parser.add_option('-r', '--region',
                  action='store',
                  help='AWS region',
                  default=aws_default_region
                  )
options, args = parser.parse_args()

if not options.folder or not options.aws_secret or not options.aws_id or not options.bucket:
  print('Missing a required option, use --help for help')
  exit(0)

cbt = CloudBufferToken('user_id', options.folder, 'ALL',
                       options.aws_secret, options.aws_id,
                       options.region, options.bucket)


token = cbt.generate_s3_token()

amazon_time_format = '%Y-%m-%dT%H-%M-%SZ'
now = time.gmtime()
current_time = time.strftime(amazon_time_format, now)

credentials = dict()
credentials['access_key_id']     = token['AccessKeyId']
credentials['secret_access_key'] = token['SecretAccessKey']
credentials['session_token']     = token['SessionToken']
credentials['expiration']        = token['Expiration']
credentials['protocol']          = 'aws'
credentials['region']            = options.region
credentials['bucket']            = options.bucket
credentials['folder']            = options.folder
credentials['current_time']      = current_time
print(json.dumps(credentials))
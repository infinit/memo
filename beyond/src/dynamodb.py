import infinit.beyond

import boto3
import botocore.exceptions
import json
import random
import string

class DynamoDB:

  def __init__(self, table = None, region = None):
    self.__region = region
    self.__tablename = table
    self.__delete = False

  def __enter__(self):
    self.__aws = boto3.session.Session(
      region_name = self.__region,
    )
    self.__client = self.__aws.client('dynamodb')
    self.__dynamodb = self.__aws.resource('dynamodb')
    if self.__tablename is None:
      self.__delete = True
      rn = ''.join(random.choice(string.ascii_uppercase)
                   for _ in range(8))
      self.__tablename = 'beyond-test-%s' % rn
      self.__client.create_table(
        TableName = self.__tablename,
        AttributeDefinitions = [
          {
            'AttributeName' : 'type',
            'AttributeType' : 'S'
          },
          {
            'AttributeName' : 'name',
            'AttributeType' : 'S'
          }
        ],
        KeySchema = [
          { 'AttributeName': 'type' , 'KeyType': 'HASH' },
          { 'AttributeName': 'name' , 'KeyType': 'RANGE' }
        ],
        ProvisionedThroughput = {
          'ReadCapacityUnits' : 1,
          'WriteCapacityUnits' : 1
        })
      try:
        waiter = self.__client.get_waiter('table_exists')
        waiter.wait(TableName = self.__tablename)
      except:
        self.__delete_table()
        raise
    self.__table = self.__dynamodb.Table(self.__tablename)
    return self.__table

  def __exit__(self, *args, **kwargs):
    if self.__delete:
      self.__delete_table()

  def __delete_table(self):
    self.__client.delete_table(TableName = self.__tablename)

  @property
  def table(self):
    return self.__table

class DynamoDBDatastore:

  def __init__(self, table):
    self.__table = table

  def __purge_json(self, o):
    del o['type']
    return o

  def __augment_json(self, o, t):
    o['type'] = t

  def __put_duplicate(self, json, Exn):
    try:
      # DynamoDB does not accept empty strings. Yeah, WTF.
      def purge(json):
        for k in list(json):
          v = json[k]
          if v == '':
            del json[k]
          elif isinstance(v, dict):
            purge(json[k])
      purge(json)
      self.__table.put_item(
        Item = json,
        Expected = {'id': {'Exists': False}},
      )
    except botocore.exceptions.ClientError as e:
      if e.response['Error']['Code'] == 'ConditionalCheckFailedException':
        raise Exn()
      else:
        raise

  def __delete(self, name, t):
    self.__table.delete_item(Key = {'type': t, 'name': name})

  ## ---- ##
  ## User ##
  ## ---- ##

  def user_insert(self, user):
    json = user.json(private = True,
                     hide_confirmation_codes = False)
    self.__augment_json(json, 'users')
    self.__put_duplicate(json, infinit.beyond.User.Duplicate)

  def users_fetch(self):
    return (
      self.__purge_json(u) for u in
      self.__table.query(
        KeyConditionExpression =
          boto3.dynamodb.conditions.Key('type').eq('users'))['Items']
      )

  def user_fetch(self, name):
    user = self.__table.get_item(
      Key = {'type': 'users', 'name': name}).get('Item')
    if user is not None:
      return self.__purge_json(user)
    else:
      raise infinit.beyond.User.NotFound()

  def users_by_email(self, email):
    raise NotImplementedError()

  def user_update(self, id, diff = {}):
    raise NotImplementedError()

  def user_delete(self, name):
    self.__delete(name, 'users')

  def invitee_networks_fetch(self, invitee):
    raise NotImplementedError()

  def owner_networks_fetch(self, owner):
    raise NotImplementedError()

  def user_networks_fetch(self, user):
    networks = self.__table.query(
      KeyConditionExpression =
        boto3.dynamodb.conditions.Key('type').eq('networks'))['Items']
    import sys
    for network in networks:
      if network['name'].split('/')[0] == user.name:
        yield self.__purge_json(network)

  def network_stats_fetch(self, network):
    raise NotImplementedError()

  ## ------- ##
  ## Pairing ##
  ## ------- ##

  def pairing_insert(self, pairing_information):
    raise NotImplementedError()

  def pairing_fetch(self, owner):
    raise NotImplementedError()

  def pairing_delete(self, owner):
    raise NotImplementedError()

  ## ------- ##
  ## Network ##
  ## ------- ##

  def network_insert(self, network):
    json = network.json()
    self.__augment_json(json, 'networks')
    # Inserting that dictionary if absent is a PAIN with
    # DynamoDB. Always include it.
    json.setdefault('endpoints', {})
    self.__put_duplicate(json, infinit.beyond.Network.Duplicate)

  def network_fetch(self, owner, name):
    id = '/'.join([owner, name])
    network = self.__table.get_item(
      Key = {'type': 'networks', 'name': id}).get('Item')
    if network is not None:
      return self.__purge_json(network)
    else:
      raise infinit.beyond.Network.NotFound()

  def network_delete(self, owner, name):
    raise NotImplementedError()

  def network_update(self, id, diff):
    # import sys
    # print(diff, file = sys.stderr)
    aset = []
    adelete = []
    names = {}
    values = {}
    count = 0
    for user, passport in diff.get('passports', {}).items():
      count += 1
      names['#passport_name_%s' % count] = user
      if passport is None:
        adelete.append('passports.#passport_name_%s' % user)
      else:
        values[':passport_%s' % count] = passport
        aset.append('passports.#passport_name_%s = :passport_%s' %
                    (count, count))
    count = 0
    for user, node in diff.get('endpoints', {}).items():
      count += 1
      names['#user_%s' % count] = user
      values[':node_%s' % (count)] = node
      aset.append('endpoints.#user_%s = :node_%s' %
                  (count, count))
      # FIXME: endpoints deletion
    # FIXME: storage
    for field in ['passports', 'endpoints', 'storages']:
      if field in diff:
        del diff[field]
    count = 0
    for k, v in diff.items():
      count += 1
      names['#attr_%s' % count] = k
      values[':attr_%s' % count] = v
      aset.append('#attr_%s = :attr_%s' % (count, count))
    # print(aset, adelete, names, values, file = sys.stderr)
    expr = []
    if aset:
      expr.append('SET %s' % ','.join(aset))
    if adelete:
      expr.append('DELETE %s' % ','.join(adelete))
    # print(expr, file = sys.stderr)
    # print(names, file = sys.stderr)
    # print(values, file = sys.stderr)
    # print({'id': id}, file = sys.stderr)
    if expr:
      kwargs = {
        'Key': {'type': 'networks', 'name': id},
        'UpdateExpression': ''.join(expr),
      }
      if names:
        kwargs['ExpressionAttributeNames'] = names
      if values:
        kwargs['ExpressionAttributeValues'] = values
      self.__table.update_item(**kwargs)


  def networks_volumes_fetch(self, networks):
    volumes = self.__table.query(
      KeyConditionExpression =
        boto3.dynamodb.conditions.Key('type').eq('volumes'))['Items']
    import sys
    for n in networks:
      for volume in volumes:
        if volume['network'] == n.name:
          yield self.__purge_json(volume)

  ## ------ ##
  ## Volume ##
  ## ------ ##

  def volume_insert(self, volume):
    json = volume.json()
    self.__augment_json(json, 'volumes')
    self.__put_duplicate(json, infinit.beyond.Volume.Duplicate)

  def volume_fetch(self, owner, name):
    id = '/'.join([owner, name])
    volume = self.__table.get_item(
      Key = {'type': 'volumes', 'name': id}).get('Item')
    if volume is not None:
      return self.__purge_json(volume)
    else:
      raise infinit.beyond.Volume.NotFound()

  def volume_delete(self, owner, name):
    raise NotImplementedError()

  ## ----- ##
  ## Drive ##
  ## ----- ##

  def drive_insert(self, drive):
    raise NotImplementedError()

  def drive_fetch(self, owner, name):
    raise NotImplementedError()

  def drive_update(self, id, diff):
    raise NotImplementedError()

  def drive_delete(self, owner, name):
    raise NotImplementedError()

  def user_drives_fetch(self, name):
    raise NotImplementedError()

  def network_drives_fetch(self, name):
    raise NotImplementedError()

  def volume_drives_fetch(self, name):
    raise NotImplementedErrorr()

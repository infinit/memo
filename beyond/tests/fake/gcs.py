class FakeGCS:

  def __init__(self):
    self.__store = {}

  def upload(self, bucket, path, *args, **kwargs):
    self.__store[path] = 'url'

  def upload_url(self, bucket, path, *args, **kwargs):
    return bucket + '/' + path

  def delete(self, bucket, path):
    if path in self.__store:
      del self.__store[path]

  def download_url(self, bucket, path, *args, **kwargs):
    return self.__store.get(path,  None)

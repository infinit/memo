class GCS:

  def __init__(self):
    self.__icons = {}

  def upload(self, bucket, path, *args, **kwargs):
    self.__icons[path] = 'url'

  def upload_url(self, bucket, path, *args, **kwargs):
    return bucket + '/' + path

  def delete(self, bucket, path):
    if path in self.__icons:
      del self.__icons[path]

  def download_url(self, bucket, path, *args, **kwargs):
    return self.__icons.get(path,  '')

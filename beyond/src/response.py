class Response(Exception):

  def __init__(self, status = 200, body = None, headers=None):
    self.__status = status
    self.__body = body
    self.__headers = headers

  @property
  def status(self):
    return self.__status

  @property
  def body(self):
    return self.__body

  @property
  def headers(self):
    return self.__headers

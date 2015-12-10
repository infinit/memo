class Response(Exception):

  def __init__(self, status = 200, body = None):
    self.__status = status
    self.__body = body

  @property
  def status(self):
    return self.__status

  @property
  def body(self):
    return self.__body

from benchmark import Benchmark
from common import *

class dd(Benchmark):

  def __init__(self, result_dir, logger, mountpoint, backend, iteration,
               bs = '1M',
               count = 1000):
    super().__init__('dd',
                     result_dir, logger, mountpoint, backend, iteration)
    self.__bs = bs
    self.__count = count
    self.__file = '%s/dd_file' % self.mountpoint

  @property
  def commands(self):
    return [
      [
        'dd',
        'if=/dev/zero',
        'bs=%s' % self.__bs,
        'count=%d' % self.__count,
        'of=%s' % self.__file,
        'conv=fdatasync',
      ],
    ]

  def _execute(self):
    return [run(self.commands[0], wait = False, stderr = self.result_file())]

  def _cleanup(self):
    os.remove(self.__file)

from benchmark import Benchmark
from common import *

class iozone(Benchmark):

  def __init__(self, result_dir, logger, mountpoint, backend, iteration):
    super().__init__('iozone',
                     result_dir, logger, mountpoint, backend, iteration)
    self.__file = '%s/iozone_file' % self.mountpoint

  @property
  def commands(self):
    return [
      [
        'iozone',
        '-a',
        '-b', self.result_file(),
        '-f', self.__file,
      ],
    ]

  def _execute(self):
    return [run(self.commands[0], wait = False, stdout = subprocess.DEVNULL)]

  def _cleanup(self):
    os.remove(self.__file)

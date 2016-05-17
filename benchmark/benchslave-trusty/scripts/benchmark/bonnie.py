from benchmark import Benchmark
from common import *

import shutil

class bonnie(Benchmark):

  def __init__(self, result_dir, logger, mountpoint, backend, iteration,
               processes = 1,
               size = '8192',
               files = 1024,
               bufferring = False):
    super().__init__('bonnie++',
                     result_dir, logger, mountpoint, backend, iteration)
    self.__bufferring = bufferring
    self.__files = files
    self.__processes = processes
    self.__size = size
    self.__working_directory = '%s/bonnie' % self.mountpoint
    os.makedirs(self.__working_directory, exist_ok = True)

  @property
  def commands(self):
    base_cmd = [
      'bonnie++',
      '-d', self.__working_directory,
      '-s', self.__size,
      '-n', str(self.__files),
      '-u', 'root',
    ]
    if self.__bufferring:
      base_cmd.append('-b')
    if self.__processes == 1:
      return [
        base_cmd,
      ]
    else:
      res = [
        base_cmd + ['-p%d' % self.__processes],
      ]
      for i in range(0, processes):
        res.append(['bonnie++', '-y'])
      return res

  def _execute(self):
    res = []
    i = 0
    for c in self.commands:
      res.append(
        run(c,
            wait = False,
            stdout = self.result_file(i if len(self.commands) else None)))
      i += 1
    return res

  def _cleanup(self):
    shutil.rmtree(self.__working_directory)

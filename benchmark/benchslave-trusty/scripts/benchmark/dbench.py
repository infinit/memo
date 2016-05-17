from benchmark import Benchmark
from common import *

import shutil

class dbench(Benchmark):

  def __init__(self, result_dir, logger, mountpoint, backend, iteration,
               processes = 10,
               clients_per_prcess = 1,
               run_time = 300):
    super().__init__('dbench',
                     result_dir, logger, mountpoint, backend, iteration)
    self.__processes = processes
    self.__clients_per_process = clients_per_prcess
    self.__run_time = run_time

  @property
  def commands(self):
    return [
      [
        'dbench', str(self.__processes),
        '--clients-per-process=%d' % self.__clients_per_process,
        '-c', '/usr/share/dbench/client.txt',
        '-t', str(self.__run_time),
        '-D', self.mountpoint,
        '--skip-cleanup',
      ],
    ]

  def _execute(self):
    return [run(self.commands[0], wait = False, stdout = self.result_file())]

  def _cleanup(self):
    shutil.rmtree('%s/clients' % self.mountpoint)

from common import *

import os
import signal
import subprocess
import time

class NotImplementedError(Exception):
  pass

class Benchmark:

  def __init__(self, name, result_dir, logger, mountpoint, backend, iteration,
               processes = 1):
    self.__backend = backend
    self.__iteration = iteration
    self.__logger = logger
    self.__mountpoint = mountpoint
    self.__name = name
    self.__processes = processes
    self.__running = []
    self.__result_dir = os.path.realpath('%s/%s' % (result_dir, self.__name))
    self.__result_files = []
    os.makedirs(str(self.__result_dir), exist_ok = True)

  @property
  def backend(self):
    return self.__backend

  @property
  def iteration(self):
    return self.__iteration

  @property
  def logger(self):
    return self.__logger

  @property
  def mountpoint(self):
    return self.__mountpoint

  @property
  def name(self):
    return self.__name

  @property
  def commands_file(self):
    return '%(dir)s/%(backend)s_commands' % {
      'dir': self.__result_dir,
      'backend': self.backend.lower(),
    }

  def result_file(self, process_num = None):
    name = '%(dir)s/%(backend)s_%(iteration)s' % {
      'dir': self.__result_dir,
      'backend': self.backend.lower(),
      'iteration': self.iteration,
    }
    if process_num:
      name += '_%d' % process_num
    res = open(name, 'w')
    self.__result_files.append(res)
    return res

  @property
  def commands(self):
    # Override
    raise NotImplementedError('must implement "commands"')

  def _execute(self):
    # Override
    raise NotImplementedError('must implement "_execute"')

  def _cleanup(self):
    # Override
    raise NotImplementedError('must implement "_cleanup"')

  def __close_files(self):
    for f in self.__result_files:
      f.close()

  def start(self):
    self.logger.info('Run %d of %s on %s' % \
                     (self.iteration, self.name, self.backend))
    # Write the commands we're going to run to file
    with open(self.commands_file, 'w') as f:
      if len(self.commands) == 1:
        f.write('%s\n' % ' '.join(self.commands[0]))
      else:
        i = 0
        for c in self.commands:
          f.write('%d: %s\n' % (i, ' '.join(c)))
          i += 1
    # Start the processes
    self.__running_processes = self._execute()
    pids = set(p.pid for p in self.__running_processes)
    # Wait for them all to finish
    while len(pids):
      try:
        pid, _ = os.wait()
        pids.remove(pid)
      except (InterruptedError, SystemExit):
        # We got killed
        return
    self.__close_files()
    self._cleanup()

  def kill(self):
    self.logger.info('Killing %s' % self.name)
    for p in self.__running:
      p.send_signal(signal.SIGINT)
      p.wait()
    self.__close_files()
    try:
      self._cleanup()
    except:
      pass

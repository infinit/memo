import copy
import os
import pipes
import signal
import subprocess

class InterruptHandler(object):

  def __init__(self, handler, sig = signal.SIGINT):
    self.__sig = sig
    self.__handler = handler

  def __enter__(self):
    self.__interrupted = False
    self.__released = False
    self.__original_handler = signal.getsignal(self.__sig)
    signal.signal(self.__sig, self.__handler)
    return self

  def __exit__(self, type, value, tb):
    self._release()

  def _release(self):
    if self.__released:
      return False
    signal.signal(self.__sig, self.__original_handler)
    self.__released = True
    return True

def run(args,
        env = {},
        input = None,
        return_code = 0,
        wait = True,
        stdout = subprocess.PIPE,
        stderr = subprocess.PIPE,
        write_to_file = None):
  if env.get('PATH') is None:
    env['PATH'] = os.environ['PATH']
  if len(args) == 0:
    raise Exception('nothing to run')
  if isinstance(args, str):
    raise Exception('arguments must be a list')
  pretty = '%s %s' % (
    ' '.join('%s=%s' % (k, v) for k, v in env.items()),
    ' '.join(pipes.quote(arg) for arg in args))
  if input is not None:
    if isinstance(input, list):
      input = '\n'.join(map(json.dumps, input)) + '\n'
    elif isinstance(input, dict):
      input = json.dumps(input) + '\n'
    pretty = 'echo %s | %s' % (
      pipes.quote(input.strip()), pretty)
    input = input.encode('utf-8')
  if write_to_file:
    with open(write_to_file, 'w') as f:
      f.write('%s\n' % pretty)
  process = subprocess.Popen(
    args,
    env = env if env else None,
    stdin = subprocess.PIPE,
    stdout = stdout,
    stderr = stderr,
  )
  if not wait:
    return process
  out, err = process.communicate(input)
  process.wait()
  if process.returncode != return_code:
    reason = err.decode('utf-8')
    raise Exception('command failed with code %s: %s (reason: %s)' % \
                    (process.returncode, pretty, reason))
  if stdout != subprocess.PIPE:
    return
  out = out.decode('utf-8')
  try:
    return json.loads(out)
  except:
    _out = []
    for line in out.split('\n'):
      if len(line) == 0:
        continue
      try:
        _out.append(json.loads(line))
      except:
        _out.append('%s' % line);
    return _out

def install_infinit(version):
  package = 'infinit_%s_amd64.deb' % version
  package_path = '/tmp/%s' % package
  if os.path.exists(package_path):
    os.remove(package_path)
  run(['scp', 'debian@debian.infinit.io:repository/%s' % package, package_path])
  try:
    run(['dpkg', '-r', 'infinit'])
  except:
    pass
  run(['dpkg', '-i', package_path])
  os.remove(package_path)

import json
import pipes
import shutil
import subprocess
import tempfile
import time

class TemporaryDirectory:

  def __init__(self):
    self.__dir = None

  def __enter__(self):
    self.__dir = tempfile.mkdtemp()
    return self

  def __exit__(self, *args, **kwargs):
    shutil.rmtree(self.__dir)

  @property
  def dir(self):
    return self.__dir


class Infinit(TemporaryDirectory):

  def run(self, args, input = None, return_code = 0):
    env = {
      'PATH': 'bin',
      'INFINIT_HOME': self.dir,
    }
    pretty = '%s %s' % (
      ' '.join('%s=%s' % (k, v) for k, v in env.items()),
      ' '.join(pipes.quote(arg) for arg in args))
    print(pretty)
    if input is not None:
      input = (json.dumps(input) + '\n').encode('utf-8')
    process = subprocess.Popen(
      args + ['-s'],
      env = env,
      stdin =  subprocess.PIPE,
      stdout =  subprocess.PIPE,
      stderr =  subprocess.PIPE,
    )
    if input is not None:
      # FIXME: On OSX, if you spam stdin before the FDStream takes it
      # over, you get a broken pipe.
      time.sleep(0.5)
    out, err = process.communicate(input)
    if process.returncode != return_code:
      raise Exception('command failed with code %s: %s' % \
                      (process.returncode, pretty))
    try:
      return json.loads(out.decode('utf-8'))
    except:
      return None

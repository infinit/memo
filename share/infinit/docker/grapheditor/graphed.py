import bottle
import os
import shutil

class Bottle(bottle.Bottle):
  def __init__(
      self,
      path):
    super().__init__(catchall = True)
    self.__path = path
    self.route('/dotfile', method='GET')(self.dot_get)
    self.route('/update', method='POST')(self.dot_update)
    self.route('/main.html', method='GET')(self.main)
    self.route('/viz.js', method='GET')(self.viz)
  def main(self):
    with open('main.html', 'r') as f:
      return f.read()
  def viz(self):
    with open('viz.js', 'r') as f:
      return f.read()
  def dot_get(self):
    dot = 'graph G {\n'
    toplevel = os.listdir(self.__path)
    for f in toplevel:
      dot += f + '['
      attrs = os.listdir(os.path.join(self.__path, f))
      for a in attrs:
        print('%s / %s' %(f, a))
        with open(os.path.join(self.__path, f, a), 'r') as fi:
          dot += a + '=' + fi.read().replace('\n', '') +','
      dot += '];\n'
    dot += '\n}'
    return dot
  def dot_update(self):
    cmd = bottle.request.POST['data']
    fields = cmd.split(' ')
    if len(fields) == 1:
      assert fields[0].find('/') == -1
      os.mkdir(os.path.join(self.__path, fields[0]))
    else:
      assert len(fields) == 3
      assert fields[0].find('/') == -1
      assert fields[1].find('/') == -1
      if fields[1] == 'rm':
        shutil.rmtree(os.path.join(self.__path, fields[0]))
      else:
        target = os.path.join(self.__path, fields[0], fields[1])
        if fields[2] == 'rm':
          os.remove(target)
        else:
          with open(target, 'w') as f:
            f.write(fields[2])
    return 'OK'

try:
  os.mkdir('/tmp/gw')
except:
  pass

app = Bottle('/tmp/gw')
bottle.run(app = app, port=8081, host='0.0.0.0')
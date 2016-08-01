import astroid
from pylint.checkers import BaseChecker
from pylint.interfaces import IAstroidChecker

def register(linter):
  linter.register_checker(BeyondChecker(linter))

# def debug(o):
#   print('\n'.join('%s = %r' % (k, getattr(o, k)) for k in dir(o)))

class BeyondChecker(BaseChecker):

  msgs = {
    'E4200': ('Invalid beyond HTTP error response: %s',
              'beyond-bad-error',
              'Used when a beyond error is not JSON with error and '
              'reason fields.'),
    }
  __implements__ = (IAstroidChecker,)

  name = 'beyond'

  def visit_callfunc(self, node):
    """Called for every function call in the source code."""
    if not isinstance(node.func, astroid.Name):
      # It isn't a simple name, can't deduce what function it is.
      return
    if node.func.name == 'Response':
      if len(node.args) not in [2,3]:
        self.add_message('E4200',
                         args = 'Response not passed two arguments',
                         node = node)
        return
      try:
        code = node.args[0]
        value = code
        if not isinstance(code, astroid.Const):
          raise Exception('status code is not a literal integer')
        elif not isinstance(code.value, int):
          raise Exception('status code is not an integer')
        else:
          code = code.value
        value = node.args[1]
        if isinstance(value, astroid.Const) and \
           not isinstance(value, astroid.Dict):
          raise Exception('value is not a dictionary')
        elif not isinstance(value, astroid.Dict):
          raise Exception('value is not a literal dictionary')
        elif code / 100 in [4, 5]:
          mandatory = {'error', 'reason'}
          for item in value.items:
            if not isinstance(item[0], astroid.Const):
              raise Exception('key is not a literal string')
            name = item[0].value
            if name in mandatory:
              mandatory.remove(item[0].value)
            if name == 'error':
              name = item[1]
              if isinstance(name, astroid.BinOp) and name.op == '%':
                name = name.left
              if not isinstance(name, astroid.Const):
                raise Exception(
                  'error name is not a literal string')
              chunks = name.value.split('/')
              if len(chunks) != 2:
                raise Exception(
                  'error name is not \'category/error\'')
          if mandatory:
            raise Exception(
              'missing mandatory keys: %s' % ', '.join(mandatory))
      except Exception as e:
        self.add_message('E4200', args = str(e), node = value)

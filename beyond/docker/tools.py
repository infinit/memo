import json
import os
import time

from mako.template import Template

def beyond_opts_from_args(args, exceptions):
  res = []
  for k, v in vars(args).items():
    if k not in exceptions and v is not None:
      res.append('%s=%s' % (k.replace('_', '-'), repr(v)))
  return json.dumps(res)

def render_file(template, result, data):
  t = Template(filename = template)
  with open(result, 'w') as f:
    print(t.render(**data), file = f)

def wait_service(path, service_name):
  step = 0
  sleep_period = 0.1
  while not os.path.exists(path) and step < 100:
    time.sleep(sleep_period)
    step += 1
  if not os.path.exists(path):
    raise Exception('timeout after %s sec waiting for "%s"' % \
                    (step * sleep_period, service_name))

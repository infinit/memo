Complete API
------------

<h3>Methods</h3>

% for method in proto['service']['rpcs']:

<h4 name="${method['name']}">${method['name']}</h4>

_${method['documentation']['abstract']}_.

**returns**: [${method['returns']}](#${method['returns']}).

**arguments**: ${' '.join(["[%(arg)s](#%(arg)s)" % {"arg": arg} for arg in method['arguments']])}.

  % for doc in ['description', 'related', 'specific']:
    % if method.get('documentation', {}).get(doc):
${method['documentation'][doc]}.
    % endif
  % endfor

% endfor

<h3>Messages</h3>

% for message in proto['messages']:

<h4 name="${message['name']}">${message['name']}</h4>
<table>
  <tr>
    <th width="10%">index</th>
    <th>name</th>
    <th>type</th>
    <th>abstract</th>
  </tr>
  % for attr in message.get('attributes', []):
    <tr>
      % if attr['type'] == 'oneof':
        <td colspan=1>${attr['name']}<td>
        <td>${attr['type']}</td>
        <td>${attr.get('documentation', {}).get('abstract', '')}</td>
        % for subattr in attr['values']:
          <tr>
            <td>${subattr['index']}</td>
            <td>${subattr['name']}</td>
            <td>${subattr['type']}</td>
            <td>${subattr.get('documentation', {}).get('abstract', '')}</td>
          </tr>
        % endfor
      % else:
        <td>${attr['index']}</td>
        <td>${attr['name']}</td>
        <td>${attr['type']}</td>
        <td>${attr.get('documentation', {}).get('abstract', '')}</td>
      % endif
    </tr>
  % endfor
</table>

% endfor
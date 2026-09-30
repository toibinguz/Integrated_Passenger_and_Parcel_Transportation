import re

with open('solver_ls.py', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Fix exhaustive_remove PASSENGER
patt_pass = re.compile(
    r"                      if node\.type == 'PASSENGER':\s*"
    r"                          delta = self\._delta_cost_remove\(route, i, data\)\s*"
    r"                          if delta > 0:\s*"
    r"                              route\.nodes\.pop\(i\)\s*"
    r"                              route\.update_states\(data\)\s*"
    r"                              unserved_pass\.append\(node\)\s*"
    r"                              total_removed \+= 1\s*"
    r"                              route_changed = True\s*"
    r"                              route_changed_ever = True\s*"
    r"                              #.*?\s*"
    r"                              continue\s*"
)

new_pass = '''                      if node.type == 'PASSENGER':
                          delta = self._delta_cost_remove(route, i, data)
                          if delta > 0:
                              test_nodes = list(route.nodes)
                              test_nodes.pop(i)
                              import copy
                              test_route = copy.copy(route)
                              test_route.nodes = test_nodes
                              test_route.update_states(data)
                              if test_route.is_feasible:
                                  route.nodes = test_nodes
                                  route.update_states(data)
                                  unserved_pass.append(node)
                                  total_removed += 1
                                  route_changed = True
                                  route_changed_ever = True
                                  continue
'''

content, n = patt_pass.subn(new_pass, content)
print(f'Replaced PASSENGER {n} times')

# 2. Fix exhaustive_remove PARCEL
patt_parc = re.compile(
    r"                              if test_route\.total_benefit > orig_ben:\s*"
    r"                                  route\.nodes = test_nodes\s*"
    r"                                  route\.update_states\(data\)\s*"
)
new_parc = '''                              if test_route.is_feasible and test_route.total_benefit > orig_ben:
                                  route.nodes = test_nodes
                                  route.update_states(data)
'''

content, n2 = patt_parc.subn(new_parc, content)
print(f'Replaced PARCEL {n2} times')

with open('solver_ls.py', 'w', encoding='utf-8') as f:
    f.write(content)

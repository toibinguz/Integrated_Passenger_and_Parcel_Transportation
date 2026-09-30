import sys

with open('solver_ls.py', 'r', encoding='utf-8') as f:
    lines = f.readlines()

new_lines = []
skip = False
for i, line in enumerate(lines):
    if skip:
        if "continue" in line and "Kh" not in line: # simplistic check to end skip
            skip = False
        continue
    
    if "delta = self._delta_cost_remove(route, i, data)" in line:
        new_lines.append(line)
        new_lines.append("                        if delta > 0:\n")
        new_lines.append("                            test_nodes = list(route.nodes)\n")
        new_lines.append("                            test_nodes.pop(i)\n")
        new_lines.append("                            import copy\n")
        new_lines.append("                            test_route = copy.copy(route)\n")
        new_lines.append("                            test_route.nodes = test_nodes\n")
        new_lines.append("                            test_route.update_states(data)\n")
        new_lines.append("                            if test_route.is_feasible:\n")
        new_lines.append("                                route.nodes = test_nodes\n")
        new_lines.append("                                route.update_states(data)\n")
        new_lines.append("                                unserved_pass.append(node)\n")
        new_lines.append("                                total_removed += 1\n")
        new_lines.append("                                route_changed = True\n")
        new_lines.append("                                route_changed_ever = True\n")
        new_lines.append("                                continue\n")
        
        # skip lines until continue
        skip = True
        continue
        
    if "if test_route.total_benefit > orig_ben:" in line:
        new_lines.append("                            if test_route.is_feasible and test_route.total_benefit > orig_ben:\n")
        continue

    new_lines.append(line)

with open('solver_ls.py', 'w', encoding='utf-8') as f:
    f.writelines(new_lines)

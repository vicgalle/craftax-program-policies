"""Conservative source/interface audit of self-contained controller emitters.

This supplements manual review; it is not a security sandbox or a proof of
behavioral correctness. The independent native monitor remains authoritative.
"""
import ast
import json
from pathlib import Path
import re

from compiler import emitter, parse_goal
from paths import HERE, digest


def audit(path, goals=None):
    path = Path(path).resolve()
    if not path.is_relative_to(HERE):
        raise ValueError('emitter must be inside compiler/')
    text = path.read_text()
    tree = ast.parse(text)
    for node in ast.walk(tree):
        if isinstance(node, ast.Import) and any(x.name not in {'re', 'math'} for x in node.names):
            raise ValueError('only re/math standard-library imports are allowed')
        if isinstance(node, ast.ImportFrom) and node.module not in {'re', 'math'}:
            raise ValueError('only re/math standard-library imports are allowed')
        if isinstance(node, ast.Name) and node.id in {'open', 'exec', 'eval', '__import__', 'globals', 'locals', 'getattr', 'setattr', 'compile', 'input', 'breakpoint'}:
            raise ValueError(f'disallowed dynamic or external access: {node.id}')
        if isinstance(node, ast.Attribute) and node.attr.startswith('__'):
            raise ValueError('dunder access is not allowed')
    module = emitter(path)
    if goals is None:
        goals = [parse_goal(json.loads(p.read_text())) for p in sorted((HERE / 'goals/train').glob('*.json'))]
    checked = 0
    for raw in goals:
        goal = parse_goal(raw)
        result = module.emit(goal)
        if set(result) != {'defs', 'rule', 'anchor'} or result['anchor'] != 'rule r_strategic [build] {':
            raise ValueError('controller must use the fixed emitter interface and insertion point')
        renamed = dict(goal, name='audit_different_name')
        if result != module.emit(renamed):
            raise ValueError('emitted behavior depends on goal name')
        tr = re.sub(r'/\*.*?\*/|//[^\n]*', '', result['defs'] + '\n' + result['rule'], flags=re.S)
        if re.search(r'\ba\.intent\.\w+\s*(?:\+\+|--|(?:[+*/%&|^-])?=(?!=))', tr):
            raise ValueError('emitter writes task intent/progress')
        if re.search(r'(?:\+\+|--)\s*a\.intent\.', tr) or re.search(r'&\s*a\.intent', tr):
            raise ValueError('emitter mutates or aliases task intent')
        if re.search(r'\b(?:def|const)\b[^;{}]*\b(?:sq_\w+|SQ_\w+|filter|observe)\s*(?:\(|=|\[)', tr):
            raise ValueError('emitter redefines common task semantics')
        if len(re.findall(r'\brule\s+\w+\s*\[', tr)) != 1:
            raise ValueError('emitter must supply exactly one controller rule')
        checked += 1
    return {'controller_path': str(path.relative_to(HERE)), 'sha256': digest(path),
            'checked_goal_structures': checked,
            'note': 'Static/interface checks plus separate manual review; not a semantic proof.'}

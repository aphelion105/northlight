#!/usr/bin/env python3
# northlight-test:
"""Exercise the production freshness gate without compiling a renderer."""
import ast
import hashlib
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
builder = repo / 'scripts/build_renderer.py'
nodes = [node for node in ast.walk(ast.parse(builder.read_text(encoding='utf-8')))
         if isinstance(node, ast.If) and 'derivative_hoist_sha256' in ast.unparse(node.test)]
assert len(nodes) == 1
gate = compile(ast.Module(body=nodes, type_ignores=[]), str(builder), 'exec')
digest = hashlib.sha256((repo / 'scripts/shaders/compile_local_light_shaders.py').read_bytes()).hexdigest()
for manifest, accepted in (({}, False), ({'derivative_hoist_sha256': '0' * 64}, False),
                           ({'derivative_hoist_sha256': digest}, True)):
    try:
        exec(gate, {'ROOT': repo / 'scripts', 'hashlib': hashlib, 'local_manifest': manifest})
    except SystemExit as exc:
        assert not accepted and 'transform differs from offline build' in str(exc)
    else:
        assert accepted, 'Missing or stale transform provenance was accepted'
print('PASS local shader transform freshness: current accepted; missing/stale rejected')

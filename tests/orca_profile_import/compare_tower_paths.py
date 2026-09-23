"""Compare linear tower extrusion by layer; this is not a firmware estimator.

Usage: python compare_tower_paths.py SOURCE.gcode NATIVE.gcode OUTPUT.json
Rejects arcs rather than silently reporting incomplete path lengths.
"""

import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import re


PARAM = re.compile(r"([XYZEF])([-+]?\d*\.?\d+)")


def analyze(path):
    xyz, e, feed = [0., 0., 0.], 0., 0.
    absolute, relative_e = True, True
    layer, role = 0, ""
    layers = defaultdict(lambda: {"mm": 0., "commanded_seconds": 0., "moves": 0})
    tools = []
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for raw in stream:
            digest.update(raw)
            line = raw.decode("utf-8", errors="replace").strip()
            if line.startswith((";LAYER_CHANGE", "; CHANGE_LAYER")):
                layer += 1
            if line.startswith(";TYPE:"):
                role = line[6:]
            code = line.split(";", 1)[0].strip()
            if not code:
                continue
            cmd = code.split()[0]
            values = {k: float(v) for k, v in PARAM.findall(code[len(cmd):])}
            if cmd in ("G2", "G3"):
                raise ValueError(f"{path}: arc moves require an arc-aware parser")
            if cmd == "G90":
                absolute = True
            elif cmd == "G91":
                absolute = False
            elif cmd == "M82":
                relative_e = False
            elif cmd == "M83":
                relative_e = True
            elif re.fullmatch(r"T\d+", cmd):
                tools.append({"layer": layer, "tool": int(cmd[1:])})
            elif cmd == "G92":
                xyz = [values.get(k, xyz[i]) for i, k in enumerate("XYZ")]
                e = values.get("E", e)
            elif cmd in ("G0", "G1"):
                feed = values.get("F", feed * 60.) / 60.
                nxt = [(values[k] if absolute else xyz[i] + values[k])
                       if k in values else xyz[i] for i, k in enumerate("XYZ")]
                de = values.get("E", 0.) if relative_e else values.get("E", e) - e
                e += de
                distance = math.dist(xyz, nxt)
                if distance > 0. and de > 0. and role in ("Wipe tower", "Prime tower"):
                    row = layers[layer]
                    row["mm"] += distance
                    row["moves"] += 1
                    row["commanded_seconds"] += distance / feed if feed > 0. else 0.
                xyz = nxt
    return {"path": str(path), "sha256": digest.hexdigest(),
            "layers": dict(layers), "tool_selections": tools,
            "total_mm": sum(row["mm"] for row in layers.values())}


def compare(source, native):
    left, right = source["layers"], native["layers"]
    common = sorted(left.keys() & right.keys())
    return {
        "common_layers": len(common),
        "common_path_difference_mm": sum(left[k]["mm"] - right[k]["mm"] for k in common),
        "max_layer_path_difference_mm": max((abs(left[k]["mm"] - right[k]["mm"]) for k in common), default=0.),
        "layers_with_different_move_counts": [k for k in common if left[k]["moves"] != right[k]["moves"]],
        "source_only_layers": {k: left[k] for k in sorted(left.keys() - right.keys())},
        "native_only_layers": {k: right[k] for k in sorted(right.keys() - left.keys())},
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("native", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source, native = analyze(args.source), analyze(args.native)
    result = {"source": source, "native": native, "comparison": compare(source, native)}
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result["comparison"], indent=2))

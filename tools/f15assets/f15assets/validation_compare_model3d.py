"""3D model replacement comparison helpers."""

from __future__ import annotations

from typing import Any


def compare_glmesh_primitive_streams(
    expected_stream: list[dict[str, Any]],
    actual_stream: list[dict[str, Any]],
    tolerance: float = 1e-5,
) -> str | None:
    """Compare reduced runtime-mesh primitive streams.

    Vertex float values are allowed a tiny tolerance because GLB authoring tools
    can round-trip text/float encodings differently. Integer source identity,
    primitive modes, color metadata, and ordering must remain exact.
    """

    if len(expected_stream) != len(actual_stream):
        return f"primitive count differs (expected={len(expected_stream)}, actual={len(actual_stream)})"

    for prim_index, (exp, act) in enumerate(zip(expected_stream, actual_stream)):
        for key in ("mode", "source_kind", "source_index", "source_color", "source_flags"):
            if exp[key] != act[key]:
                return f"primitive {prim_index} {key} differs (expected={exp[key]}, actual={act[key]})"
        if len(exp["vertices"]) != len(act["vertices"]):
            return (
                f"primitive {prim_index} vertex count differs "
                f"(expected={len(exp['vertices'])}, actual={len(act['vertices'])})"
            )
        for channel, (a, b) in enumerate(zip(exp["rgba"], act["rgba"])):
            if abs(float(a) - float(b)) > tolerance:
                return f"primitive {prim_index} rgba[{channel}] differs (expected={a}, actual={b})"
        for vertex_index, (exp_vertex, act_vertex) in enumerate(zip(exp["vertices"], act["vertices"])):
            for axis, (a, b) in enumerate(zip(exp_vertex, act_vertex)):
                if abs(float(a) - float(b)) > tolerance:
                    return (
                        f"primitive {prim_index} vertex {vertex_index} axis {axis} differs "
                        f"(expected={a}, actual={b})"
                    )
    return None

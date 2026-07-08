from __future__ import annotations

import json
import re
import struct
from typing import Any, Dict, List, Optional, Sequence, Tuple

from .pic import _default_palette
from .io import from_base64, read_s16_le, read_u16_le, to_base64, write_u16_le

SIGNATURE_3D3 = 0x3333


_FACE_PALETTE_RGB8: Optional[List[int]] = None
_FLIGHT_DAY_COLOR_LUT = [
    # src/egflight.c calls loadColorPalette(g_nightMode) before render3DView().
    # For normal day rendering g_nightMode is 0, so color bytes are remapped
    # through g_colorPalettes[0] before indexing the active VGA palette.
    # Source: src/egdata.c g_colorPalettes[0].
    0x00,
    0x01,
    0x02,
    0x03,
    0x04,
    0x00,
    0x06,
    0x07,
    0x08,
    0x09,
    0x0A,
    0x0B,
    0x0C,
    0x0D,
    0x0E,
    0x0F,
]


def _face_material_color(color_index: int) -> Tuple[float, float, float, float]:
    global _FACE_PALETTE_RGB8
    if _FACE_PALETTE_RGB8 is None:
        _FACE_PALETTE_RGB8 = _default_palette(8)

    raw_idx = int(color_index) & 0xFF
    idx = _FLIGHT_DAY_COLOR_LUT[raw_idx] if raw_idx < len(_FLIGHT_DAY_COLOR_LUT) else raw_idx
    if _FACE_PALETTE_RGB8:
        palette_colors = _FACE_PALETTE_RGB8
        palette_size = len(palette_colors) // 3
        if palette_size <= 0:
            palette_size = 256
        if idx >= palette_size:
            idx %= palette_size
        base = idx * 3
        return (
            palette_colors[base] / 255.0,
            palette_colors[base + 1] / 255.0,
            palette_colors[base + 2] / 255.0,
            1.0,
        )

    # Deterministic fallback in case the palette extraction fails.
    hue = (idx * 37) % 256
    return (hue / 255.0, ((idx * 73) + 31) % 256 / 255.0, ((idx * 19) + 123) % 256 / 255.0, 1.0)


def _decode_rle_primitive_order(
    src: bytes, edge_count: int, visible_edge_mask: int
) -> List[int]:
    # This follows the original renderer's traversal table rather than a normal
    # compression scheme. The stream describes primitive draw order through
    # edge-index links, with 0xFF acting as a return/sentinel marker.
    if edge_count <= 0:
        return [0xFF]
    if not src:
        return [0xFF]

    row_base = [0xFF] * 256
    for edge_idx in range(min(edge_count, 256)):
        if (visible_edge_mask >> edge_idx) & 1:
            row_base[edge_idx] = 0x00

    order: List[int] = []
    if len(src) == 0:
        order.append(0xFF)
        return order

    cx = src[0]
    if cx >= 256:
        cx &= 0xFF

    stack: List[Tuple[int, int]] = []
    while True:
        dx = 0 if row_base[cx] != 0xFF else 2
        bx = cx << 1
        while True:
            if dx == 0:
                dx = 1
                if bx >= len(src):
                    order.append(0xFF)
                    return order
                al = src[bx]
                if al == 0xFF:
                    continue
            elif dx == 1:
                order.append(cx)
                dx = 4
                if bx + 1 >= len(src):
                    order.append(0xFF)
                    return order
                al = src[bx + 1]
                if al == 0xFF:
                    continue
            elif dx == 2:
                dx = 3
                if bx >= len(src):
                    order.append(0xFF)
                    return order
                al = src[bx]
                if al == 0xFF:
                    continue
            elif dx == 3:
                order.append(cx)
                dx = 4
                if bx + 1 >= len(src):
                    order.append(0xFF)
                    return order
                al = src[bx + 1]
                if al == 0xFF:
                    continue
            else:
                if not stack:
                    order.append(0xFF)
                    return order
                dx, cx = stack.pop()
                bx = cx << 1
                continue

            if len(stack) >= 256:
                order.append(0xFF)
                return order
            stack.append((dx, cx))
            cx = al
            bx = cx << 1
            break


def _decode_primitive_command_stream(
    stream: bytes,
    command_budget: Optional[int],
    vertices: List[Tuple[int, int, int]],
    edges: List[Tuple[int, int]],
    wide_mask: bool,
    seen_faces: Optional[set[Tuple[int, ...]]] = None,
    seen_triangles: Optional[set[Tuple[Tuple[int, int, int], int]]] = None,
) -> Tuple[
    List[Tuple[int, int, int]],
    List[int],
    List[Tuple[int, int, Optional[int]]],
    Dict[int, int],
]:
    triangles: List[Tuple[int, int, int]] = []
    triangle_colors: List[int] = []
    lines: List[Tuple[int, int, Optional[int]]] = []
    face_edge_colors: Dict[int, int] = {}
    if seen_faces is None:
        seen_faces = set()
    if seen_triangles is None:
        seen_triangles = set()

    cursor = 0
    while cursor < len(stream) and (command_budget is None or command_budget > 0):
        if cursor >= len(stream):
            break
        opcode = stream[cursor]
        cursor += 1

        if (opcode & 3) == 1:
            # Face command: the command references edges, not vertices. Rebuild
            # the vertex loop from edge endpoints and triangulate for glTF.
            if cursor >= len(stream):
                break
            edge_ref_count = int(stream[cursor])
            cursor += 1
            if cursor + edge_ref_count + 1 > len(stream):
                break
            edge_indices = [int(v) for v in stream[cursor : cursor + edge_ref_count]]
            color = stream[cursor + edge_ref_count]
            cursor += edge_ref_count + 1
            if color == 0xFF:
                # 0xFF is used by the original renderer as "no filled face".
                if command_budget is not None:
                    command_budget -= 1
                continue
            for edge_idx in edge_indices:
                face_edge_colors.setdefault(int(edge_idx), int(color))

            polygon_vertices = _edge_loop_from_edges(edge_indices, edges)
            polygon_vertices = [
                v for v in polygon_vertices if 0 <= v < len(vertices)
            ]
            if len(polygon_vertices) < 3:
                if command_budget is not None:
                    command_budget -= 1
                continue

            signature = _canonical_polygon_signature(polygon_vertices)
            if signature and signature in seen_faces:
                if command_budget is not None:
                    command_budget -= 1
                continue
            if signature:
                seen_faces.add(signature)

            a = polygon_vertices[0]
            for i in range(1, len(polygon_vertices) - 1):
                b = polygon_vertices[i]
                c = polygon_vertices[i + 1]
                if a < len(vertices) and b < len(vertices) and c < len(vertices):
                    if a != b and b != c and a != c:
                        tri = (a, b, c)
                        tri_signature = tuple(sorted(tri))
                        tri_key = (tri_signature, color)
                        if tri_key not in seen_triangles:
                            seen_triangles.add(tri_key)
                            triangles.append(tri)
                            triangle_colors.append(color)
        else:
            # Non-face commands are kept as line primitives. Some source shapes
            # are intentionally 2D/single-sided, so fabricating reverse faces
            # would introduce z-fighting and incorrect colors.
            cursor, _ = _read_visibility_mask(stream, cursor, wide_mask)
            if cursor + 2 > len(stream):
                break

            edge_idx = stream[cursor]
            color = stream[cursor + 1]
            cursor += 2
            if color != 0xFF and edge_idx < len(edges):
                a, b = edges[edge_idx]
                lines.append((a, b, int(color)))

        if command_budget is not None:
            command_budget -= 1

    return triangles, triangle_colors, lines, face_edge_colors


def _decode_edge_record_candidates(
    data: bytes,
    edge_count: int,
    wide_mask: bool,
    vertex_count: int,
) -> List[Tuple[List[Tuple[int, int]], int]]:
    """Decode edge records that can encode hidden edges without endpoint bytes.

    The game stream uses a visibility mask before every edge record. If the mask is
    false, only the mask bytes are present; otherwise two edge-endpoint bytes are
    stored. At convert time we do not know the runtime visibility mask, so multiple
    candidates are possible. This helper enumerates candidates by cursor position and
    keeps the endpoint-best candidate per cursor to avoid explosive branching.
    """
    if edge_count <= 0:
        return [([], 0)]

    mask_width = 4 if wide_mask else 2
    # state: (next_cursor, edges, valid_endpoint_score)
    states: List[Tuple[int, List[Tuple[int, int]], int]] = [(0, [], 0)]

    for edge_idx in range(edge_count):
        rem_edges = edge_count - edge_idx - 1
        min_tail = rem_edges * mask_width + 1
        next_states: Dict[int, Tuple[List[Tuple[int, int]], int]] = {}

        for cursor, edges, score in states:
            try:
                cursor_after_mask, _ = _read_visibility_mask(data, cursor, wide_mask)
            except ValueError:
                continue
            if cursor_after_mask > len(data):
                continue
            if len(data) - cursor_after_mask < min_tail:
                continue

            # Hidden edge: only mask bytes are present.
            hidden_entry = (0, 0)
            current = next_states.get(cursor_after_mask)
            if current is None or current[1] <= score:
                next_states[cursor_after_mask] = (edges + [hidden_entry], score)

            # Visible edge: mask + two vertex endpoints.
            if cursor_after_mask + 2 <= len(data) and len(data) - (cursor_after_mask + 2) >= min_tail:
                v1 = data[cursor_after_mask]
                v2 = data[cursor_after_mask + 1]
                endpoint_score = (1 if v1 < vertex_count else 0) + (1 if v2 < vertex_count else 0)
                visible_score = score + endpoint_score
                visible_cursor = cursor_after_mask + 2
                current = next_states.get(visible_cursor)
                if current is None or current[1] < visible_score:
                    next_states[visible_cursor] = (edges + [(v1, v2)], visible_score)

        states = [(cursor, list(edge_list), edge_score) for cursor, (edge_list, edge_score) in next_states.items()]
        if not states:
            break

    candidates = []
    for cursor, edges, _ in states:
        if cursor < len(data):
            candidates.append((edges, cursor))
    return candidates


def _decode_fixed_edge_records(
    data: bytes,
    edge_count: int,
    wide_mask: bool,
) -> Tuple[List[Tuple[int, int]], int, Optional[str]]:
    edges: List[Tuple[int, int]] = []
    cursor = 0
    for _ in range(edge_count):
        try:
            cursor, _ = _read_visibility_mask(data, cursor, wide_mask)
        except ValueError:
            return edges, cursor, "truncated_visibility_mask"

        if cursor + 2 > len(data):
            return edges, cursor, "truncated_edge_endpoints"

        v1 = data[cursor]
        v2 = data[cursor + 1]
        cursor += 2
        edges.append((v1, v2))

    return edges, cursor, None


def _decode_primitive_payload(
    stream: bytes,
    cursor: int,
    vertices: List[Tuple[int, int, int]],
    edges: List[Tuple[int, int]],
    wide_mask: bool,
    rle_entry_count: int,
) -> Tuple[
    List[Tuple[int, int, int]],
    List[int],
    List[Tuple[int, int, Optional[int]]],
    Dict[int, int],
    Optional[str],
]:
    if cursor >= len(stream):
        return [], [], [], {}, "missing_primitive_payload"

    triangles: List[Tuple[int, int, int]] = []
    triangle_colors: List[int] = []
    lines: List[Tuple[int, int, Optional[int]]] = []
    face_edge_colors: Dict[int, int] = {}
    try:
        command_budget = stream[cursor]
        cursor += 1

        if command_budget == 0xFF:
            # Complex RLE-ordered command table used by several aircraft shapes.
            # Format after the 0xFF header:
            # [2*rle_entry_count bytes: coords] [rle_entry_count bytes: run counts]
            # [command blocks]
            shared_faces: set[Tuple[int, ...]] = set()
            shared_triangles: set[Tuple[Tuple[int, int, int], int]] = set()
            rle_stream = stream[cursor:]
            if not rle_stream:
                return triangles, triangle_colors, lines, face_edge_colors, "empty_rle_stream"

            # The stream format stores one leading byte before the RLE row table
            # that is not part of the decode source in this game format.
            rle_row_stream = rle_stream[1:]

            coord_offset = (rle_entry_count * 2) + 1
            count_offset = coord_offset + (rle_entry_count * 2)
            data_offset = count_offset + rle_entry_count
            if (
                coord_offset <= len(rle_stream)
                and count_offset <= len(rle_stream)
                and data_offset <= len(rle_stream)
            ):
                coord = rle_stream[coord_offset : coord_offset + (rle_entry_count * 2)]
                run_counts = rle_stream[count_offset:data_offset]

                visible_mask = (
                    (1 << rle_entry_count) - 1 if rle_entry_count < 257 else 0xFFFFFFFFFFFFFFFF
                )
                order = _decode_rle_primitive_order(
                    rle_row_stream,
                    rle_entry_count,
                    visible_mask,
                )

                # The game follows one view-dependent order, terminated by
                # 0xFF. A self-contained editable export must preserve every
                # primitive run that can be selected by the visibility masks,
                # otherwise aircraft faces disappear from static GLB output.
                ordered_runs: List[int] = []
                for run_edge in order:
                    if run_edge == 0xFF:
                        break
                    if run_edge < rle_entry_count and run_edge not in ordered_runs:
                        ordered_runs.append(run_edge)
                for run_edge in range(rle_entry_count):
                    if run_edge not in ordered_runs:
                        ordered_runs.append(run_edge)

                for run_edge in ordered_runs:
                    if run_edge * 2 + 1 >= len(coord):
                        continue
                    run_offset = int.from_bytes(
                        coord[(run_edge * 2) : (run_edge * 2) + 2],
                        "little",
                        signed=True,
                    )
                    if run_offset < 0 or run_offset >= len(rle_stream[data_offset:]):
                        continue
                    run_ptr = rle_stream[data_offset + run_offset :]
                    run_count = run_counts[run_edge]
                    run_triangles, run_colors, run_lines, run_face_edge_colors = _decode_primitive_command_stream(
                        run_ptr,
                        run_count,
                        vertices,
                        edges,
                        wide_mask,
                        shared_faces,
                        shared_triangles,
                    )
                    triangles.extend(run_triangles)
                    triangle_colors.extend(run_colors)
                    lines.extend(run_lines)
                    face_edge_colors.update(run_face_edge_colors)
            else:
                return triangles, triangle_colors, lines, face_edge_colors, "truncated_rle_payload"
        elif command_budget != 0:
            triangles_block, colors_block, lines_block, edge_color_block = _decode_primitive_command_stream(
                stream[cursor:],
                command_budget,
                vertices,
                edges,
                wide_mask,
            )
            triangles.extend(triangles_block)
            triangle_colors.extend(colors_block)
            lines.extend(lines_block)
            face_edge_colors.update(edge_color_block)
    except Exception as exc:
        return triangles, triangle_colors, lines, face_edge_colors, f"decode_exception:{exc}"

    return triangles, triangle_colors, lines, face_edge_colors, None


def _skip_visibility_mask(data: bytes, offset: int, wide: bool) -> int:
    return offset + (4 if wide else 2)


def _read_visibility_mask(data: bytes, offset: int, wide: bool) -> Tuple[int, int]:
    end = _skip_visibility_mask(data, offset, wide)
    if end > len(data):
        raise ValueError("truncated visibility mask")
    return end, int.from_bytes(data[offset:end], "little", signed=False)


def _edge_loop_from_edges(edge_indices: Sequence[int], edges: Sequence[Tuple[int, int]]) -> List[int]:
    if len(edge_indices) < 3:
        return []

    edge_lookup: Dict[int, Tuple[int, int]] = {}
    for edge_idx in edge_indices:
        if 0 <= edge_idx < len(edges):
            edge_lookup[int(edge_idx)] = edges[edge_idx]
    if not edge_lookup:
        return []

    ordered_edges = list(edge_lookup.keys())
    first = ordered_edges[0]
    a, b = edge_lookup[first]
    output = [a, b]
    used = {first}
    current = b
    previous = a

    while len(used) < len(edge_lookup):
        next_idx = None
        next_vertex = None

        for idx, (u, v) in edge_lookup.items():
            if idx in used:
                continue
            if u == current and v != previous:
                next_idx = idx
                next_vertex = v
                break
            if v == current and u != previous:
                next_idx = idx
                next_vertex = u
                break

        if next_idx is None:
            break

        used.add(next_idx)
        output.append(next_vertex)
        previous, current = current, next_vertex
        if current == output[0]:
            break

    if len(output) < 3:
        uniq: List[int] = []
        for idx in edge_indices:
            if idx not in edge_lookup:
                continue
            for vertex in edge_lookup[idx]:
                if vertex not in uniq:
                    uniq.append(vertex)
        output = uniq

    return output


def _canonical_polygon_signature(vertices: Sequence[int]) -> Tuple[int, ...]:
    if len(vertices) < 3:
        return ()

    reduced: List[int] = []
    for vertex in vertices:
        if not reduced or reduced[-1] != vertex:
            reduced.append(vertex)
    if reduced and reduced[0] == reduced[-1]:
        reduced = reduced[:-1]
    if len(reduced) < 3:
        return ()

    cycle = list(reduced)
    n = len(cycle)
    doubled = cycle * 2
    forward: Optional[Tuple[int, ...]] = None
    for i in range(n):
        candidate = tuple(doubled[i : i + n])
        if forward is None or candidate < forward:
            forward = candidate

    reverse = list(reversed(cycle))
    doubled_rev = reverse * 2
    backward: Optional[Tuple[int, ...]] = None
    for i in range(n):
        candidate = tuple(doubled_rev[i : i + n])
        if backward is None or candidate < backward:
            backward = candidate

    if forward is None or backward is None:
        return tuple(cycle)
    return backward if backward < forward else forward


def _decode_model_edges_and_primitives(
    stream: bytes, shared_pool: Optional[Dict[str, Any]]
) -> Dict[str, Any]:
    if not stream:
        return {
            "render_mode": 0,
            "vertices": [],
            "edges": [],
            "triangles": [],
            "lines": [],
            "metadata": {},
        }

    cursor = 0
    render_mode = stream[cursor]
    cursor += 1

    lod_records: List[int] = []
    while cursor + 2 < len(stream) and (stream[cursor] & 0x80):
        lod_records.append(stream[cursor])
        cursor += 3
        if cursor > len(stream):
            cursor = len(stream)
            break

    transform_records: List[int] = []
    while cursor < len(stream) and (stream[cursor] & 0x60) == 0x60:
        transform_records.append(stream[cursor])
        cursor += 1
        if cursor >= len(stream):
            return {
                "render_mode": render_mode,
                "vertices": [],
                "edges": [],
                "triangles": [],
                "lines": [],
                "metadata": {
                    "lod_records": lod_records,
                    "transform_records": transform_records,
                },
            }

    if cursor >= len(stream):
        return {
            "render_mode": render_mode,
            "vertices": [],
            "edges": [],
            "triangles": [],
            "lines": [],
            "metadata": {"lod_records": lod_records, "transform_records": transform_records},
        }

    face_info = stream[cursor]
    cursor += 1
    face_count = face_info & 0x1F
    cursor += face_count * 8
    if cursor > len(stream):
        raise ValueError("truncated 3D3 face-normal block")

    # In the runtime pipeline, `g_modelEdgeCount` is this face count, and it is
    # reused by the 0xFF primitive-order path. The edge table that follows has
    # a separate count.
    rle_entry_count = face_count
    wide_mask = rle_entry_count > 0x10

    if cursor >= len(stream):
        return {
            "render_mode": render_mode,
            "vertices": [],
            "edges": [],
            "triangles": [],
            "lines": [],
            "metadata": {
                "face_count": face_count,
                "lod_records": lod_records,
                "transform_records": transform_records,
            },
        }

    vertex_header = stream[cursor]
    cursor += 1
    vertex_count = vertex_header & 0x7F
    vertices: List[Tuple[int, int, int]] = []
    unresolved_vertex_count = 0

    if vertex_header & 0x80:
        for _ in range(vertex_count):
            if cursor >= len(stream):
                break
            cursor, _ = _read_visibility_mask(stream, cursor, wide_mask)
            if cursor >= len(stream):
                break
            ref = stream[cursor]
            cursor += 1

            x = 0
            y = 0
            z = 0
            if shared_pool:
                x_indices = shared_pool.get("x_indices", [])
                y_indices = shared_pool.get("y_indices", [])
                z_indices = shared_pool.get("z_indices", [])
                x_values = shared_pool.get("x_values", [])
                y_values = shared_pool.get("y_values", [])
                z_values = shared_pool.get("z_values", [])

                if (
                    ref < len(x_indices)
                    and ref < len(y_indices)
                    and ref < len(z_indices)
                ):
                    xi = x_indices[ref]
                    yi = y_indices[ref]
                    zi = z_indices[ref]
                    x = x_values[xi] if xi < len(x_values) else 0
                    y = y_values[yi] if yi < len(y_values) else 0
                    z = z_values[zi] if zi < len(z_values) else 0
                else:
                    unresolved_vertex_count += 1
            else:
                unresolved_vertex_count += 1

            vertices.append((x, y, z))
    else:
        for _ in range(vertex_count):
            if cursor >= len(stream):
                break
            cursor, _ = _read_visibility_mask(stream, cursor, wide_mask)
            if cursor + 6 > len(stream):
                break
            x = read_s16_le(stream, cursor)
            y = read_s16_le(stream, cursor + 2)
            z = read_s16_le(stream, cursor + 4)
            cursor += 6
            vertices.append((x, y, z))

    if cursor >= len(stream):
        return {
            "render_mode": render_mode,
            "vertices": vertices,
            "edges": [],
            "triangles": [],
            "lines": [],
            "metadata": {
                "face_count": face_count,
                "lod_records": lod_records,
                "transform_records": transform_records,
                "unresolved_vertex_count": unresolved_vertex_count,
            },
        }

    edge_count = stream[cursor]
    cursor += 1
    if cursor > len(stream):
        raise ValueError("truncated 3D3 edge declaration")

    edge_payload_start = cursor
    edge_payload = stream[edge_payload_start:]
    edge_candidates = _decode_edge_record_candidates(
        edge_payload,
        edge_count,
        wide_mask,
        len(vertices),
    )

    edges: List[Tuple[int, int]] = []
    triangles: List[Tuple[int, int, int]] = []
    triangle_colors: List[int] = []
    lines: List[Tuple[int, int, Optional[int]]] = []
    face_edge_colors_by_index: Dict[int, int] = {}

    fixed_edges, fixed_cursor, fixed_edge_decode_error = _decode_fixed_edge_records(
        edge_payload,
        edge_count,
        wide_mask,
    )
    fixed_triangles, fixed_colors, fixed_lines, fixed_face_edge_colors, fixed_decode_error = _decode_primitive_payload(
        edge_payload,
        fixed_cursor,
        vertices,
        fixed_edges,
        wide_mask,
        rle_entry_count,
    )
    fixed_parse_ok = fixed_edge_decode_error is None and fixed_decode_error is None

    edge_decode_mode = "fixed"
    edges = fixed_edges
    triangles = fixed_triangles
    triangle_colors = fixed_colors
    lines = fixed_lines
    face_edge_colors_by_index = fixed_face_edge_colors
    rle_decode_error = fixed_decode_error or fixed_edge_decode_error

    needs_adaptive = (
        fixed_edge_decode_error is not None
        or fixed_decode_error is not None
        or (fixed_parse_ok and not triangles)
    )

    if needs_adaptive and edge_candidates:
        best_score = (-1, -1, -1, -1, 0x7FFFFFFF)
        chosen_edges: Optional[List[Tuple[int, int]]] = None
        chosen_triangles: List[Tuple[int, int, int]] = []
        chosen_colors: List[int] = []
        chosen_lines: List[Tuple[int, int, Optional[int]]] = []
        chosen_face_edge_colors: Dict[int, int] = {}
        chosen_error: Optional[str] = None

        for cand_edges, cand_cursor in edge_candidates:
            if cand_cursor >= len(edge_payload):
                continue

            visible_edge_count = sum(
                1 for a, b in cand_edges if a < len(vertices) and b < len(vertices) and a != b
            )
            tri_block, color_block, line_block, face_edge_color_block, decode_error = _decode_primitive_payload(
                edge_payload,
                cand_cursor,
                vertices,
                cand_edges,
                wide_mask,
                rle_entry_count,
            )
            decode_score = (
                0 if decode_error else 1,
                visible_edge_count,
                len(tri_block),
                len(line_block),
                -cand_cursor,
            )
            if decode_score > best_score:
                best_score = decode_score
                chosen_edges = cand_edges
                chosen_triangles = tri_block
                chosen_colors = color_block
                chosen_lines = line_block
                chosen_face_edge_colors = face_edge_color_block
                chosen_error = decode_error

        if (
            chosen_edges is not None
            and chosen_error is None
            and chosen_triangles
        ):
            edges = chosen_edges
            triangles = chosen_triangles
            triangle_colors = chosen_colors
            lines = chosen_lines
            face_edge_colors_by_index = chosen_face_edge_colors
            edge_decode_mode = "adaptive"
            rle_decode_error = chosen_error

    face_edge_colors: Dict[Tuple[int, int], int] = {}
    face_color_counts: Dict[int, int] = {}
    for tri_index, tri in enumerate(triangles):
        if len(tri) != 3 or tri_index >= len(triangle_colors):
            continue
        color = int(triangle_colors[tri_index])
        face_color_counts[color] = face_color_counts.get(color, 0) + 1
        a, b, c = (int(tri[0]), int(tri[1]), int(tri[2]))
        for edge in (tuple(sorted((a, b))), tuple(sorted((b, c))), tuple(sorted((c, a)))):
            face_edge_colors.setdefault(edge, color)
    dominant_face_color: Optional[int] = None
    if face_color_counts:
        dominant_face_color = sorted(
            face_color_counts.items(),
            key=lambda item: (-item[1], item[0]),
        )[0][0]

    colored_line_edges = {
        tuple(sorted((int(line[0]), int(line[1]))))
        for line in lines
        if len(line) >= 3 and line[2] is not None and line[0] != line[1]
    }
    structural_edges = [
        (edge_idx, int(edge[0]), int(edge[1]))
        for edge_idx, edge in enumerate(edges)
        if edge[0] != edge[1]
    ]
    for edge_idx, a, b in structural_edges:
        if a < len(vertices) and b < len(vertices):
            edge_key = tuple(sorted((a, b)))
            if edge_key in colored_line_edges:
                continue
            lines.append(
                (
                    a,
                    b,
                    face_edge_colors_by_index.get(
                        edge_idx,
                        face_edge_colors.get(edge_key, dominant_face_color),
                    ),
                )
            )

    return {
        "render_mode": render_mode,
        "vertices": vertices,
        "edges": edges,
        "triangles": triangles,
        "triangle_colors": triangle_colors,
        "lines": lines,
        "metadata": {
            "face_count": face_count,
            "edge_count": edge_count,
            "lod_records": lod_records,
            "transform_records": transform_records,
            "unresolved_vertex_count": unresolved_vertex_count,
            "edge_decode_mode": edge_decode_mode,
            "edge_candidates": len(edge_candidates),
            "edge_decode_error": rle_decode_error,
            "face_edge_color_count": len(face_edge_colors_by_index),
            "dominant_face_color": dominant_face_color,
        },
    }


def _align4(value: int) -> int:
    return (4 - (value % 4)) % 4


def _emit_buffer_view(gltf: Dict[str, Any], raw: bytes, target: int) -> int:
    uri = gltf["buffers"][0]["uri"]
    comma_pos = uri.index(",")
    raw_buffer = bytearray(from_base64(uri[comma_pos + 1:]))
    raw_buffer.extend(b"\x00" * _align4(len(raw_buffer)))
    byte_offset = len(raw_buffer)
    raw_buffer.extend(raw)
    view_index = len(gltf["bufferViews"])
    gltf["bufferViews"].append(
        {
            "buffer": 0,
            "byteOffset": byte_offset,
            "byteLength": len(raw),
            "target": target,
        }
    )
    gltf["buffers"][0]["uri"] = "data:application/octet-stream;base64," + to_base64(
        bytes(raw_buffer)
    )
    gltf["buffers"][0]["byteLength"] = len(raw_buffer)
    return view_index


def _emit_accessor(
    gltf: Dict[str, Any],
    buffer_view: int,
    component_type: int,
    type_name: str,
    count: int,
    byte_offset: int = 0,
) -> int:
    accessor_index = len(gltf["accessors"])
    dims = 3 if type_name == "VEC3" else 1
    gltf["accessors"].append(
        {
            "bufferView": buffer_view,
            "byteOffset": byte_offset,
            "componentType": component_type,
            "count": count,
            "type": type_name,
            "min": [0] * dims,
            "max": [0] * dims,
        }
    )
    return accessor_index


def _pack_indices(values: Sequence[int]) -> Tuple[bytes, int]:
    max_value = max(values) if values else 0
    if max_value > 0xFFFF:
        return struct.pack("<" + "I" * len(values), *values), 5125
    return struct.pack("<" + "H" * len(values), *values), 5123


def _pack_floats(values: Sequence[float]) -> bytes:
    return struct.pack("<" + "f" * len(values), *values) if values else b""


def _safe_shape_name(value: object) -> str:
    name = str(value or "").strip()
    name = re.sub(r"[^0-9A-Za-z_. -]+", "_", name)
    name = re.sub(r"\s+", "_", name)
    return name.strip("_")[:64]


def _shape_label(shape_names: object, shape_index: int) -> str:
    label = ""
    if isinstance(shape_names, dict):
        label = str(shape_names.get(str(shape_index), "") or shape_names.get(shape_index, "") or "")
    elif isinstance(shape_names, list) and shape_index < len(shape_names):
        label = str(shape_names[shape_index] or "")
    safe = _safe_shape_name(label)
    if safe:
        return f"shape_{shape_index:03d}_{safe}"
    return f"shape_{shape_index:03d}"


def parse_3d3(data: bytes) -> Dict[str, Any]:
    if len(data) < 6:
        raise ValueError(".3D3 data too short")

    offset = 0
    signature = read_u16_le(data, offset)
    if signature != SIGNATURE_3D3:
        raise ValueError(f"bad .3D3 signature: 0x{signature:04x}")
    offset += 2

    shape_count = read_u16_le(data, offset)
    offset += 2

    shape_offsets = []
    for _ in range(shape_count):
        if offset + 2 > len(data):
            raise ValueError("truncated .3D3 shape table")
        shape_offsets.append(read_u16_le(data, offset))
        offset += 2

    if offset + 2 > len(data):
        raise ValueError("truncated .3D3 model size")
    model_data_size = read_u16_le(data, offset)
    offset += 2

    model_data_end = offset + model_data_size
    if model_data_end > len(data):
        raise ValueError("truncated .3D3 model payload")
    model_data = data[offset:model_data_end]
    offset = model_data_end

    shared_pool: Optional[Dict[str, Any]] = None
    if offset < len(data) and data[offset] != 0:
        x_ref_count = data[offset]
        offset += 1

        expected = offset + x_ref_count * 3
        if expected > len(data):
            raise ValueError("truncated .3D3 shared index arrays")
        x_indices = list(data[offset : offset + x_ref_count])
        y_indices = list(data[offset + x_ref_count : offset + 2 * x_ref_count])
        z_indices = list(data[offset + 2 * x_ref_count : offset + 3 * x_ref_count])
        offset += 3 * x_ref_count

        if offset >= len(data):
            raise ValueError("truncated .3D3 x coordinate count")
        x_count = data[offset]
        offset += 1
        x_values_end = offset + (x_count * 2)
        if x_values_end > len(data):
            raise ValueError("truncated .3D3 x coordinate values")
        x_values = [
            int.from_bytes(data[i : i + 2], "little", signed=True)
            for i in range(offset, x_values_end, 2)
        ]
        offset = x_values_end

        if offset >= len(data):
            raise ValueError("truncated .3D3 y coordinate count")
        y_count = data[offset]
        offset += 1
        y_values_end = offset + (y_count * 2)
        if y_values_end > len(data):
            raise ValueError("truncated .3D3 y coordinate values")
        y_values = [
            int.from_bytes(data[i : i + 2], "little", signed=True)
            for i in range(offset, y_values_end, 2)
        ]
        offset = y_values_end

        if offset >= len(data):
            raise ValueError("truncated .3D3 z coordinate count")
        z_count = data[offset]
        offset += 1
        z_values_end = offset + (z_count * 2)
        if z_values_end > len(data):
            raise ValueError("truncated .3D3 z coordinate values")
        z_values = [
            int.from_bytes(data[i : i + 2], "little", signed=True)
            for i in range(offset, z_values_end, 2)
        ]
        offset = z_values_end

        shared_pool = {
            "x_indices": x_indices,
            "y_indices": y_indices,
            "z_indices": z_indices,
            "x_values": x_values,
            "y_values": y_values,
            "z_values": z_values,
        }

    trailing = data[offset:]

    return {
        "format": "3D3",
        "version": 1,
        "signature": signature,
        "shape_offsets": shape_offsets,
        "model_data": to_base64(model_data),
        "model_data_size": model_data_size,
        "shared_vertex_pool": shared_pool,
        "trailing_bytes": to_base64(trailing),
    }


def export_3d3_to_gltf(payload: Dict[str, Any]) -> Dict[str, Any]:
    if payload.get("format") != "3D3":
        raise ValueError("invalid payload format")

    model_data = from_base64(payload["model_data"])
    shape_offsets = payload["shape_offsets"]
    shape_names = payload.get("shape_names", {})
    model_data_size = int(payload.get("model_data_size", len(model_data)))
    shared_pool = payload.get("shared_vertex_pool")
    shared_pool_summary = None
    if shared_pool:
        shared_pool_summary = {
            "index_count": len(shared_pool.get("x_indices", [])),
            "x_value_count": len(shared_pool.get("x_values", [])),
            "y_value_count": len(shared_pool.get("y_values", [])),
            "z_value_count": len(shared_pool.get("z_values", [])),
        }

    gltf: Dict[str, Any] = {
        "asset": {"version": "2.0", "generator": "f15assets"},
        "scenes": [{"name": "default", "nodes": []}],
        "nodes": [],
        "meshes": [],
        "materials": [
            {"name": "wireframe", "doubleSided": True},
            {"name": "faces", "doubleSided": True},
        ],
        "accessors": [],
        "bufferViews": [],
        "buffers": [{"byteLength": 0, "uri": "data:application/octet-stream;base64,"}],
        "extras": {
            "format": "3D3",
            "model_data_size": model_data_size,
            "shape_offsets": shape_offsets,
            "shape_names": shape_names,
            "has_shared_vertex_pool": bool(shared_pool),
            "shared_vertex_pool": shared_pool_summary,
            "skipped_shapes": [],
        },
    }

    for shape_index, shape_offset in enumerate(shape_offsets):
        if shape_offset >= model_data_size:
            continue
        shape_end = model_data_size
        color_materials: Dict[int, int] = {}
        fallback_face_material = 1
        if shape_index + 1 < len(shape_offsets):
            shape_end = min(model_data_size, shape_offsets[shape_index + 1])
            if shape_end < shape_offset:
                shape_end = shape_offset

        shape_bytes = model_data[shape_offset:shape_end]
        if len(shape_bytes) < 1:
            shape_parse = {
                "render_mode": 0,
                "vertices": [],
                "edges": [],
                "triangles": [],
                "lines": [],
                "metadata": {
                    "render_error": "empty_shape_payload",
                    "shape_index": shape_index,
                },
            }
        else:
            try:
                shape_parse = _decode_model_edges_and_primitives(shape_bytes, shared_pool)
            except Exception as exc:
                shape_parse = {
                    "render_mode": 0,
                    "vertices": [],
                    "edges": [],
                    "triangles": [],
                    "lines": [],
                    "metadata": {
                        "render_error": str(exc),
                        "shape_index": shape_index,
                        "shape_offset": shape_offset,
                        "shape_end": shape_end,
                    },
                }

        vertices = shape_parse["vertices"]
        triangles = shape_parse["triangles"]
        triangle_colors = shape_parse.get("triangle_colors", [])
        lines = shape_parse["lines"]

        mesh_index = len(gltf["meshes"])
        primitives: List[Dict[str, Any]] = []

        if vertices:
            flat_positions = [float(v) for item in vertices for v in item]
            pos_view = _emit_buffer_view(gltf, _pack_floats(flat_positions), 34962)
            pos_acc = _emit_accessor(
                gltf,
                pos_view,
                component_type=5126,
                type_name="VEC3",
                count=len(vertices),
            )
            mins = [
                min(vertices[i][0] for i in range(len(vertices))),
                min(vertices[i][1] for i in range(len(vertices))),
                min(vertices[i][2] for i in range(len(vertices))),
            ]
            maxs = [
                max(vertices[i][0] for i in range(len(vertices))),
                max(vertices[i][1] for i in range(len(vertices))),
                max(vertices[i][2] for i in range(len(vertices))),
            ]
            gltf["accessors"][pos_acc]["min"] = [float(x) for x in mins]
            gltf["accessors"][pos_acc]["max"] = [float(x) for x in maxs]

            if triangles:
                triangles_by_color: Dict[Optional[int], List[int]] = {}
                for tri_index, tri in enumerate(triangles):
                    if len(tri) != 3:
                        continue
                    color = (
                        int(triangle_colors[tri_index])
                        if tri_index < len(triangle_colors)
                        else None
                    )
                    triangles_by_color.setdefault(color, []).extend(int(v) for v in tri)

                for color, tri_indices in triangles_by_color.items():
                    tri_bin, tri_type = _pack_indices(tri_indices)
                    tri_view = _emit_buffer_view(gltf, tri_bin, 34963)
                    tri_acc = _emit_accessor(
                        gltf,
                        tri_view,
                        component_type=tri_type,
                        type_name="SCALAR",
                        count=len(tri_indices),
                    )
                    gltf["accessors"][tri_acc]["min"] = [0]
                    gltf["accessors"][tri_acc]["max"] = [max(0, len(vertices) - 1)]

                    if color is None:
                        material_index = fallback_face_material
                    else:
                        if color not in color_materials:
                            r, g, b, a = _face_material_color(color)
                            material_index = len(gltf["materials"])
                            color_materials[color] = material_index
                            gltf["materials"].append(
                                {
                                    "name": f"faces_color_0x{color:02x}",
                                    "doubleSided": True,
                                    "pbrMetallicRoughness": {
                                        "baseColorFactor": [r, g, b, a],
                                        "metallicFactor": 0.0,
                                        "roughnessFactor": 1.0,
                                    },
                                }
                            )
                        else:
                            material_index = color_materials[color]

                    primitives.append(
                        {
                            "attributes": {"POSITION": pos_acc},
                            "indices": tri_acc,
                            "mode": 4,
                            "material": material_index,
                        }
                    )

            lines_by_color: Dict[Optional[int], set[Tuple[int, int]]] = {}
            for line in lines:
                if len(line) == 2:
                    a, b = int(line[0]), int(line[1])
                    color = None
                else:
                    a, b = int(line[0]), int(line[1])
                    color = line[2]
                    color = None if color is None else int(color)
                if a == b:
                    continue
                lines_by_color.setdefault(color, set()).add(tuple(sorted((a, b))))

            for color, line_set in sorted(lines_by_color.items(), key=lambda item: -1 if item[0] is None else item[0]):
                uniq_lines = sorted(line_set)
                if not uniq_lines:
                    continue
                line_indices = [int(v) for edge in uniq_lines for v in edge]
                line_bin, line_type = _pack_indices(line_indices)
                line_view = _emit_buffer_view(gltf, line_bin, 34963)
                line_acc = _emit_accessor(
                    gltf,
                    line_view,
                    component_type=line_type,
                    type_name="SCALAR",
                    count=len(line_indices),
                )
                gltf["accessors"][line_acc]["min"] = [0]
                gltf["accessors"][line_acc]["max"] = [max(0, len(vertices) - 1)]

                if color is None:
                    material_index = 0
                else:
                    r, g, b, a = _face_material_color(color)
                    material_index = len(gltf["materials"])
                    gltf["materials"].append(
                        {
                            "name": f"lines_color_0x{color:02x}",
                            "doubleSided": True,
                            "pbrMetallicRoughness": {
                                "baseColorFactor": [r, g, b, a],
                                "metallicFactor": 0.0,
                                "roughnessFactor": 1.0,
                            },
                        }
                    )

                primitives.append(
                    {
                        "attributes": {"POSITION": pos_acc},
                        "indices": line_acc,
                        "mode": 1,
                        "material": material_index,
                    }
                )

        if not primitives:
            # glTF requires each mesh to have at least one primitive. Some game
            # shape slots are empty, metadata-only, or fail safe decoding; the
            # original renderer simply never draws geometry for those entries.
            # Export their parse metadata in extras instead of creating an
            # invalid empty mesh that Blender's importer cannot handle.
            gltf["extras"]["skipped_shapes"].append(
                {
                    "shape_index": shape_index,
                    "shape_name": _shape_label(shape_names, shape_index),
                    "shape_offset": shape_offset,
                    "shape_end": shape_end,
                    "render_mode": shape_parse["render_mode"],
                    "shape_payload": shape_parse["metadata"],
                }
            )
            continue

        shape_label = _shape_label(shape_names, shape_index)
        mesh = {
            "name": shape_label,
            "primitives": primitives,
            "extras": {
                "shape_index": shape_index,
                "shape_name": shape_label,
                "render_mode": shape_parse["render_mode"],
                "shape_offset": shape_offset,
                "shape_end": shape_end,
                "shape_payload": shape_parse["metadata"],
            },
        }
        gltf["meshes"].append(mesh)

        node_index = len(gltf["nodes"])
        gltf["nodes"].append({"name": shape_label, "mesh": mesh_index})
        gltf["scenes"][0]["nodes"].append(node_index)

    # Finalize final buffer length in case there were no writes.
    if gltf["buffers"][0]["uri"].startswith("data:application/octet-stream;base64,"):
        gltf["buffers"][0]["byteLength"] = len(
            from_base64(gltf["buffers"][0]["uri"].split(",", 1)[1])
        )
    else:
        gltf["buffers"][0]["byteLength"] = 0

    return gltf


def export_3d3_to_glb(payload: Dict[str, Any]) -> bytes:
    """Export a ``3D3`` payload as a binary glTF (`.glb`) blob."""
    gltf = export_3d3_to_gltf(payload)
    buffer_uri = gltf["buffers"][0].get("uri", "")
    if not buffer_uri.startswith("data:application/octet-stream;base64,"):
        raise ValueError("expected embedded binary buffer URI in glTF payload")

    gltf_buffer = from_base64(buffer_uri.split(",", 1)[1])
    gltf["buffers"][0].pop("uri", None)
    gltf["buffers"][0]["byteLength"] = len(gltf_buffer)

    json_bytes = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    pad_json = (-len(json_bytes)) & 3
    if pad_json:
        json_bytes += b"\x20" * pad_json

    pad_bin = (-len(gltf_buffer)) & 3
    if pad_bin:
        gltf_buffer += b"\x00" * pad_bin

    chunks = bytearray()
    chunks.extend(struct.pack("<I4s", len(json_bytes), b"JSON"))
    chunks.extend(json_bytes)
    chunks.extend(struct.pack("<I4s", len(gltf_buffer), b"BIN"))
    chunks.extend(gltf_buffer)

    header_size = 12 + len(chunks)
    glb = bytearray()
    glb.extend(struct.pack("<4sII", b"glTF", 2, header_size))
    glb.extend(chunks)
    return bytes(glb)


def build_3d3(payload: Dict[str, Any]) -> bytes:
    if payload.get("format") != "3D3":
        raise ValueError("invalid payload format")

    shape_offsets = payload["shape_offsets"]
    model_data = from_base64(payload["model_data"])

    out = bytearray()
    out.extend(write_u16_le(SIGNATURE_3D3))
    out.extend(write_u16_le(len(shape_offsets)))
    for value in shape_offsets:
        out.extend(write_u16_le(int(value)))
    out.extend(write_u16_le(len(model_data)))
    out.extend(model_data)

    pool = payload.get("shared_vertex_pool")
    if pool:
        x_indices = [int(v) & 0xFF for v in pool["x_indices"]]
        y_indices = [int(v) & 0xFF for v in pool["y_indices"]]
        z_indices = [int(v) & 0xFF for v in pool["z_indices"]]
        x_values = [int(v) for v in pool["x_values"]]
        y_values = [int(v) for v in pool["y_values"]]
        z_values = [int(v) for v in pool["z_values"]]

        if not (len(x_indices) == len(y_indices) == len(z_indices)):
            raise ValueError("x/y/z index table lengths must match")

        count = len(x_indices)
        out.append(count & 0xFF)
        out.extend(bytes(x_indices))
        out.extend(bytes(y_indices))
        out.extend(bytes(z_indices))

        out.append(len(x_values) & 0xFF)
        for value in x_values:
            out.extend(int(value).to_bytes(2, "little", signed=True))
        out.append(len(y_values) & 0xFF)
        for value in y_values:
            out.extend(int(value).to_bytes(2, "little", signed=True))
        out.append(len(z_values) & 0xFF)
        for value in z_values:
            out.extend(int(value).to_bytes(2, "little", signed=True))

    trailing = payload.get("trailing_bytes")
    if trailing:
        out.extend(from_base64(trailing))

    return bytes(out)

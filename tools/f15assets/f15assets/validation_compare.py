"""Compatibility facade for replacement-asset validation comparisons.

Format-specific comparison code lives in validation_compare_* modules. Keep this
module as the stable import point for CLI code and downstream scripts.
"""

from __future__ import annotations

from .validation_compare_core import (
    compare_byte_sequence,
    compare_count_value,
    compare_mapping_exact,
    compare_sequence_exact,
    first_byte_difference,
)
from .validation_compare_font import compare_font_atlas_rows, compare_font_glyph96
from .validation_compare_image import compare_png_palette, compare_png_pixels
from .validation_compare_model3d import compare_glmesh_primitive_streams
from .validation_compare_sound import compare_wav_sample_rate, compare_wav_samples
from .validation_compare_structured import compare_structured_rebuild

__all__ = [
    "compare_byte_sequence",
    "compare_count_value",
    "compare_font_atlas_rows",
    "compare_font_glyph96",
    "compare_glmesh_primitive_streams",
    "compare_mapping_exact",
    "compare_png_palette",
    "compare_png_pixels",
    "compare_sequence_exact",
    "compare_structured_rebuild",
    "compare_wav_sample_rate",
    "compare_wav_samples",
    "first_byte_difference",
]

#!/usr/bin/env python3

# Copyright 2026 Roland Arsenault
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
#    * Neither the name of the Roland Arsenault nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""Decode and assemble SonarVisualizationTile bands (project ADR-0008 D1).

ROS-free so it is testable without a node: the functions take the plain
fields of a `marine_interfaces/VisualizationBand`, not the message.

Two details in the message contract are easy to get subtly wrong and both are
pinned by tests:

* **Dequantize generically.** `value = raw * scale + offset`, with the element
  type taken from `dtype` -- never hardcoded. The depth band is INT16 today,
  but uncertainty/backscatter/intensity are UINT8 and UINT16 is reserved for
  sidescan; hardcoding int16 would silently mis-decode three of the four.
* **`nodata` is a RAW sentinel, compared BEFORE dequantization.** Applying
  scale/offset first and then comparing would both miss the sentinel and turn
  it into a plausible-looking value -- e.g. -32768 * 0.01 = -327.68 m reads as
  a very deep sounding rather than "no data".

Tiles arrive as dirty sub-windows, not whole tiles, so a tile is assembled by
patching windows into a full-size array. See SonarVisualizationTile.
"""

import numpy

# Mirrors VisualizationBand's constants, which in turn mirror
# sensor_msgs/PointField numeric datatypes. Kept local for the same reason the
# message keeps them local: no sensor_msgs dependency.
UINT8 = 2
INT16 = 3
UINT16 = 4

_DTYPES = {
    UINT8: numpy.dtype(numpy.uint8).newbyteorder('<'),
    INT16: numpy.dtype(numpy.int16).newbyteorder('<'),
    UINT16: numpy.dtype(numpy.uint16).newbyteorder('<'),
}


def decode_band(dtype, data, width, height, scale, offset, nodata):
    """Return a float32 array of the window, NaN where the cell is empty.

    `data` is the raw little-endian buffer of `width * height` cells in
    row-major GGGS cell order. Raises ValueError on an unknown dtype or a
    buffer whose length disagrees with the window -- a truncated patch must
    fail loudly rather than render a shifted image.
    """
    element = _DTYPES.get(dtype)
    if element is None:
        raise ValueError('unsupported VisualizationBand dtype {}'.format(dtype))

    expected = width * height * element.itemsize
    if len(data) != expected:
        raise ValueError(
            'band data is {} bytes, expected {} for a {}x{} window of {}'
            .format(len(data), expected, width, height, element))

    # Consume the message buffer directly. `bytes(data)` copies the whole
    # band before numpy ever looks at it -- about 1.8 MB per 960x960 uint16
    # tile, on the executor thread, for every message on a best-effort topic.
    # Nothing below mutates `raw` (every operation on it allocates a new
    # array), so it does not matter whether the view is writable; what matters
    # is that the ROS message owns the memory for the whole of this call and
    # `raw` is never returned. `memoryview` refuses a buffer numpy could not
    # have read anyway, so a sequence type without the buffer protocol falls
    # back to the copy rather than becoming a new failure mode.
    try:
        buffer = memoryview(data)
    except TypeError:
        buffer = bytes(data)
    raw = numpy.frombuffer(buffer, dtype=element).reshape(height, width)

    # Compare the sentinel in RAW units, before dequantization -- but only if
    # it is representable in this element type. Casting an out-of-range
    # sentinel WRAPS (-32768 -> 0 in uint8), which would mask real cells: a
    # UINT8 band carrying a leftover INT16 sentinel would read every zero cell
    # as empty. An unrepresentable sentinel simply cannot occur in the data, so
    # it matches nothing.
    sentinel = int(nodata)
    limits = numpy.iinfo(element)
    if limits.min <= sentinel <= limits.max:
        empty = raw == element.type(sentinel)
    else:
        empty = numpy.zeros(raw.shape, dtype=bool)

    values = raw.astype(numpy.float32) * numpy.float32(scale) \
        + numpy.float32(offset)
    return numpy.where(empty, numpy.float32(numpy.nan), values)


def new_tile(width, height):
    """Return an all-empty (NaN) tile array."""
    return numpy.full((height, width), numpy.nan, dtype=numpy.float32)


def apply_window(tile, values, window_row, window_col):
    """Patch a decoded window into a full tile array, in place.

    Raises ValueError if the window does not fit: a patch that runs off the
    edge means the producer and consumer disagree about tile geometry, and
    silently clipping it would corrupt the tile with no signal.
    """
    height, width = values.shape
    if (window_row < 0 or window_col < 0
            or window_row + height > tile.shape[0]
            or window_col + width > tile.shape[1]):
        raise ValueError(
            'window {}x{} at ({},{}) does not fit tile {}x{}'
            .format(width, height, window_col, window_row,
                    tile.shape[1], tile.shape[0]))
    tile[window_row:window_row + height,
         window_col:window_col + width] = values


def time_to_nanoseconds(stamp):
    """Return a builtin_interfaces/Time as an integer nanosecond count.

    Versions are compared for newest-wins and against the catalog's generation
    time, so they must be a single ordered scalar rather than a pair.
    """
    return int(stamp.sec) * 1000000000 + int(stamp.nanosec)

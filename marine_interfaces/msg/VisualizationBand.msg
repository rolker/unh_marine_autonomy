# One named, self-describing quantized band of a SonarVisualizationTile.
# The consumer dequantizes generically -- value = raw * scale + offset -- with
# no hardcoded per-band knowledge. `data` holds the raw quantized cells of the
# tile's dirty sub-window (see SonarVisualizationTile), row-major in GGGS cell
# order, length = window_width * window_height * <sizeof dtype>. See ADR-0008 D1.

string name    # band identity: "depth" | "uncertainty" | "backscatter" | "intensity"

# Element type of the packed `data` bytes, little-endian. Values mirror
# sensor_msgs/PointField numeric datatypes, kept as local constants so this
# package takes no sensor_msgs dependency. Only the types v1 quantizes to are
# listed (depth -> INT16; uncertainty/backscatter/intensity -> UINT8).
uint8 UINT8  = 2
uint8 INT16  = 3
uint8 UINT16 = 4
uint8 dtype

float64 scale    # dequantization gain  (value = raw * scale + offset)
float64 offset   # dequantization bias
float64 nodata   # raw sentinel marking an empty/unknown cell, in RAW units
                 # (compared before dequantization), e.g. -32768 for INT16 depth

uint8[] data     # row-major raw cells of the dirty window, little-endian

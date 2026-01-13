# An orientaiton in free space in Euler form, using common navigator's conventions. 

# According to the Applanix POS MV V5 Installation and Operation Guide:
# X is positive towards the bow of the vessel.
# Y is positive towards starboard.
# Z is positive down.
# The Tate-Bryant sequence of rotations is used.
# Heading rotation - apply a right-hand screw rotation θz about the z-axis
# Pitch rotation - apply a right-hand screw rotation θy about the once-rotated y-axis
# Roll rotation - apply a right-hand screw rotation θx about the twice-rotated x-axis

# Angles are in degrees.

float64 roll
float64 pitch
float64 heading 

# Info about a contact

uint8 CONTACT_SOURCE_UNSPECIFIED = 0
uint8 CONTACT_SOURCE_AIS         = 1
uint8 CONTACT_SOURCE_RADAR       = 2

std_msgs/Header header

uint8 contact_source

uint32 mmsi                     # Maritime Mobile Service Identity
string name
string callsign

geographic_msgs/GeoPoint position
float32 cog                     # Course over ground (radians clockwise from north) [-1 = unavailable]
float32 sog                     # Speed over ground (m/s) [-1 = unavailable]
float32 heading                 # True heading (radians clockwise from north) [-1 = unavailable]

float32 dimension_to_stbd       # Distance between position source and starboard end of the contact (m) [0 = unavailable]
float32 dimension_to_port       # Distance between position source and port end of the contact (m) [0 = unavailable]
float32 dimension_to_bow        # Distance between position source and the forward most end of the contact (m) [0 = unavailable]
float32 dimension_to_stern      # Distance between position source and aft most end of the contact (m) [0 = unavailable]

# Section of a curved trajectory

int8    DIRECTION_RIGHT    = -1
int8    DIRECTION_STRAIGHT = 0
int8    DIRECTION_LEFT     = 1

int8                    direction    # one of above constants
float64                 length       # distance in meters
float64                 radius       # radius in meters for non-straight sections
builtin_interfaces/Time arrival_time # target time to reach the end of the curve


'''track_patterns.py

Val Schmidt
Center for Coastal and Ocean Mapping
University of New Hampshire
Copyright 2023

This module contains routines that create canned track patterns, which can be
used as building blocks to create larger missions or to dynamically respond to 
changing situations quickly. Track patterns are returned as a list of 
project11_nav_msgs/TaskInformation messages which can be passed on directly to
the MissionManager node, which in turn, will then update the Navigator node 
for execution. (See the mission_manager service for updating the task queue.)

Routines in this library operate in Euclidean space unless their documentation 
explicitly allows for on-the-fly translation. The return TaskInformation() 
messages provide a list of Poses. 

Typically a pattern is centered or bounded on some point. The return navigation
task will be provided in the same frame_id as the input.

Typical Usage:
--------------
import track_patterns
from geometry_msgs.msg import PoseStamped

Ps = PoseStamped()
## Set Ps position and frame_id here.
# <<<>>>

S = ExpandingBoxSearch(name='searchtest',
                        startLocation = Ps,
                        startHeading = 0,
                        searchSpeedKts = 5,
                        loopSpacing = 500,
                        maxSearchRadius = 15000)
S.create()
# The resulting list of navigation tasks will be found in S.pattern
# Send S.pattern to the mission manager here.
)

'''


from project11_nav_msgs.msg import TaskInformation
from geometry_msgs.msg import PoseStamped
import numpy as np
import yaml
import copy

def poseFromDistanceAndHeading(start, distance, heading):
    '''Returns a pose some distance and heading from a start
    
    Args:
        start:      Initial pose
        distance:   distance in m
        heading:    Azimuth in degrees True
        
    Returns:
        pose:       PostStamped message containing the point.'''

    po = start.pose.position
    P = PoseStamped()
    P.header.frame_id = start.header.frame_id
    p1 = P.pose.position
    p1.x = po.x + distance * np.cos(heading * np.pi/180) 
    p1.y = po.y + distance * np.sin(heading * np.pi/180) 
    return P

def pose2poseDistance(pose1,pose2):
    '''Calculates the distance between two Poses (in the same euclidean frame)
    
    Arg: 
        pose1:  PoseStamped
        pose2;  PoseStamped
        
        '''
    # TODO: Verify they are in the same frame and transform them if not.
    p1 = pose1.pose.position
    p2 = pose2.pose.position
    return np.sqrt((p2.x-p1.x)**2 + (p2.y-p1.y)**2)

def translatedPose(start,dx,dy):
    '''Return a new pose, translated from the start by dx and dy'''
    P = copy.deepcopy(start)
    P.pose.position.x += dx
    P.pose.position.y += dy
    return P


class ExpandingBoxSearch():
    '''A class to create an expanding box search pattern
    
    Args:
        name:           Mission element name
        startLocation:  PoseStamped. Return pattern is in this map frame
        startHeading:   Initial heading of search, deg T. (default 0, e.g. 000T)
        searchSpeedKts: Search speed (default 8).
        loopSpacing:    Spacing between each loop, m. (default 300)
        searchDirection:"clockwise" (default) or "counterclockwise"
        maxSearchRadius:Maximum search radius, m.  (default 10000)  

    Returns:
        project11_nav_msgs.msg.TaskInformation() object containing a 
        trackline for the search.
        
    '''
    
    def __init__(self,
                 name=None,
                startLocation = PoseStamped(),
                startHeading=0,
                searchSpeedKts = 8,
                loopSpacing = 300,
                maxSearchRadius = 10000,
                searchDirection = "clockwise"):


        self.name = name
        self.startLocation = startLocation
        self.startHeading = startHeading + 90 # Makes 0 north.
        self.loopSpacing = loopSpacing
        self.maxSearchRadius = maxSearchRadius
        self.searchDirection = searchDirection
        self.searchSpeed = searchSpeedKts * 0.514444 # knots to m/s

        self.pattern = TaskInformation()

    def create(self, id = None):
        '''Creates an Expanding Box Search Pattern'''

        if id is None:
            # Setup the task. Use a random id so it is unique.
            self.pattern.id = 'search_pattern' + str(np.random.randint(1e6))
        else:
            self.pattern.id = id
        self.pattern.type = 'survey_line'
        self.pattern.priority = 0
        self.pattern.data = yaml.safe_dump({'speed': self.searchSpeed})
        self.pattern.poses.append(self.startLocation)

        heading = self.startHeading 
        segmentLength = self.loopSpacing
        z = 1
        # Continue adding waypoints until we reach the maximum search radius.
        while (pose2poseDistance(self.startLocation,
                            self.pattern.poses[-1]) <
                            self.maxSearchRadius):
            
            self.pattern.poses.append(
                poseFromDistanceAndHeading(self.pattern.poses[-1],
                                     segmentLength, 
                                     heading)
            )

            # Note reversed convention here, bc in euclidian space, angles
            # are measured in the opposite direction from geogrpahic heading.
            if self.searchDirection == "clockwise":
                heading -= 90
            else:
                heading += 90

            if np.mod(z,2) == 0:
                segmentLength = segmentLength + self.loopSpacing
            z+=1
        return self.pattern

class RaceTrackPattern():
    '''A class to create a sliding rectangle racetrack pattern.
    
    startLocation:      PoseStamped of the start point.
    startHeading:       Heading in degrees of the first line.
    dimX:               X-dimension of the sliding rectangle.
    dimY:               Y-dimension of the sliding rectangle.
    lapSpacinginX:      Amount to slide the rectangle in the X direction.
    lapSpacinginY;      Amount to slide the rectangle in the Y direction.
    maxRadius:          How far away from the start to continue the pattern.
    speedKts:           Survey speed in knots (default 8)
    
    '''
    def __init__(self,
                 name = None,
                 startLocation = PoseStamped(),
                 startHeading = 0,
                 dimX = 100,
                 dimY = 500,
                 lapSpacinginX = 100,
                 lapSpacingInY = 100,
                 maxRadius = 1000,
                 speedKts = 8):

        self.name = name
        self.startLocation = startLocation
        self.dimX = dimX
        self.dimY = dimY
        self.startHeading = startHeading + 90 # Makes it north up.
        self.lapSpacingInX = lapSpacinginX
        self.lapSpacingInY = lapSpacingInY
        self.maxRadius = maxRadius
        self.speed = speedKts * 0.514444 # knots to m/s

        self.pattern = TaskInformation()

    def create(self):

        # Setup the task. Use a random id so it is unique.
        self.pattern.id = 'racetrack_pattern' + str(np.random.randint(1e6))
        self.pattern.type = 'survey_line'
        self.pattern.priority = 0
        self.pattern.data = yaml.safe_dump({'speed': self.speed})

        # Get a reference to the list of poses for the track.
        box = self.pattern.poses
        box.append(self.startLocation)
        heading = self.startHeading
        z = 0
            
        box.append(poseFromDistanceAndHeading(box[-1],
                                            self.dimY + self.lapSpacingInY,
                                            heading))
        heading -= 90
        box.append(poseFromDistanceAndHeading(box[-1],
                                              self.dimX + self.lapSpacingInX,                                                  heading))
        heading -= 90
        box.append(poseFromDistanceAndHeading(box[-1],
                                              self.dimY + self.lapSpacingInY,
                                              heading))
        heading -= 90   

        while pose2poseDistance(self.startLocation,box[-1]) < self.maxRadius:
            if z==1:
                box.append(translatedPose(box[-4],self.lapSpacingInX,0))
            else:
                box.append(translatedPose(box[-4],self.lapSpacingInX,self.lapSpacingInY))
                
            box.append(translatedPose(box[-4],self.lapSpacingInX,self.lapSpacingInY))
            box.append(translatedPose(box[-4],self.lapSpacingInX,self.lapSpacingInY))
            box.append(translatedPose(box[-4],self.lapSpacingInX,self.lapSpacingInY))
            z +=1

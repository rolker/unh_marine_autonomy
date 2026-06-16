# One open-vocabulary classification hypothesis for a perception Contact.

string  class_id        # open vocabulary (look the name up in a metadata source);
                        # YOLO/vision_msgs labels map straight in. A human-drawn
                        # contact is a single hypothesis at probability 1.0.
float32 probability     # [0, 1]

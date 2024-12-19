#ifndef PROJECT11_TF2_UTILS_H
#define PROJECT11_TF2_UTILS_H

#include "rclcpp/rclcpp.hpp"
#include "tf2_ros/transform_listener.h"
#include "tf2_ros/buffer.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "geographic_msgs/msg/geo_point.hpp"
#include "geometry_msgs/msg/point.hpp"
#include "project11/utils.h"
#include <cmath>

namespace project11
{
  class Transformations
  {
  public:
      Transformations(rclcpp::Node::SharedPtr node):
        node_(node)
      {
        tf_buffer_ = std::make_unique<tf2_ros::Buffer>(node_->get_clock());
        tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);
      }

      bool canTransform(std::string map_frame="map", rclcpp::Time target_time = rclcpp::Time(), tf2::Duration timeout = tf2::durationFromSec(0.5))
      {
        return buffer()->canTransform("earth", map_frame, target_time, timeout);
      }

      geometry_msgs::msg::Point wgs84_to_map(geographic_msgs::msg::GeoPoint const &position, std::string map_frame="map", rclcpp::Time target_time = rclcpp::Time())
      {
        geometry_msgs::msg::Point ret;
        try
        {
          geometry_msgs::msg::TransformStamped t = buffer()->lookupTransform(map_frame,"earth",target_time);
          LatLongDegrees p_ll;
          fromMsg(position, p_ll);
          bool alt_is_nan = std::isnan(position.altitude);
          if(alt_is_nan)
            p_ll.altitude() = 0.0;
          ECEF p_ecef = p_ll;
          geometry_msgs::msg::Point in;
          toMsg(p_ecef, in);
          tf2::doTransform(in,ret,t);
          if(alt_is_nan)
            ret.z = std::nan("");
        }
        catch (tf2::TransformException &ex)
        {
          RCLCPP_WARN_STREAM(node_->get_logger(), "Transformations: " << ex.what());
        }
        return ret;
      }
      
      geographic_msgs::msg::GeoPoint map_to_wgs84(geometry_msgs::msg::Point const &point, std::string map_frame="map", rclcpp::Time target_time = rclcpp::Time())
      {
          geographic_msgs::msg::GeoPoint ret;
          try
          {
            geometry_msgs::msg::TransformStamped t = buffer()->lookupTransform("earth",map_frame, target_time);
            geometry_msgs::msg::Point out;
            tf2::doTransform(point,out,t);
            ECEF out_ecef;
            fromMsg(out,out_ecef);
            LatLongDegrees latlon = out_ecef;
            toMsg(latlon, ret);
          }
          catch (tf2::TransformException &ex)
          {
            RCLCPP_WARN_STREAM(node_->get_logger(), "Transformations: " << ex.what());
          }
          return ret;
      }
      
      const std::unique_ptr<tf2_ros::Buffer> &operator()() const
      {
        return buffer();
      }
      
    private:
      const std::unique_ptr<tf2_ros::Buffer> &buffer() const
      {
        return tf_buffer_;
      }

      rclcpp::Node::SharedPtr node_;

      std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
      std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
    };
}

#endif

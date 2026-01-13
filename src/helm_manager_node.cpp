// Roland Arsenault
// Center for Coastal and Ocean Mapping
// University of New Hampshire
// Copyright 2021, All rights reserved.

#include "helm_manager.h"


int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  auto node = std::make_shared<helm_manager::HelmManager>("helm_manager");
  rclcpp::spin(node->get_node_base_interface());
  return 0;
}

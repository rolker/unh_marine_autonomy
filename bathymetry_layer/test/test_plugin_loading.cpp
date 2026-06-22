// Copyright (c) 2026 Roland Arsenault
// Licensed under BSD license

#include <gtest/gtest.h>

#include <memory>

#include "nav2_costmap_2d/layer.hpp"
#include "pluginlib/class_loader.hpp"

// Verify that BathymetryLayer can be loaded via pluginlib from its
// costmap_plugins.xml descriptor. This catches plugin registration issues
// (wrong class name, missing export, broken shared library) that unit tests
// exercising the class directly would miss.
TEST(BathymetryLayerPluginTest, LoadsViaPluginlib)
{
  pluginlib::ClassLoader<nav2_costmap_2d::Layer> loader(
    "nav2_costmap_2d", "nav2_costmap_2d::Layer");

  std::shared_ptr<nav2_costmap_2d::Layer> plugin;
  ASSERT_NO_THROW(
    plugin = loader.createSharedInstance("bathymetry_layer::BathymetryLayer"));
  EXPECT_NE(plugin, nullptr);
}

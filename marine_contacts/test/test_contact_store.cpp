// Copyright 2026 Roland Arsenault
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "marine_contacts/contact_store.hpp"
#include "marine_interfaces/msg/contact.hpp"

using marine_contacts::ContactStore;
using marine_contacts::MapPoint;
using marine_contacts::make_box_contact;
using marine_contacts::export_contacts_geojson;
using Contact = marine_interfaces::msg::Contact;

TEST(ContactStore, BoxContactCentroidAndExtent)
{
  const std::vector<MapPoint> box{{10.0, 20.0}, {14.0, 26.0}};
  const Contact c = make_box_contact(box, "T-001", "sidescan.port", "bizzy/map", 1234.5);
  EXPECT_EQ(c.header.frame_id, "bizzy/map");
  EXPECT_EQ(c.id, "T-001");
  EXPECT_EQ(c.source, "sidescan.port");
  EXPECT_FLOAT_EQ(c.existence_probability, 1.0f);
  EXPECT_EQ(c.origin_kind, Contact::ORIGIN_HUMAN);
  EXPECT_EQ(c.status, Contact::STATUS_PROPOSED);
  EXPECT_EQ(c.shape.type, marine_interfaces::msg::Shape::BOX);
  EXPECT_NEAR(c.kinematics.pose.pose.position.x, 12.0, 1e-9);  // centroid
  EXPECT_NEAR(c.kinematics.pose.pose.position.y, 23.0, 1e-9);
  EXPECT_NEAR(c.shape.dimensions.x, 4.0, 1e-9);                // extent
  EXPECT_NEAR(c.shape.dimensions.y, 6.0, 1e-9);
  EXPECT_TRUE(std::isnan(c.geo_pose.position.latitude));       // unresolved
  EXPECT_EQ(c.header.stamp.sec, 1234);
}

TEST(ContactStore, InBoxQuery)
{
  ContactStore s;
  s.add(make_box_contact({{0.0, 0.0}}, "a", "src", "bizzy/map", 0.0));
  s.add(make_box_contact({{100.0, 100.0}}, "b", "src", "bizzy/map", 0.0));
  EXPECT_EQ(s.size(), 2u);
  const auto hits = s.inBox(-5.0, -5.0, 5.0, 5.0);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits.front()->id, "a");
}

TEST(ContactStore, SaveLoadRoundTrip)
{
  ContactStore s;
  s.add(make_box_contact({{10.0, 20.0}, {14.0, 26.0}}, "T-001", "sidescan.port",
    "bizzy/map", 1234.5));
  s.add(make_box_contact({{-3.0, 7.0}}, "T-002", "sidescan.starboard", "bizzy/map", 9.0));

  const std::string path = std::string(testing::TempDir()) + "mc_contacts_test.cdr";
  ASSERT_TRUE(s.save(path));

  ContactStore loaded;
  ASSERT_TRUE(loaded.load(path));
  ASSERT_EQ(loaded.size(), 2u);
  EXPECT_EQ(loaded.contacts()[0].id, "T-001");
  EXPECT_NEAR(loaded.contacts()[0].kinematics.pose.pose.position.x, 12.0, 1e-9);
  EXPECT_NEAR(loaded.contacts()[0].shape.dimensions.y, 6.0, 1e-9);
  EXPECT_EQ(loaded.contacts()[1].id, "T-002");
  EXPECT_EQ(loaded.contacts()[1].source, "sidescan.starboard");
}

TEST(ContactStore, LoadMissingFileFails)
{
  ContactStore s;
  EXPECT_FALSE(s.load(std::string(testing::TempDir()) + "does_not_exist_mc.cdr"));
}

TEST(ContactStore, GeoJsonExportWritesResolvedSkipsUnreferenced)
{
  // One geo-resolved contact + one with no geo reference (NaN latitude by default).
  Contact resolved = make_box_contact(
    {{10.0, 20.0}, {14.0, 26.0}}, "T-001", "sidescan", "bizzy/map", 1234.5);
  resolved.geo_pose.position.latitude = 43.05;
  resolved.geo_pose.position.longitude = -71.40;
  resolved.geo_pose.position.altitude = 52.3;

  std::vector<Contact> contacts{
    resolved,
    make_box_contact({{-3.0, 7.0}}, "T-002", "sidescan", "bizzy/map", 9.0)};

  const std::string path = std::string(testing::TempDir()) + "mc_contacts_test.geojson";
  const auto r = export_contacts_geojson(contacts, path);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.written, 1u);   // only the geo-resolved one
  EXPECT_EQ(r.skipped, 1u);   // the NaN-latitude one

  std::ifstream f(path);
  ASSERT_TRUE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string text = ss.str();
  EXPECT_NE(text.find("\"FeatureCollection\""), std::string::npos);
  EXPECT_NE(text.find("\"id\": \"T-001\""), std::string::npos);
  EXPECT_EQ(text.find("T-002"), std::string::npos);   // skipped, not present
  EXPECT_NE(text.find("-71.40"), std::string::npos);  // lon first (GeoJSON order)
  EXPECT_NE(text.find("43.05"), std::string::npos);
}

TEST(ContactStore, GeoJsonExportSanitizesNonFiniteProperties)
{
  // A geo-resolved contact with a non-finite dimension (e.g. from a corrupt load)
  // must still produce valid JSON — no bare nan/inf token.
  Contact c = make_box_contact({{0.0, 0.0}}, "T-001", "sidescan", "bizzy/map", 1.0);
  c.geo_pose.position.latitude = 43.0;
  c.geo_pose.position.longitude = -71.0;
  c.shape.dimensions.x = std::numeric_limits<double>::quiet_NaN();
  c.shape.dimensions.y = std::numeric_limits<double>::infinity();

  const std::string path = std::string(testing::TempDir()) + "mc_contacts_nonfinite.geojson";
  const auto r = export_contacts_geojson({c}, path);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.written, 1u);

  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string text = ss.str();
  EXPECT_EQ(text.find("nan"), std::string::npos);
  EXPECT_EQ(text.find("inf"), std::string::npos);
}

TEST(ContactStore, GeoJsonExportEmptyOnNoGeo)
{
  std::vector<Contact> contacts{
    make_box_contact({{0.0, 0.0}}, "T-001", "sidescan", "bizzy/map", 1.0)};
  const std::string path = std::string(testing::TempDir()) + "mc_contacts_nogeo.geojson";
  const auto r = export_contacts_geojson(contacts, path);
  EXPECT_TRUE(r.ok);          // valid (empty) FeatureCollection still written
  EXPECT_EQ(r.written, 0u);
  EXPECT_EQ(r.skipped, 1u);
}

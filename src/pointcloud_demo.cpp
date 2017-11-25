#include <iostream>
#include <string>

#include "ros/ros.h"
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/PointCloud2.h>
#include <cv_bridge/cv_bridge.h>
#include "pcl_conversions/pcl_conversions.h"
#include "tf/transform_listener.h"
#include "pcl_ros/transforms.h"

#include "custom_landmark_2d/rgbd_pointcloud.h"


typedef pcl::PointXYZRGB PointC;
typedef pcl::PointCloud<pcl::PointXYZRGB> PointCloudC;

using namespace sensor_msgs;
using namespace message_filters;

void callback(const ImageConstPtr& rgb, const ImageConstPtr& depth, ros::Publisher& cloud_pub) {
  ROS_INFO("received 2 images");

  // fetch CameraInfo
  sensor_msgs::CameraInfoConstPtr camera_info =
      ros::topic::waitForMessage<sensor_msgs::CameraInfo>(
          "/head_camera/rgb/camera_info");

  ROS_INFO("received camear_info");  

  ROS_INFO("depth encoding: %s\n", depth->encoding.c_str()); 

  // convert sensor_msgs::Images to cv::Mats
  cv_bridge::CvImagePtr rgb_ptr;
  cv_bridge::CvImagePtr depth_ptr;

  try {
    rgb_ptr = cv_bridge::toCvCopy(rgb, sensor_msgs::image_encodings::BGR8);
    depth_ptr = cv_bridge::toCvCopy(depth); // 32FC1
  } 
  catch (cv_bridge::Exception& e) {
    ROS_ERROR("cv_bridge exception: %s", e.what());
    ros::shutdown(); 
  }

  // cv::Mat depth_16uc1 = cv::Mat(depth_ptr->image.size(), CV_16UC1);
  // cv::convertScaleAbs(depth_ptr->image, depth_16uc1, 100, 0.0);


  custom_landmark_2d::RgbdPointCloud rgbd_pointcloud(camera_info);
  PointCloudC::Ptr pcl_cloud = rgbd_pointcloud.to_pointcloud(rgb_ptr->image, depth_ptr->image);

  sensor_msgs::PointCloud2 ros_cloud;
  pcl::toROSMsg(*pcl_cloud, ros_cloud);
  ros_cloud.header.frame_id = camera_info->header.frame_id; // head_camera_rgb_optical_frame

  tf::TransformListener tf_listener;
  tf_listener.waitForTransform("base_link", ros_cloud.header.frame_id,
                               ros::Time(0), ros::Duration(5.0));
  tf::StampedTransform transform;

  try {     
    tf_listener.lookupTransform("base_link", ros_cloud.header.frame_id,
                                ros::Time(0), transform);
  } catch (tf::LookupException& e) {
    std::cerr << e.what() << std::endl;
    ros::shutdown(); 
  } catch (tf::ExtrapolationException& e) {
    std::cerr << e.what() << std::endl;
    ros::shutdown(); 
  }

  sensor_msgs::PointCloud2 cloud_out;
  // Transform a sensor_msgs::PointCloud2 dataset from its frame to a given TF target frame. 
  pcl_ros::transformPointCloud("base_link", transform, ros_cloud, cloud_out);

  cloud_pub.publish(ros_cloud);

  ros::shutdown(); 
}

int main(int argc, char** argv) {

  ros::init(argc, argv, "pointcloud_demo");

  ros::NodeHandle nh;  

  ros::Publisher cloud_pub =
    nh.advertise<sensor_msgs::PointCloud2>("generated_cloud", 1, true);

  message_filters::Subscriber<Image> rgb_sub(nh, "/head_camera/rgb/image_raw", 1);
  message_filters::Subscriber<Image> depth_sub(nh, "/head_camera/depth_registered/image_raw", 1);

  typedef sync_policies::ApproximateTime<Image, Image> SyncPolicy;
  // ApproximateTime takes a queue size as its constructor argument, hence SyncPolicy(10)
  Synchronizer<SyncPolicy> sync(SyncPolicy(10), rgb_sub, depth_sub);
  sync.registerCallback(boost::bind(&callback, _1, _2, cloud_pub));

  ros::spin();

  return 0;
}
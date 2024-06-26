/** path_tracker_node.cpp
 * 
 * Copyright (C) 2024 Shuo SUN & Advanced Robotics Center, National University of Singapore
 * 
 * MIT License
 * 
 * ROS Node for robot to track a given path
 */

#include "angles/angles.h"
#include "me5413_world/math_utils.hpp"
#include "me5413_world/path_tracker_node.hpp"

namespace me5413_world 
{

// Dynamic Parameters
double SPEED_TARGET;
double PID_Kp, PID_Ki, PID_Kd;
double STANLEY_K;
double look_ahead_dist; // Pure pursuit look ahead distance
double yaw_Kp; // Angular velocity proportional coefficient
bool PARAMS_UPDATED;


// A dynamic parameter callback function to set dynamic parameters upon changes
void dynamicParamCallback(const me5413_world::path_trackerConfig& config, uint32_t level)
{
  // Common Params
  SPEED_TARGET = config.speed_target;
  // PID 
  PID_Kp = config.PID_Kp;
  PID_Ki = config.PID_Ki;
  PID_Kd = config.PID_Kd;
  // Stanley
  STANLEY_K = config.stanley_K;

  // Pure path tracking
  look_ahead_dist = config.look_ahead_dist;
  yaw_Kp = config.yaw_Kp;
  PARAMS_UPDATED = true;
}

PathTrackerNode::PathTrackerNode() : tf2_listener_(tf2_buffer_)
{
  f = boost::bind(&dynamicParamCallback, _1, _2);
  server.setCallback(f);

  this->sub_robot_odom_ = nh_.subscribe("/gazebo/ground_truth/state", 1, &PathTrackerNode::robotOdomCallback, this);
  this->sub_local_path_ = nh_.subscribe("/me5413_world/planning/local_path", 1, &PathTrackerNode::localPathCallback, this);
  this->pub_cmd_vel_ = nh_.advertise<geometry_msgs::Twist>("/jackal_velocity_controller/cmd_vel", 1);

  // Initialization
  this->robot_frame_ = "base_link";
  this->world_frame_ = "world";

  this->pid_ = control::PID(0.1, 1.0, -1.0, PID_Kp, PID_Ki, PID_Kd);
}

void PathTrackerNode::localPathCallback(const nav_msgs::Path::ConstPtr& path)
{
  this->pub_cmd_vel_.publish(computeControlOutputs(this->odom_world_robot_, path));
  return;
}

// void PathTrackerNode::localPathCallback(const nav_msgs::Path::ConstPtr& path)
// {
//   // Calculate absolute errors (wrt to world frame)
//   this->pose_world_goal_ = path->poses[11].pose;
//   this->pub_cmd_vel_.publish(computeControlOutputs(this->odom_world_robot_, this->pose_world_goal_));

//   return;
// }

void PathTrackerNode::robotOdomCallback(const nav_msgs::Odometry::ConstPtr& odom)
{
  this->world_frame_ = odom->header.frame_id;
  this->robot_frame_ = odom->child_frame_id;
  this->odom_world_robot_ = *odom.get();

  return;
}

geometry_msgs::Point PathTrackerNode::findGoalPoint(const tf2::Vector3& point_robot, const nav_msgs::Path::ConstPtr& path, double look_ahead_dist)
{
    auto it = std::find_if(path->poses.begin(), path->poses.end(), [&](const geometry_msgs::PoseStamped& pose){
        tf2::Vector3 point_path;
        tf2::fromMsg(pose.pose.position, point_path);
        return tf2::tf2Distance(point_robot, point_path) >= look_ahead_dist;
    });

    if (it == path->poses.end())
        return path->poses.back().pose.position;

    return it->pose.position;
}

geometry_msgs::Twist PathTrackerNode::computeControlOutputs(const nav_msgs::Odometry& odom_robot, const nav_msgs::Path::ConstPtr& path)
{
  tf2::Quaternion q_robot;
  tf2::fromMsg(odom_robot.pose.pose.orientation, q_robot);
  const tf2::Matrix3x3 m_robot = tf2::Matrix3x3(q_robot);
  double roll, pitch, yaw_robot, yaw_goal;
  m_robot.getRPY(roll, pitch, yaw_robot);
  tf2::Vector3 point_robot, point_goal;
  tf2::fromMsg(odom_robot.pose.pose.position, point_robot);

  geometry_msgs::Twist cmd_vel;

  // pure path tracking
  geometry_msgs::Point goal_point = findGoalPoint(point_robot, path, look_ahead_dist);
  yaw_goal = atan2(goal_point.y - point_robot.y(), goal_point.x - point_robot.x());
  double yaw_error = angles::normalize_angle(yaw_goal - yaw_robot);
  cmd_vel.angular.z = yaw_Kp * yaw_error;

  // control velocity by PID
  tf2::Vector3 robot_vel;
  tf2::fromMsg(this->odom_world_robot_.twist.twist.linear, robot_vel);
  const double velocity = robot_vel.length();

  if (PARAMS_UPDATED)
  {
    this->pid_.updateSettings(PID_Kp, PID_Ki, PID_Kd);
    PARAMS_UPDATED = false;
  }
  cmd_vel.linear.x = this->pid_.calculate(SPEED_TARGET, velocity);

  return cmd_vel;
}

// double PathTrackerNode::computeStanleyControl(const double heading_error, const double cross_track_error, const double velocity)
// {
//   const double stanley_output = -1.0*(heading_error + std::atan2(STANLEY_K*cross_track_error, std::max(velocity, 0.3)));

//   return std::min(std::max(stanley_output, -2.2), 2.2);
// }

// geometry_msgs::Twist PathTrackerNode::computeControlOutputs(const nav_msgs::Odometry& odom_robot, const geometry_msgs::Pose& pose_goal)
// {
//   // Heading Error
//   tf2::Quaternion q_robot, q_goal;
//   tf2::fromMsg(odom_robot.pose.pose.orientation, q_robot);
//   tf2::fromMsg(pose_goal.orientation, q_goal);
//   const tf2::Matrix3x3 m_robot = tf2::Matrix3x3(q_robot);
//   const tf2::Matrix3x3 m_goal = tf2::Matrix3x3(q_goal);

//   double roll, pitch, yaw_robot, yaw_goal;
//   m_robot.getRPY(roll, pitch, yaw_robot);
//   m_goal.getRPY(roll, pitch, yaw_goal);

//   const double heading_error = unifyAngleRange(yaw_robot - yaw_goal);

//   // Lateral Error
//   tf2::Vector3 point_robot, point_goal;
//   tf2::fromMsg(odom_robot.pose.pose.position, point_robot);
//   tf2::fromMsg(pose_goal.position, point_goal);
//   const tf2::Vector3 V_goal_robot = point_robot - point_goal;
//   const double angle_goal_robot = std::atan2(V_goal_robot.getY(), V_goal_robot.getX());
//   const double angle_diff = angle_goal_robot - yaw_goal;
//   const double lat_error = V_goal_robot.length()*std::sin(angle_diff);

//   // Velocity
//   tf2::Vector3 robot_vel;
//   tf2::fromMsg(this->odom_world_robot_.twist.twist.linear, robot_vel);
//   const double velocity = robot_vel.length();

//   geometry_msgs::Twist cmd_vel;
//   if (PARAMS_UPDATED)
//   {
//     this->pid_.updateSettings(PID_Kp, PID_Ki, PID_Kd);
//     PARAMS_UPDATED = false;
//   }
//   cmd_vel.linear.x = this->pid_.calculate(SPEED_TARGET, velocity);
//   cmd_vel.angular.z = computeStanleyControl(heading_error, lat_error, velocity);

//   // std::cout << "robot velocity is " << velocity << " throttle is " << cmd_vel.linear.x << std::endl;
//   // std::cout << "lateral error is " << lat_error << " heading_error is " << heading_error << " steering is " << cmd_vel.angular.z << std::endl;

//   return cmd_vel;
// }


} // namespace me5413_world

int main(int argc, char** argv)
{
  ros::init(argc, argv, "path_tracker_node");
  me5413_world::PathTrackerNode path_tracker_node;
  ros::spin();  // spin the ros node.
  return 0;
}

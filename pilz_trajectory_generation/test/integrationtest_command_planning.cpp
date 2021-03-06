/*
 * Copyright (c) 2018 Pilz GmbH & Co. KG
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <memory>
#include <vector>

#include <ros/ros.h>

#include <eigen_conversions/eigen_msg.h>

#include <moveit_msgs/Constraints.h>
#include <moveit_msgs/JointConstraint.h>
#include <moveit_msgs/GetMotionPlan.h>
#include <moveit/kinematic_constraints/utils.h>
#include <moveit/robot_state/conversions.h>
#include <moveit/planning_interface/planning_request.h>
#include <moveit/robot_model_loader/robot_model_loader.h>

#include <pilz_industrial_motion_testutils/xml_testdata_loader.h>
#include <pilz_industrial_motion_testutils/motion_plan_request_director.h>
#include <pilz_industrial_motion_testutils/command_types_typedef.h>

#include "test_utils.h"

const double EPSILON=1.0e-6;
const std::string PLAN_SERVICE_NAME = "/plan_kinematic_path";

// Parameters from parameter server
const std::string PARAM_PLANNING_GROUP_NAME("planning_group");
const std::string POSE_TRANSFORM_MATRIX_NORM_TOLERANCE("pose_norm_tolerance");
const std::string ORIENTATION_NORM_TOLERANCE("orientation_norm_tolerance");
const std::string PARAM_TARGET_LINK_NAME("target_link");
const std::string TEST_DATA_FILE_NAME("testdata_file_name");

using namespace pilz_industrial_motion_testutils;

/**
 * PLEASE NOTE:
 * More detailed lin tests are done via unit tests. With the help of the
 * integration tests, it is only checked that a linear command actually
 * performs a linear command.
 */
class IntegrationTestCommandPlanning : public ::testing::Test
{
protected:
  virtual void SetUp();

protected:
  ros::NodeHandle ph_ {"~"};

  robot_model::RobotModelPtr robot_model_;

  double pose_norm_tolerance_, orientation_norm_tolerance_;
  std::string planning_group_, target_link_, test_data_file_name_;

  std::unique_ptr<pilz_industrial_motion_testutils::TestdataLoader> test_data_;

  unsigned int num_joints_ {0};
};

void IntegrationTestCommandPlanning::SetUp()
{
  // create robot model
  robot_model_loader::RobotModelLoader model_loader;
  robot_model_ = model_loader.getModel();

  // get the parameters
  ASSERT_TRUE(ph_.getParam(PARAM_PLANNING_GROUP_NAME, planning_group_));
  ASSERT_TRUE(ph_.getParam(POSE_TRANSFORM_MATRIX_NORM_TOLERANCE, pose_norm_tolerance_));
  ASSERT_TRUE(ph_.getParam(PARAM_TARGET_LINK_NAME, target_link_));
  ASSERT_TRUE(ph_.getParam(ORIENTATION_NORM_TOLERANCE, orientation_norm_tolerance_));
  ASSERT_TRUE(ph_.getParam(TEST_DATA_FILE_NAME, test_data_file_name_));

  // load the test data provider
  test_data_.reset(new pilz_industrial_motion_testutils::XmlTestdataLoader{test_data_file_name_, robot_model_});
  ASSERT_NE(nullptr, test_data_) << "Failed to load test data by provider.";

  num_joints_ = robot_model_->getJointModelGroup(planning_group_)->getActiveJointModelNames().size();
}

/**
 * @brief Tests if ptp motions with start & goal state given as
 * joint configuration are executed correctly.
 *
 * Test Sequence:
 *    1. Generate request with joint goal and start state call planning service.
 *
 * Expected Results:
 *    1. Last point of the resulting trajectory is at the goal
 */
TEST_F(IntegrationTestCommandPlanning, PtpJoint)
{
  ros::NodeHandle node_handle("~");
  auto ptp {test_data_->getPtpJoint("Ptp1")};

  moveit_msgs::GetMotionPlan srv;
  moveit_msgs::MotionPlanRequest req = ptp.toRequest();
  srv.request.motion_plan_request = req;

  ASSERT_TRUE(ros::service::waitForService(PLAN_SERVICE_NAME, ros::Duration(testutils::DEFAULT_SERVICE_TIMEOUT)));
  ros::ServiceClient client = node_handle.serviceClient<moveit_msgs::GetMotionPlan>(PLAN_SERVICE_NAME);

  ASSERT_TRUE(client.call(srv));
  const moveit_msgs::MotionPlanResponse& response {srv.response.motion_plan_response};

  // Check the result
  ASSERT_EQ(moveit_msgs::MoveItErrorCodes::SUCCESS, response.error_code.val) << "Planning failed!";
  trajectory_msgs::JointTrajectory trajectory = response.trajectory.joint_trajectory;

  EXPECT_EQ(trajectory.joint_names.size(), num_joints_) << "Wrong number of jointnames";
  EXPECT_GT(trajectory.points.size(), 0u) << "There are no points in the trajectory";

  // Check that every point has position, velocity, acceleration
  for(trajectory_msgs::JointTrajectoryPoint point : trajectory.points)
  {
    EXPECT_EQ(point.positions.size(), num_joints_);
    EXPECT_EQ(point.velocities.size(), num_joints_);
    EXPECT_EQ(point.accelerations.size(), num_joints_);
  }

  for(size_t i = 0; i < num_joints_; ++i)
  {
    EXPECT_NEAR(trajectory.points.back().positions.at(i), req.goal_constraints.back().joint_constraints.at(i).position, 10e-10);
    EXPECT_NEAR(trajectory.points.back().velocities.at(i), 0, 10e-10);
    // EXPECT_NEAR(trajectory.points.back().accelerations.at(i), 0, 10e-10); // TODO what is expected
  }
}

/**
 * @brief Tests if ptp motions with start state given as joint configuration
 * and goal state given as cartesian configuration are executed correctly.
 *
 * Test Sequence:
 *    1. Generate request with pose goal and start state call planning service.
 *
 * Expected Results:
 *    1. Last point of the resulting trajectory is at the goal
 */
TEST_F(IntegrationTestCommandPlanning, PtpJointCart)
{
  ros::NodeHandle node_handle("~");

  PtpJointCart ptp {test_data_->getPtpJointCart("Ptp1")};
  ptp.getGoalConfiguration().setPoseTolerance(0.01);
  ptp.getGoalConfiguration().setAngleTolerance(0.01);

  moveit_msgs::GetMotionPlan srv;
  srv.request.motion_plan_request = ptp.toRequest();

  ASSERT_TRUE(ros::service::waitForService(PLAN_SERVICE_NAME, ros::Duration(testutils::DEFAULT_SERVICE_TIMEOUT)));
  ros::ServiceClient client = node_handle.serviceClient<moveit_msgs::GetMotionPlan>(PLAN_SERVICE_NAME);

  ASSERT_TRUE(client.call(srv));
  const moveit_msgs::MotionPlanResponse& response {srv.response.motion_plan_response};

  // Make sure the planning succeeded
  ASSERT_EQ(moveit_msgs::MoveItErrorCodes::SUCCESS, response.error_code.val) << "Planning failed!";

  // Check the result
  trajectory_msgs::JointTrajectory trajectory = response.trajectory.joint_trajectory;

  EXPECT_EQ(trajectory.joint_names.size(), num_joints_) << "Wrong number of jointnames";
  EXPECT_GT(trajectory.points.size(), 0u) << "There are no points in the trajectory";

  // Check that every point has position, velocity, acceleration
  for(trajectory_msgs::JointTrajectoryPoint point : trajectory.points)
  {
    EXPECT_EQ(point.positions.size(), num_joints_);
    EXPECT_EQ(point.velocities.size(), num_joints_);
    EXPECT_EQ(point.accelerations.size(), num_joints_);
  }

  // TODO check that at right position
  robot_state::RobotState rstate(robot_model_);
  rstate.setJointGroupPositions(planning_group_,response.trajectory.joint_trajectory.points.back().positions);
  rstate.update();
  Eigen::Isometry3d tf = rstate.getFrameTransform(target_link_);

  const geometry_msgs::Pose& expected_pose {ptp.getGoalConfiguration().getPose()};
  EXPECT_NEAR(tf(0,3), expected_pose.position.x, EPSILON);
  EXPECT_NEAR(tf(1,3), expected_pose.position.y, EPSILON);
  EXPECT_NEAR(tf(2,3), expected_pose.position.z, EPSILON);

  Eigen::Isometry3d exp_iso3d_pose;
  tf::poseMsgToEigen(expected_pose, exp_iso3d_pose);

  EXPECT_TRUE(Eigen::Quaterniond(tf.rotation()).isApprox(Eigen::Quaterniond(exp_iso3d_pose.rotation()), EPSILON));
}

/**
 * @brief Tests if linear motions with start and goal state given
 * as joint configuration are executed correctly.
 *
 * Test Sequence:
 *  1. Generate request and make service request.
 *  2. Check if target position correct.
 *  3. Check if trajectory is linear.
 *
 * Expected Results:
 *  1. Planning request is successful.
 *  2. Goal position correponds with the given goal position.
 *  3. Trajectory is a straight line.
 */
TEST_F(IntegrationTestCommandPlanning, LinJoint)
{
  planning_interface::MotionPlanRequest req {test_data_->getLinJoint("lin2").toRequest()};

  std::cout << "++++++++++" << std::endl;
  std::cout << "+ Step 1 +" << std::endl;
  std::cout << "++++++++++" << std::endl;

  moveit_msgs::GetMotionPlan srv;
  srv.request.motion_plan_request = req;

  ASSERT_TRUE(ros::service::waitForService(PLAN_SERVICE_NAME, ros::Duration(testutils::DEFAULT_SERVICE_TIMEOUT)));
  ros::NodeHandle node_handle("~");
  ros::ServiceClient client = node_handle.serviceClient<moveit_msgs::GetMotionPlan>(PLAN_SERVICE_NAME);

  ASSERT_TRUE(client.call(srv));
  const moveit_msgs::MotionPlanResponse& response {srv.response.motion_plan_response};

  ASSERT_EQ(moveit_msgs::MoveItErrorCodes::SUCCESS, response.error_code.val) << "Planning failed!";

  std::cout << "++++++++++" << std::endl;
  std::cout << "+ Step 2 +" << std::endl;
  std::cout << "++++++++++" << std::endl;

  ASSERT_TRUE(testutils::isGoalReached(robot_model_,
                                       response.trajectory.joint_trajectory,
                                       req,
                                       pose_norm_tolerance_,
                                       orientation_norm_tolerance_)) << "Goal not reached.";

  std::cout << "++++++++++" << std::endl;
  std::cout << "+ Step 3 +" << std::endl;
  std::cout << "++++++++++" << std::endl;

  ASSERT_TRUE(testutils::checkCartesianLinearity(
                robot_model_,
                response.trajectory.joint_trajectory,
                req,
                pose_norm_tolerance_,
                orientation_norm_tolerance_)) << "Trajectory violates cartesian linearity.";
}

/**
 * @brief Tests if linear motions with start state given as joint
 * configuration and goal state given as cartesian configuration
 * are executed correctly.
 *
 * Test Sequence:
 *  1. Generate request and make service request.
 *  2. Check if target position correct.
 *  3. Check if trajectory is linear.
 *
 * Expected Results:
 *  1. Planning request is successful.
 *  2. Goal position correponds with the given goal position.
 *  3. Trajectory is a straight line.
 */
TEST_F(IntegrationTestCommandPlanning, LinJointCart)
{
  ros::NodeHandle node_handle("~");
  planning_interface::MotionPlanRequest req {test_data_->getLinJointCart("lin2").toRequest()};

  std::cout << "++++++++++" << std::endl;
  std::cout << "+ Step 1 +" << std::endl;
  std::cout << "++++++++++" << std::endl;

  moveit_msgs::GetMotionPlan srv;
  srv.request.motion_plan_request = req;

  ASSERT_TRUE(ros::service::waitForService(PLAN_SERVICE_NAME, ros::Duration(testutils::DEFAULT_SERVICE_TIMEOUT)));
  ros::ServiceClient client = node_handle.serviceClient<moveit_msgs::GetMotionPlan>(PLAN_SERVICE_NAME);

  ASSERT_TRUE(client.call(srv));
  const moveit_msgs::MotionPlanResponse& response {srv.response.motion_plan_response};

  ASSERT_EQ(moveit_msgs::MoveItErrorCodes::SUCCESS, response.error_code.val) << "Planning failed!";

  std::cout << "++++++++++" << std::endl;
  std::cout << "+ Step 2 +" << std::endl;
  std::cout << "++++++++++" << std::endl;

  ASSERT_TRUE(testutils::isGoalReached(robot_model_,
                                       response.trajectory.joint_trajectory,
                                       req,
                                       pose_norm_tolerance_,
                                       orientation_norm_tolerance_)) << "Goal not reached.";

  std::cout << "++++++++++" << std::endl;
  std::cout << "+ Step 3 +" << std::endl;
  std::cout << "++++++++++" << std::endl;

  ASSERT_TRUE(testutils::checkCartesianLinearity(
                robot_model_,
                response.trajectory.joint_trajectory,
                req,
                pose_norm_tolerance_,
                orientation_norm_tolerance_)) << "Trajectory violates cartesian linearity.";

}

/**
 * @brief Tests if circular motions with start & goal state given as joint
 * configuration and center point given as cartesian configuration
 * are executed correctly.
 *
 * Test Sequence:
 *    1. Generate request with JOINT goal and start state call planning service.
 *
 * Expected Results:
 *    1. Last point of the resulting trajectory is at the goal
 *    2. Waypoints are on the desired circle
 */
TEST_F(IntegrationTestCommandPlanning, CircJointCenterCart)
{
  ros::NodeHandle node_handle("~");

  CircJointCenterCart circ {test_data_->getCircJointCenterCart("circ1_center_2")};

  moveit_msgs::MotionPlanRequest req {circ.toRequest()};

  moveit_msgs::GetMotionPlan srv;
  srv.request.motion_plan_request = req;

  ASSERT_TRUE(ros::service::waitForService(PLAN_SERVICE_NAME, ros::Duration(testutils::DEFAULT_SERVICE_TIMEOUT)));
  ros::ServiceClient client = node_handle.serviceClient<moveit_msgs::GetMotionPlan>(PLAN_SERVICE_NAME);

  ASSERT_TRUE(client.call(srv));
  const moveit_msgs::MotionPlanResponse& response {srv.response.motion_plan_response};

  // Check the result
  ASSERT_EQ(moveit_msgs::MoveItErrorCodes::SUCCESS, response.error_code.val) << "Planning failed!";
  trajectory_msgs::JointTrajectory trajectory = response.trajectory.joint_trajectory;

  EXPECT_EQ(trajectory.joint_names.size(), num_joints_) << "Wrong number of jointnames";
  EXPECT_GT(trajectory.points.size(), 0u) << "There are no points in the trajectory";

  // Check that every point has position, velocity, acceleration
  for(trajectory_msgs::JointTrajectoryPoint point : trajectory.points)
  {
    EXPECT_EQ(point.positions.size(), num_joints_);
    EXPECT_EQ(point.velocities.size(), num_joints_);
    EXPECT_EQ(point.accelerations.size(), num_joints_);
  }

  // check goal is reached
  ASSERT_TRUE(testutils::isGoalReached(robot_model_,
                                       response.trajectory.joint_trajectory,
                                       req,
                                       pose_norm_tolerance_,
                                       orientation_norm_tolerance_)) << "Goal not reached.";

  // check all waypoints are on the circle and SLERP
  robot_state::RobotState waypoint_state(robot_model_);
  Eigen::Isometry3d waypoint_pose;
  double x_dist, y_dist, z_dist;

  const geometry_msgs::Pose& aux_pose {circ.getAuxiliaryConfiguration().getConfiguration().getPose()};

  CircCenterCart circ_cart {test_data_->getCircCartCenterCart("circ1_center_2")};
  const geometry_msgs::Pose& start_pose {circ_cart.getStartConfiguration().getPose()};
  const geometry_msgs::Pose& goal_pose {circ_cart.getGoalConfiguration().getPose()};

  x_dist = aux_pose.position.x - start_pose.position.x;
  y_dist = aux_pose.position.y - start_pose.position.y;
  z_dist = aux_pose.position.z - start_pose.position.z;
  double expected_radius = sqrt( x_dist*x_dist + y_dist*y_dist +  z_dist*z_dist );
  for(const auto & waypoint : trajectory.points)
  {
    waypoint_state.setJointGroupPositions(planning_group_, waypoint.positions);
    waypoint_pose = waypoint_state.getFrameTransform(target_link_);

    // Calculate (and check) distance of current trajectory waypoint from circ center
    x_dist = aux_pose.position.x - waypoint_pose(0,3);
    y_dist = aux_pose.position.y - waypoint_pose(1,3);
    z_dist = aux_pose.position.z - waypoint_pose(2,3);
    double actual_radius = sqrt( x_dist*x_dist + y_dist*y_dist +  z_dist*z_dist );
    EXPECT_NEAR(actual_radius, expected_radius, pose_norm_tolerance_) << "Trajectory way point is not on the circle.";

    // Check orientation
    Eigen::Isometry3d start_pose_iso3d, goal_pose_iso3d;
    tf::poseMsgToEigen(start_pose, start_pose_iso3d);
    tf::poseMsgToEigen(goal_pose, goal_pose_iso3d);
    EXPECT_TRUE( testutils::checkSLERP(start_pose_iso3d, goal_pose_iso3d, waypoint_pose, orientation_norm_tolerance_) );
  }
}

/**
 * @brief Tests if linear motions with start state given as cartesian
 * configuration and goal state given as cartesian configuration
 * are executed correctly.
 *
 *  - Test Sequence:
 *    1. Generate request with POSE goal and start state call planning service.
 *
 *  - Expected Results:
 *    1. Last point of the resulting trajectory is at the goal
 *    2. Waypoints are on the desired circle
 */
TEST_F(IntegrationTestCommandPlanning, CircCartCenterCart)
{
  ros::NodeHandle node_handle("~");

  CircCenterCart circ {test_data_->getCircCartCenterCart("circ1_center_2")};
  moveit_msgs::MotionPlanRequest req {circ.toRequest()};
  moveit_msgs::GetMotionPlan srv;
  srv.request.motion_plan_request = req;

  ASSERT_TRUE(ros::service::waitForService(PLAN_SERVICE_NAME, ros::Duration(testutils::DEFAULT_SERVICE_TIMEOUT)));
  ros::ServiceClient client = node_handle.serviceClient<moveit_msgs::GetMotionPlan>(PLAN_SERVICE_NAME);

  ASSERT_TRUE(client.call(srv));
  const moveit_msgs::MotionPlanResponse& response = srv.response.motion_plan_response;

  // Check the result
  ASSERT_EQ(moveit_msgs::MoveItErrorCodes::SUCCESS, response.error_code.val) << "Planning failed!";
  trajectory_msgs::JointTrajectory trajectory = response.trajectory.joint_trajectory;

  EXPECT_EQ(trajectory.joint_names.size(), num_joints_) << "Wrong number of jointnames";
  EXPECT_GT(trajectory.points.size(), 0u) << "There are no points in the trajectory";

  // Check that every point has position, velocity, acceleration
  for(trajectory_msgs::JointTrajectoryPoint point : trajectory.points)
  {
    EXPECT_EQ(point.positions.size(), num_joints_);
    EXPECT_EQ(point.velocities.size(), num_joints_);
    EXPECT_EQ(point.accelerations.size(), num_joints_);
  }

  // check goal is reached
  ASSERT_TRUE(testutils::isGoalReached(robot_model_,
                                       response.trajectory.joint_trajectory,
                                       req,
                                       pose_norm_tolerance_,
                                       orientation_norm_tolerance_)) << "Goal not reached.";

  // check all waypoints are on the cricle and SLERP
  robot_state::RobotState waypoint_state(robot_model_);
  Eigen::Isometry3d waypoint_pose;
  double x_dist, y_dist, z_dist;

  const geometry_msgs::Pose& start_pose {circ.getStartConfiguration().getPose()};
  const geometry_msgs::Pose& aux_pose {circ.getAuxiliaryConfiguration().getConfiguration().getPose()};
  const geometry_msgs::Pose& goal_pose {circ.getGoalConfiguration().getPose()};

  x_dist = aux_pose.position.x - start_pose.position.x;
  y_dist = aux_pose.position.y - start_pose.position.y;
  z_dist = aux_pose.position.z - start_pose.position.z;
  double expected_radius = sqrt( x_dist*x_dist + y_dist*y_dist +  z_dist*z_dist );
  for(const auto & waypoint : trajectory.points)
  {
    waypoint_state.setJointGroupPositions(planning_group_, waypoint.positions);
    waypoint_pose = waypoint_state.getFrameTransform(target_link_);

    // Calculate (and check) distance of current trajectory waypoint from circ center
    x_dist = aux_pose.position.x - waypoint_pose(0,3);
    y_dist = aux_pose.position.y - waypoint_pose(1,3);
    z_dist = aux_pose.position.z - waypoint_pose(2,3);
    double actual_radius = sqrt( x_dist*x_dist + y_dist*y_dist +  z_dist*z_dist );
    EXPECT_NEAR(actual_radius, expected_radius, pose_norm_tolerance_) << "Trajectory way point is not on the circle.";

    // Check orientation
    Eigen::Isometry3d start_pose_iso3d, goal_pose_iso3d;
    tf::poseMsgToEigen(start_pose, start_pose_iso3d);
    tf::poseMsgToEigen(goal_pose, goal_pose_iso3d);
    EXPECT_TRUE(testutils::checkSLERP(start_pose_iso3d,
                                      goal_pose_iso3d,
                                      waypoint_pose,
                                      orientation_norm_tolerance_));
  }

}


int main(int argc, char **argv)
{
  ros::init(argc, argv, "integrationtest_command_planning");
  ros::NodeHandle nh(); // For output via ROS_ERROR etc during test

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

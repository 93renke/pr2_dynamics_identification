/*
 * Dynamics Identification Toolbox
 *
 * Copyright (c) 2010 Stanford University. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>
 */

/**
   \file di_controller_plugin.cpp
   \author Roland Philippsen
*/

#include <dynamics_identification/Start.h>
#include <dynamics_identification/Data.h>
#include <pr2_controller_interface/controller.h>
#include <pluginlib/class_list_macros.h>
#include <XmlRpcValue.h>
#include <XmlRpcException.h>

using namespace std;


class DIController
  : public pr2_controller_interface::Controller
{
public:
  DIController();
  virtual ~DIController();
  
  virtual void update(void);
  virtual bool init(pr2_mechanism_model::RobotState * robot, ros::NodeHandle &nn);
  
  void start_cb(dynamics_identification::Start::ConstPtr const & start);
  
  uint64_t tick_;
  size_t ndof_;
  vector<pr2_mechanism_model::JointState *> controlled_joint_;
  map<string, size_t> joint_map_;
  
  ros::Subscriber start_sub_;
  dynamics_identification::Start start_msg_;
  ssize_t active_joint_index_;
  ros::Time t0_;
  
  ros::Publisher data_pub_;
  dynamics_identification::Data data_msg_;
};


DIController::
DIController()
  : tick_(0),
    active_joint_index_(-1),
    t0_(ros::Time::now())
{
}


DIController::
~DIController()
{
}


void DIController::
start_cb(dynamics_identification::Start::ConstPtr const & start)
{
  active_joint_index_ = -1;
  
  if (0 >= start->chunksize) {
    ROS_ERROR ("DIController::start_cb(): invalid chunksize");
    return;
  }
  
  map<string, size_t>::const_iterator ij(joint_map_.find(start->joint_name));
  if (ij == joint_map_.end()) {
    ROS_ERROR ("DIController::start_cb(): invalid joint name `%s'", start->joint_name.c_str());
    return;
  }
  
  if (data_msg_.tick.size() != start->chunksize) {
    data_msg_.tick.resize(start->chunksize);
    data_msg_.milliseconds.resize(start->chunksize);
    data_msg_.command_torque.resize(start->chunksize);
    data_msg_.position.resize(start->chunksize);
    data_msg_.velocity.resize(start->chunksize);
    data_msg_.applied_torque.resize(start->chunksize);
  }

  start_msg_ = *start;
  active_joint_index_ = ij->second;
  tick_ = 0;
  t0_ = ros::Time::now();
}


void DIController::
update(void)
{
  ros::Duration const dt(ros::Time::now() - t0_);
  
  if (0 > active_joint_index_ ) {
    for (size_t ii(0); ii < ndof_; ++ii) {
      controlled_joint_[ii]->commanded_effort_ = 0;
    }
    return;
  }
  
  size_t const data_index(tick_ % start_msg_.chunksize);
  if ((0 == data_index) && (0 < tick_)) {
    // publish previously populated message
    data_pub_.publish(data_msg_);
  }
  
  data_msg_.tick[data_index] = tick_;
  data_msg_.milliseconds[data_index] = dt.toNSec() * 1e-6;
  if (controlled_joint_[active_joint_index_]->position_ >= start_msg_.upper_position) {
    data_msg_.command_torque[data_index] = - start_msg_.torque_magnitude;
  }
  if (controlled_joint_[active_joint_index_]->position_ <= start_msg_.lower_position) {
    data_msg_.command_torque[data_index] = start_msg_.torque_magnitude;
  }
  data_msg_.position[data_index] = controlled_joint_[active_joint_index_]->position_;
  data_msg_.velocity[data_index] = controlled_joint_[active_joint_index_]->velocity_;
  data_msg_.applied_torque[data_index] = controlled_joint_[active_joint_index_]->measured_effort_;
  
  for (size_t ii(0); ii < ndof_; ++ii) {
    if (static_cast<ssize_t>(ii) == active_joint_index_) {
      controlled_joint_[ii]->commanded_effort_ = data_msg_.command_torque[data_index];
    }
    else {
      controlled_joint_[ii]->commanded_effort_ = 0; // XXXX to do: PID on starting position
    }
  }
  
  ++tick_;
}


bool DIController::
init(pr2_mechanism_model::RobotState * robot, ros::NodeHandle & nn)
{
  try {
    
    string joints_param_name;
    if ( ! nn.getParam("joints_param_name", joints_param_name)) {
      joints_param_name = "joints";
    }
    
    ROS_INFO ("reading joints from parameter `%s'", joints_param_name.c_str());
    XmlRpc::XmlRpcValue joints_value;
    if ( ! nn.getParam(joints_param_name, joints_value)) {
      throw runtime_error("invalid joints_param_name `" + joints_param_name + "'");
    }
    if (0 == joints_value.size()) {
      throw runtime_error("empty joint list");
    }
    
    controlled_joint_.clear();
    joint_map_.clear();
    for (int ii(0); ii < joints_value.size(); ++ii) {
      string const & name(static_cast<string const &>(joints_value[ii]));
      pr2_mechanism_model::JointState * joint(robot->getJointState(name));
      if ( ! joint) {
	throw runtime_error("no joint called `" + name + "'");
      }
      ROS_INFO ("adding joint `%s'", name.c_str());
      joint_map_[name] = controlled_joint_.size();
      controlled_joint_.push_back(joint);
    }
    
  }
  
  catch (exception const & ee) {
    ROS_ERROR ("DIController::init(): EXCEPTION: %s", ee.what());
    // cleanup?
    return false;
  }

  catch (XmlRpc::XmlRpcException const & ee) {
    ROS_ERROR ("DIController::init(): XmlRpcException: %s", ee.getMessage().c_str());
    // cleanup?
    return false;
  }
  
  start_sub_ =
    nn.subscribe<dynamics_identification::Start>("start", 1, &DIController::start_cb, this);
  data_pub_ = nn.advertise<dynamics_identification::Data>("data", 100);
  
  ndof_ = controlled_joint_.size();
  active_joint_index_ = -1;
  ROS_INFO ("ready to rock");
  return true;
}


PLUGINLIB_DECLARE_CLASS (dynamics_identification, DIController, DIController, pr2_controller_interface::Controller)

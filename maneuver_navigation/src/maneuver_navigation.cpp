#include "maneuver_navigation.h"

namespace mn
{
ManeuverNavigation::ManeuverNavigation(tf::TransformListener& tf, ros::NodeHandle& nh, double timeout_duration) :
tf_(tf), nh_(nh), blp_loader_("nav_core", "nav_core::BaseLocalPlanner")
{    
    
    initialized_ = false;
    timeout_duration_ = ros::Duration(timeout_duration);
    timer_running_ = false;
};


ManeuverNavigation::~ManeuverNavigation() 
{
    local_planner_.reset();
};

void ManeuverNavigation::init() 
{    


    
    local_costmap_ros = new costmap_2d::Costmap2DROS("local_costmap", tf_);    
    
//     costmap_ros_ = new costmap_2d::Costmap2DROS("local_costmap", tf_); //global_costmap
    costmap_ros_ = local_costmap_ros;
    costmap_ = costmap_ros_->getCostmap();
    
    world_model_ = new base_local_planner::CostmapModel(*costmap_);
    maneuver_planner = maneuver_planner::ManeuverPlanner("maneuver_planner",costmap_ros_);
//     try{
//         local_planner.initialize("TrajectoryPlannerROS", &tf_, local_costmap_ros);
//     } catch(...) {
//       // 
//         ROS_FATAL("Failed to initialize the global planner");
//         exit(1);
//     }
    
    //create a local planner
//     nh_.param("base_local_planner", local_planner_str, std::string("base_local_planner/TrajectoryPlannerROS"));
    std::string local_planner_str;    
    nh_.param("base_local_planner", local_planner_str, std::string("teb_local_planner/TebLocalPlannerROS"));
    try {      
      local_planner_ = blp_loader_.createInstance(local_planner_str);
      //printf("Created local_planner %s", local_planner_str.c_str());
      local_planner_->initialize(blp_loader_.getName(local_planner_str), &tf_, local_costmap_ros);
    } catch (const pluginlib::PluginlibException& ex) {
      //ROS_FATAL("Failed to create the %s planner, are you sure it is properly registered and that the containing library is built? Exception: %s", local_planner.c_str(), ex.what());
        ROS_FATAL("Failed to create the local planner");
      exit(1);
    }    
    
    nh_.getParam(blp_loader_.getName(local_planner_str)+"/xy_goal_tolerance", xy_goal_tolerance_);
    nh_.getParam(blp_loader_.getName(local_planner_str)+"/yaw_goal_tolerance", yaw_goal_tolerance_);
        
    
    mn_goal_.conf.precise_goal = false;
    mn_goal_.conf.use_line_planner = false;
    
    local_nav_state_ = LOC_NAV_IDLE;
    manv_nav_state_  = MANV_NAV_IDLE;
    
    MAX_AHEAD_DIST_BEFORE_REPLANNING = 1.0;
    goal_free_ =  false;    
    
    vel_pub_ = nh_.advertise<geometry_msgs::Twist>("cmd_vel", 1);
    
    pub_navigation_fb_ =   nh_.advertise<geometry_msgs::PoseStamped> ( "/maneuver_navigation/feedback", 1 );
    
    last_goal_as_start_ = false;
    last_goal_valid_ =  false;
    
    plan.clear();
    
    initialized_ = true;

};

void ManeuverNavigation::clearCostmaps()
{
    costmap_ros_->resetLayers();
}

void ManeuverNavigation::reinitPlanner(const geometry_msgs::Polygon& new_footprint) 
{      
    costmap_ros_->setUnpaddedRobotFootprintPolygon(new_footprint);   
    costmap_ros_->resetLayers();
    // initialize maneuver planner
    maneuver_planner = maneuver_planner::ManeuverPlanner("maneuver_planner",costmap_ros_);
    // Initializelocal planner
    std::string local_planner_str;
//     nh_.param("base_local_planner", local_planner_str, std::string("base_local_planner/TrajectoryPlannerROS"));
    nh_.param("base_local_planner", local_planner_str, std::string("teb_local_planner/TebLocalPlannerROS"));
    local_planner_.reset();    
    try {      
      local_planner_ = blp_loader_.createInstance(local_planner_str);
      local_planner_->initialize(blp_loader_.getName(local_planner_str), &tf_, local_costmap_ros);
    } catch (const pluginlib::PluginlibException& ex) {
      //ROS_FATAL("Failed to create the %s planner, are you sure it is properly registered and that the containing library is built? Exception: %s", local_planner.c_str(), ex.what());
        ROS_FATAL("Failed to create the local planner");
      exit(1);
    } 
    
    nh_.getParam(blp_loader_.getName(local_planner_str)+"/xy_goal_tolerance", xy_goal_tolerance_);
    nh_.getParam(blp_loader_.getName(local_planner_str)+"/yaw_goal_tolerance", yaw_goal_tolerance_);
    
    mn_goal_.conf.precise_goal = false;
    mn_goal_.conf.use_line_planner = false;    

}

  void ManeuverNavigation::publishZeroVelocity(){
    geometry_msgs::Twist cmd_vel;
    cmd_vel.linear.x = 0.0;
    cmd_vel.linear.y = 0.0;
    cmd_vel.angular.z = 0.0;
    vel_pub_.publish(cmd_vel);
  }




bool ManeuverNavigation:: gotoGoal(const geometry_msgs::PoseStamped& goal) 
{   
    simple_goal_ = true;
    if(last_goal_valid_)
    {
        last_goal_ = goal_;        
    }        

    if( last_goal_as_start_ & last_goal_valid_ )    
    {
        append_new_maneuver_ = true;
    }else{
        append_new_maneuver_ = false;
    }
        
    goal_ = goal;
    last_goal_valid_ =  true;
                
    mn_goal_.conf.precise_goal = false;
    mn_goal_.conf.use_line_planner = false;
    manv_nav_state_ = MANV_NAV_MAKE_INIT_PLAN;    
    local_nav_state_ = LOC_NAV_IDLE;
    return true; // TODO: implement

};

bool ManeuverNavigation:: gotoGoal(const maneuver_navigation::Goal& goal) 
{        
    mn_goal_ = goal;
    simple_goal_ = false;    
    append_new_maneuver_ = mn_goal_.conf.append_new_maneuver;
    manv_nav_state_ = MANV_NAV_MAKE_INIT_PLAN;    
    local_nav_state_ = LOC_NAV_IDLE;
    return true; // TODO: implement

};

void ManeuverNavigation:: cancel() 
{    
   publishZeroVelocity();        
   local_nav_state_ = LOC_NAV_IDLE;
   manv_nav_state_   = MANV_NAV_IDLE;
   return;

};

bool ManeuverNavigation::isGoalReachable() 
{
    return true; // TODO: implement

};


//we need to take the footprint of the robot into account when we calculate cost to obstacles
double ManeuverNavigation::footprintCost(double x_i, double y_i, double theta_i)
{
    if(!initialized_)
    {
        ROS_ERROR("The navigator has not been initialized");
        return -1.0;
    }

    std::vector<geometry_msgs::Point> footprint = costmap_ros_->getRobotFootprint();

    //if we have no footprint... do nothing
    if(footprint.size() < 3)
        return -1.0;

    //check if the footprint is legal
    double footprint_cost = world_model_->footprintCost(x_i, y_i, theta_i, footprint);
    
    return footprint_cost;
}


bool ManeuverNavigation::checkFootprintOnGlobalPlan(const std::vector<geometry_msgs::PoseStamped>& plan, const double& max_ahead_dist, double& dist_before_obs, int &index_closest_to_pose, int &index_before_obs )
{
    tf::Stamped<tf::Pose> global_pose;
    if( !getRobotPose(global_pose) )
        return false;    
    // First find the closes point from the robot pose to the path   
    double dist_to_path_min = 1e3;
    double dist_to_path; 
    tf::Pose pose_temp;
    tf::Quaternion quat_temp;
    int index_pose;
    double yaw, pitch, roll;
    double x = global_pose.getOrigin().getX();
    double y = global_pose.getOrigin().getY();
    int i;

    for (i =0; i < plan.size(); i++) 
    {
        dist_to_path = hypot(plan[i].pose.position.x-x,plan[i].pose.position.y-y);
        if(dist_to_path < dist_to_path_min)
        {
            dist_to_path_min = dist_to_path;
            index_pose = i; // TODO: Do this in a smarter way, remebering last index. Index needs to be resseted when there is a replan
        }
        else
        {
            break;
        }
    }    
    // Now start checking poses in the future up to the desired distance
    double total_ahead_distance = 0.0;
    index_closest_to_pose = index_pose;
    index_before_obs = plan.size()-1;
    double dist_next_point;
    double footprint_cost;
    bool is_traj_free = true;
    tf::Stamped<tf::Pose> pose_from_plan;
    
    for (i = index_pose; i < plan.size()-2; i++) 
    {
        dist_next_point = hypot(plan[i].pose.position.x-plan[i+1].pose.position.x,plan[i].pose.position.y-plan[i+1].pose.position.y);
        total_ahead_distance += dist_next_point;
        if( total_ahead_distance < max_ahead_dist)
        {
            if (plan[i+1].header.frame_id.empty()){
                // Non valid plan
                ROS_ERROR("NON VALID PLAN!!!");
                is_traj_free = false;
                
            }
            tf::poseStampedMsgToTF(plan[i+1],pose_from_plan);             
            pose_from_plan.getBasis().getEulerYPR(yaw, pitch, roll);
            footprint_cost = footprintCost(pose_from_plan.getOrigin().getX(), pose_from_plan.getOrigin().getY(), yaw);
            if( footprint_cost < 0 )
            {
                printf("footprint_cost %f",footprint_cost);
                is_traj_free = false;                
                index_before_obs = i;
                break;
            }
        }
        else
            break;
    }    
    
    dist_before_obs = total_ahead_distance;
    return is_traj_free;
    
    
}


void ManeuverNavigation::callLocalNavigationStateMachine() 
{
    geometry_msgs::Twist cmd_vel;
    tf::Stamped<tf::Pose> global_pose;
    geometry_msgs::PoseStamped feedback_pose;    
    
    if( getRobotPose(global_pose) )
    {
        tf::poseStampedTFToMsg(global_pose, feedback_pose); 
        pub_navigation_fb_.publish(feedback_pose);
    }    
    
    switch(local_nav_state_){
        case LOC_NAV_IDLE:
            break;
        case LOC_NAV_SET_PLAN:
            
            if (!local_planner_->setPlan(plan))
            {
                ROS_ERROR("Plan not set");
                local_nav_state_  = LOC_NAV_IDLE;
                manv_nav_state_   = MANV_NAV_IDLE;
            }
            else
                local_nav_state_ = LOC_NAV_BUSY;
            
            break;  
        case LOC_NAV_BUSY:  
            
            if(local_planner_->isGoalReached())
            {
                printf("local planner, partial Goal reached!");
                local_nav_state_ = LOC_NAV_IDLE;
                tf::Stamped<tf::Pose> goal_pose;
                tf::poseStampedMsgToTF(goal_,goal_pose);
                tf::Pose diff_pose;
                diff_pose = goal_pose.inverseTimes(global_pose);
                double dist_to_goal = hypot(diff_pose.getOrigin().getX(), diff_pose.getOrigin().getY());
                double diff_yaw =  tf::getYaw(diff_pose.getRotation()); 
                
                if( mn_goal_.conf.precise_goal && ( std::abs(dist_to_goal) > xy_goal_tolerance_ || std::abs(diff_yaw) > yaw_goal_tolerance_ ) ) 
                    manv_nav_state_  = MANV_NAV_MAKE_INIT_PLAN; // replan maneuver until tolerances are met
                else if (goal_free_ == false)
                    manv_nav_state_  = MANV_NAV_MAKE_INIT_PLAN; // replan maneuver until goal is free
                else
                    manv_nav_state_   = MANV_NAV_DONE;                    
                
                
            }
            else if(local_planner_->computeVelocityCommands(cmd_vel))
            {
                //make sure that we send the velocity command to the base
                double min_velocity = 0.1; // [m/s]; The platform controller cannot hnadle low velocities well. 
                if((cmd_vel.linear.x > 0) && (cmd_vel.linear.x < min_velocity)) { // for the time being only positive x
                  cmd_vel.linear.x = min_velocity;
                }
                vel_pub_.publish(cmd_vel);
                local_plan_infeasible_ = false;
            }
            else 
            {
                ROS_ERROR("local planner, The local planner could not find a valid plan.");
                local_plan_infeasible_ = true;
                
//                 local_nav_state_ = LOC_NAV_SET_PLAN;
                
//                 publishZeroVelocity();        
                local_nav_state_ = LOC_NAV_IDLE;
                manv_nav_state_   = MANV_NAV_MAKE_INIT_PLAN;
            }
            
                  
            
            break;
        default:
            break;
    }

};


bool ManeuverNavigation::getRobotPose(tf::Stamped<tf::Pose> & global_pose) 
{
    bool got_pose = false;
    for (int i = 0; i < 3; i++)
    {
        if(costmap_ros_->getRobotPose(global_pose))
        {
            got_pose = true;
            break;
        }
    }

    if(!got_pose)
    {
        ROS_ERROR("maneuver_navigation cannot make a plan for you because it could not get the start pose of the robot");
        publishZeroVelocity();        
        local_nav_state_ = LOC_NAV_IDLE;
        manv_nav_state_   = MANV_NAV_IDLE;
        return false;
    }   
    return true;
};

maneuver_navigation::Feedback ManeuverNavigation::callManeuverNavigationStateMachine() 
{
    double dist_before_obs;  
    int index_closest_to_pose;
    int index_before_obs;
    std::vector<geometry_msgs::PoseStamped> old_plan;  
    bool is_plan_free;
    maneuver_navigation::Feedback feedback;
    
    feedback.traj_free =  false;
    feedback.dist_to_obs =  false;
    feedback.status = maneuver_navigation::Feedback::BUSY;    
    
    tf::Stamped<tf::Pose> global_pose;
    geometry_msgs::PoseStamped start;    
    
    switch(manv_nav_state_){
        case MANV_NAV_IDLE:
            feedback.status = maneuver_navigation::Feedback::IDLE;
            return feedback;
        case MANV_NAV_MAKE_INIT_PLAN:                            
            if( simple_goal_ )
            {                
                if( !getRobotPose(global_pose) )
                    break;            
                
                tf::poseStampedTFToMsg(global_pose, start);  

                if( last_goal_as_start_ & last_goal_valid_ )   
                {                    
                    start.pose.orientation = last_goal_.pose.orientation;
                }
            }
            else
            {
                goal_ = mn_goal_.goal;
                start = mn_goal_.start;
            }
            if(append_new_maneuver_ && plan.size()>0)
            {
                // Find first current position on plan and then move certain disctance ahead to make the plan.
                is_plan_free = checkFootprintOnGlobalPlan(plan, MAX_AHEAD_DIST_BEFORE_REPLANNING, dist_before_obs, index_closest_to_pose, index_before_obs);
                old_plan.clear();
                old_plan.insert(old_plan.begin(), plan.begin()+index_closest_to_pose, plan.begin()+index_before_obs);
                start.pose.position = plan[index_before_obs].pose.position;
                goal_free_ = maneuver_planner.makePlan(start,goal_, plan, dist_before_obs, mn_goal_.conf.use_line_planner);
                plan.insert(plan.begin(),old_plan.begin(), old_plan.end());
            }
            else
            {
                goal_free_ = maneuver_planner.makePlan(start,goal_, plan, dist_before_obs, mn_goal_.conf.use_line_planner);
            }
            std::cout << "Navigation: dist_before_obs " << dist_before_obs << std::endl; 
            if( dist_before_obs > MAX_AHEAD_DIST_BEFORE_REPLANNING || goal_free_ == true)
            {
                if( plan.size()>0 )
                {
                    resetTimeoutTimer();
                    simple_goal_ = true; // the structured goal is only the first time is received and succesful                
                    local_nav_state_ = LOC_NAV_SET_PLAN;
                    manv_nav_state_  = MANV_NAV_BUSY;
                }
                else
                {
                    ROS_ERROR("Empty plan");
                }
            }
            else
            {
                std::cout <<  "Warning: maneuver_navigation cannot make a plan due to obstacles, inform and keep trying" << std::endl; 
                publishZeroVelocity();  
                if (!timer_running_)
                {
                    startTimeoutTimer();
                    ROS_WARN("Clearing costmaps");
                    clearCostmaps();
                }
                else if (isTimeoutReached())
                {
                    resetTimeoutTimer();
                    ROS_ERROR("Maneuver navigation failed due to obstacles");
                    local_nav_state_ = LOC_NAV_IDLE;
                    manv_nav_state_ = MANV_NAV_IDLE;
                    feedback.status = maneuver_navigation::Feedback::FAILURE_OBSTACLES;
                    return feedback;
                }
              //  local_nav_state_ = LOC_NAV_IDLE;
                // manv_nav_state_   = MANV_NAV_IDLE; 
            }
            
            break;
         case MANV_NAV_BUSY:
             is_plan_free = checkFootprintOnGlobalPlan(plan, MAX_AHEAD_DIST_BEFORE_REPLANNING, dist_before_obs, index_closest_to_pose, index_before_obs);
             if( !is_plan_free)
             {
                std::cout << "Navigation: Obstacle in front at " << dist_before_obs << ". Try to replan" << std::endl; 
                publishZeroVelocity();  // TODO: Do this smarter by decreasing speed while computing new path    
                if( !getRobotPose(global_pose) )
                    break;
                tf::poseStampedTFToMsg(global_pose, start);     
                
                // Find first current position on plan and then move certain disctance ahead to make the plan.
                // is_plan_free = checkFootprintOnGlobalPlan(plan, MAX_AHEAD_DIST_BEFORE_REPLANNING, dist_before_obs, index_closest_to_pose, index_before_obs);
                old_plan.clear();
                old_plan.insert(old_plan.begin(), plan.begin()+index_closest_to_pose, plan.begin()+index_before_obs);
                start.pose.position = plan[index_before_obs].pose.position;
                                         
                goal_free_ = maneuver_planner.makePlan(start,goal_, plan);
                plan.insert(plan.begin(),old_plan.begin(), old_plan.end());
                if(goal_free_)
                {
                    if( plan.size()>0 )
                    {
                        local_nav_state_ = LOC_NAV_SET_PLAN;
                        manv_nav_state_  = MANV_NAV_BUSY;                        
                    }
                    else
                    {
                         std::cout << "Error: Empty plan" << std::endl;
                    }
                }
                else
                {
                    std::cout <<  "No replan possible. Stop, inform and continue trying" << std::endl; 
                    publishZeroVelocity();        
                   // local_nav_state_ = LOC_NAV_IDLE;
                    manv_nav_state_   = MANV_NAV_MAKE_INIT_PLAN;
                }                                 
             }
             else if (goal_free_ == false && dist_before_obs < (MAX_AHEAD_DIST_BEFORE_REPLANNING)) // When the goal was not free and we are close to end of temporary plan, replan
             {
                if( !getRobotPose(global_pose) )
                    break;
                
                tf::poseStampedTFToMsg(global_pose, start);       
                 std::cout <<  "Aproaching to end of temporary plan, distance " << dist_before_obs <<" m. Make new plan" << std::endl; 
                goal_free_ = maneuver_planner.makePlan(start,goal_, plan, dist_before_obs, mn_goal_.conf.use_line_planner);            
                if( goal_free_ || dist_before_obs > MAX_AHEAD_DIST_BEFORE_REPLANNING )
                {
                    if( plan.size()>0 )
                    {
                        local_nav_state_ = LOC_NAV_SET_PLAN;
                        manv_nav_state_  = MANV_NAV_BUSY;
                    }
                    else
                    {
                        std::cout << "Error: Empty plan" << std::endl;
                    }                    
                }              
             }
             
            break;
        case MANV_NAV_DONE:   
            manv_nav_state_ = MANV_NAV_IDLE;
            feedback.status = maneuver_navigation::Feedback::SUCCESS;
            return feedback;
        default:
            break;
    }    
    return feedback;

};

void ManeuverNavigation::startTimeoutTimer()
{
    timeout_timer_ = ros::Time::now();
    timer_running_ = true;
}

bool ManeuverNavigation::isTimeoutReached()
{
    if (!timer_running_ || // timer hasn't been started yet
        ros::Time::now() - timeout_timer_ > timeout_duration_)
    {
        return true;
    }
    return false;
}

void ManeuverNavigation::resetTimeoutTimer()
{
    timer_running_ = false;
}

}

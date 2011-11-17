/*
 *  Copyright (C) 2010 UT-Austin & Austin Robot Technology, Michael Quinlan
 *  Copyright (C) 2011 UT-Austin & Austin Robot Technology
 *  License: Modified BSD Software License 
 */

/**  @file

     Adjacent right observer implementation.

     @author Michael Quinlan, Jack O'Quin, Corbyn Salisbury

 */

#include <art_observers/QuadrilateralOps.h>
#include <art_observers/adjacent_right.h>

namespace observers
{

AdjacentRight::AdjacentRight(art_observers::ObserversConfig &config):
  Observer(config,
	   art_msgs::Observation::Adjacent_right,
	   std::string("Adjacent Right"))
{
  distance_filter_.configure();
  velocity_filter_.configure();
}

AdjacentRight::~AdjacentRight()
{
}

art_msgs::Observation
  AdjacentRight::update(const art_msgs::ArtQuadrilateral &robot_quad,
			 const art_msgs::ArtLanes &local_map,
			 const art_msgs::ArtLanes &obstacles,
			 MapPose pose_) {
  art_msgs::ArtLanes adj_lane_quads = quad_ops::filterAdjacentLanes
					(pose_, local_map, -1);

  art_msgs::ArtLanes adj_lane_obstacles = getObstaclesInLane(obstacles, adj_lane_quads);
  
  //Finding closest poly in left lane
  PolyOps polyOps_left;
  std::vector<poly> adj_polys_left;
  int index_adj = -1;
  polyOps_left.GetPolys(adj_lane_quads, adj_polys_left);
  index_adj = polyOps_left.getClosestPoly(adj_polys_left, robot_quad.midpoint.x, robot_quad.midpoint.y);
 
  float distance = config_.max_range;
  if (adj_lane_obstacles.polygons.size()!=0)
    {
      // Get distance along road from robot to nearest obstacle
      int target_id = adj_lane_obstacles.polygons[0].poly_id;
      distance = 0;
      if(adj_lane_quads.polygons[index_adj].poly_id < adj_lane_obstacles.polygons[0].poly_id) {
        for (size_t i = index_adj; i < adj_lane_quads.polygons.size(); i++)
	  {
	    distance += adj_lane_quads.polygons[i].length;
	    if (adj_lane_quads.polygons[i].poly_id == target_id)
	      break;
	  }
      } else {
	for( size_t i = index_adj; i<adj_lane_quads.polygons.size(); i--) {
	  distance += adj_lane_quads.polygons[i].length;
	  if(adj_lane_quads.polygons[i].poly_id == target_id)
	    break;
	}
      }
    }

  // Filter the distance by averaging over time
  float filt_distance;
  distance_filter_.update(distance, filt_distance);
  
  // Calculate velocity of object (including filter)
  float prev_distance = observation_.distance;
  ros::Time current_update(ros::Time::now());
  double time_change = (current_update - prev_update_).toSec();
  float velocity = (filt_distance - prev_distance) / (time_change);
  float filt_velocity;
  velocity_filter_.update(velocity,filt_velocity);
  prev_update_ = current_update; // Reset prev_update time

  // Time to intersection (-1 if obstacle is moving away)
  double time = -1;
  if (filt_velocity < 0)      // Object getting closer, will intersect
    {
      if (filt_velocity > -0.1)	    // avoid dividing by a tiny number
	{
	  filt_velocity = 0.1;
	}
      time = fabs(filt_distance / filt_velocity);
    }

  // Am I clear, I.e. I won't hit anything
  bool clear=false;
  if (time == -1) // Should this be updated?
    {
      clear = true;
    }

  // Do I really know enough to publish data ?
  bool applicable=false;
  if (velocity_filter_.isFull())
    applicable = true;
    
  // return the observation
  observation_.distance = filt_distance;
  observation_.velocity = filt_velocity;
  observation_.time = time;
  observation_.clear = clear;
  observation_.applicable = applicable;
                   
  return observation_;
}
}


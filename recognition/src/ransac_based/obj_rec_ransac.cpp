/*
 * Software License Agreement (BSD License)
 *
 *  Point Cloud Library (PCL) - www.pointclouds.org
 *  Copyright (c) 2010-2012, Willow Garage, Inc.
 *  
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id$
 *
 */

#include <pcl/common/random.h>
#include <pcl/recognition/ransac_based/obj_rec_ransac.h>

using namespace pcl::common;

pcl::recognition::ObjRecRANSAC::ObjRecRANSAC (float pair_width, float voxel_size)
: pair_width_ (pair_width),
  voxel_size_ (voxel_size),
  position_discretization_ (5.0f*voxel_size_),
  rotation_discretization_ (5.0f*AUX_DEG_TO_RADIANS),
  abs_zdist_thresh_ (1.5f*voxel_size_),
  max_coplanarity_angle_ (3.0f*AUX_DEG_TO_RADIANS),
  model_library_ (pair_width, voxel_size, max_coplanarity_angle_)
{
}

//===============================================================================================================================================

void
pcl::recognition::ObjRecRANSAC::recognize (const PointCloudIn& scene, const PointCloudN& normals, std::list<ObjRecRANSAC::Output>& recognized_objects, double success_probability)
{
  // Clear some stuff
  this->clearTestData ();

  // Build the scene octree
  scene_octree_.build(scene, voxel_size_, &normals);
  // Project it on the xy-plane (which roughly corresponds to the projection plane of the scanning device)
  scene_octree_proj_.build(scene_octree_, abs_zdist_thresh_, abs_zdist_thresh_);

  // Needed only if icp hypotheses refinement is to be performed
  scene_octree_points_ = PointCloudIn::Ptr (new PointCloudIn ());
  // First, get the scene octree points
  scene_octree_.getFullLeavesPoints (*scene_octree_points_);

  if ( do_icp_hypotheses_refinement_ )
  {
    // Build the ICP instance with the scene points as the target
    trimmed_icp_.init (scene_octree_points_);
    trimmed_icp_.setNewToOldEnergyRatio (0.99f);
  }

  if ( success_probability >= 1.0 )
    success_probability = 0.99;

  // Compute the number of iterations
  std::vector<ORROctree::Node*>& full_leaves = scene_octree_.getFullLeaves();
  int num_iterations = this->computeNumberOfIterations(success_probability), num_full_leaves = static_cast<int> (full_leaves.size ());

  // Make sure that there are not more iterations than full leaves
  if ( num_iterations > num_full_leaves )
    num_iterations = num_full_leaves;

#ifdef OBJ_REC_RANSAC_VERBOSE
  printf("ObjRecRANSAC::%s(): recognizing objects [%i iteration(s)]\n", __func__, num_iterations);
#endif

  // First, sample oriented point pairs (opps)
  this->sampleOrientedPointPairs (num_iterations, full_leaves, sampled_oriented_point_pairs_);

  // Leave if we are in the SAMPLE_OPP test mode
  if ( rec_mode_ == ObjRecRANSAC::SAMPLE_OPP )
    return;

  // Generate hypotheses from the sampled opps
  std::list<HypothesisBase> pre_hypotheses;
  int num_hypotheses = this->generateHypotheses (sampled_oriented_point_pairs_, pre_hypotheses);

  // Cluster the hypotheses
  HypothesisOctree grouped_hypotheses;
  this->groupHypotheses (pre_hypotheses, num_hypotheses, transform_space_, grouped_hypotheses);
  pre_hypotheses.clear ();

  // The last graph-based steps in the algorithm
  ORRGraph<Hypothesis> graph_of_close_hypotheses;
  this->buildGraphOfCloseHypotheses (grouped_hypotheses, graph_of_close_hypotheses);
  this->filterGraphOfCloseHypotheses (graph_of_close_hypotheses, accepted_hypotheses_);
  graph_of_close_hypotheses.clear ();

  // Leave if we are in the TEST_HYPOTHESES mode
  if ( rec_mode_ == ObjRecRANSAC::TEST_HYPOTHESES )
    return;

  // Create and initialize a vector of bounded objects needed for the bounding volume hierarchy (BVH)
  std::vector<BVHH::BoundedObject*> bounded_objects (accepted_hypotheses_.size ());
  int i = 0;

  // Initialize the vector with bounded objects
  for ( auto hypo = accepted_hypotheses_.begin () ; hypo != accepted_hypotheses_.end () ; ++hypo, ++i )
  {
    // Create, initialize and save a bounded object based on the hypothesis
    auto *bounded_object = new BVHH::BoundedObject (&(*hypo));
    hypo->computeCenterOfMass (bounded_object->getCentroid ());
    hypo->computeBounds (bounded_object->getBounds ());
    bounded_objects[i] = bounded_object;
  }

  // Build a bounding volume hierarchy (BVH) which is used to accelerate the hypothesis intersection tests
  BVHH bvh;
  bvh.build (bounded_objects);

  ORRGraph<Hypothesis*> conflict_graph;
  this->buildGraphOfConflictingHypotheses (bvh, conflict_graph);
  this->filterGraphOfConflictingHypotheses (conflict_graph, recognized_objects);

  // Cleanup
  this->clearTestData ();

#ifdef OBJ_REC_RANSAC_VERBOSE
    printf("ObjRecRANSAC::%s(): done [%i object(s)]\n", __func__, static_cast<int> (recognized_objects.size ()));
#endif
}

//===============================================================================================================================================

void
pcl::recognition::ObjRecRANSAC::sampleOrientedPointPairs (int num_iterations, const std::vector<ORROctree::Node*>& full_scene_leaves,
    std::list<OrientedPointPair>& output) const
{
#ifdef OBJ_REC_RANSAC_VERBOSE
  printf ("ObjRecRANSAC::%s(): sampling oriented point pairs (opps) ... ", __func__); fflush (stdout);
  int num_of_opps = 0;
#endif

  int num_full_leaves = static_cast<int> (full_scene_leaves.size ());

  if ( !num_full_leaves )
  {
#ifdef OBJ_REC_RANSAC_VERBOSE
    std::cout << "done [" << num_of_opps << " opps].\n";
#endif
    return;
  }

  // The random generator
  UniformGenerator<int> randgen (0, num_full_leaves - 1, static_cast<std::uint32_t> (time (nullptr)));

  // Init the vector with the ids
  std::vector<int> ids (num_full_leaves);
  for ( int i = 0 ; i < num_full_leaves ; ++i )
    ids[i] = i;

  // Sample 'num_iterations' number of oriented point pairs
  for ( int i = 0 ; i < num_iterations ; ++i )
  {
    // Choose a random position within the array of ids
    randgen.setParameters (0, static_cast<int> (ids.size ()) - 1);
    int rand_pos = randgen.run ();

    // Get the leaf at that random position
    ORROctree::Node *leaf1 = full_scene_leaves[ids[rand_pos]];

    // Delete the selected id
    ids.erase (ids.begin() + rand_pos);

    // Get the leaf's point and normal
    const float *p1 = leaf1->getData ()->getPoint ();
    const float *n1 = leaf1->getData ()->getNormal ();

    // Randomly select a leaf at the right distance from 'leaf1'
    ORROctree::Node *leaf2 = scene_octree_.getRandomFullLeafOnSphere (p1, pair_width_);
    if ( !leaf2 )
      continue;

    // Get the leaf's point and normal
    const float *p2 = leaf2->getData ()->getPoint ();
    const float *n2 = leaf2->getData ()->getNormal ();

    if ( ignore_coplanar_opps_ )
    {
      if ( aux::pointsAreCoplanar (p1, n1, p2, n2, max_coplanarity_angle_) )
        continue;
    }

    // Save the sampled point pair
    output.emplace_back(p1, n1, p2, n2);

#ifdef OBJ_REC_RANSAC_VERBOSE
    ++num_of_opps;
#endif
  }

#ifdef OBJ_REC_RANSAC_VERBOSE
  std::cout << "done [" << num_of_opps << " opps].\n";
#endif
}

//===============================================================================================================================================

int
pcl::recognition::ObjRecRANSAC::generateHypotheses (const std::list<OrientedPointPair>& pairs, std::list<HypothesisBase>& out) const
{
#ifdef OBJ_REC_RANSAC_VERBOSE
  printf("ObjRecRANSAC::%s(): generating hypotheses ... ", __func__); fflush (stdout);
#endif

  // Only for 3D hash tables: this is the max number of neighbors a 3D hash table cell can have!
  ModelLibrary::HashTableCell *neigh_cells[27];
  float hash_table_key[3];
  int num_hypotheses = 0;

  for (const auto &pair : pairs)
  {
    // Just to make the code more readable
    const float *scene_p1 = pair.p1_;
    const float *scene_n1 = pair.n1_;
    const float *scene_p2 = pair.p2_;
    const float *scene_n2 = pair.n2_;

    // Use normals and points to compute a hash table key
    compute_oriented_point_pair_signature (scene_p1, scene_n1, scene_p2, scene_n2, hash_table_key);
    // Get the cell and its neighbors based on 'key'
    int num_neigh_cells = model_library_.getHashTable ().getNeighbors (hash_table_key, neigh_cells);

    for ( int i = 0 ; i < num_neigh_cells ; ++i )
    {
      // Check for all models in the current cell
      for (const auto &cell : *neigh_cells[i])
      {
        // For better code readability
        const ModelLibrary::Model *obj_model = cell.first;
        const ModelLibrary::node_data_pair_list& model_pairs = cell.second;

        // Check for all pairs which belong to the current model
        for (const auto &model_pair : model_pairs)
        {
          // Get the points and normals
          const float *model_p1 = model_pair.first->getPoint ();
          const float *model_n1 = model_pair.first->getNormal ();
          const float *model_p2 = model_pair.second->getPoint ();
          const float *model_n2 = model_pair.second->getNormal ();

          HypothesisBase hypothesis(obj_model);
          // Get the rigid transform from model to scene
          this->computeRigidTransform(model_p1, model_n1, model_p2, model_n2, scene_p1, scene_n1, scene_p2, scene_n2, hypothesis.rigid_transform_);
          // Save the current object hypothesis
          out.push_back(hypothesis);
          ++num_hypotheses;
        }
      }
    }
  }
#ifdef OBJ_REC_RANSAC_VERBOSE
  printf("%i hypotheses\n", num_hypotheses);
#endif

  return (num_hypotheses);
}

//===============================================================================================================================================

int
pcl::recognition::ObjRecRANSAC::groupHypotheses(std::list<HypothesisBase>& hypotheses, int num_hypotheses,
    RigidTransformSpace& transform_space, HypothesisOctree& grouped_hypotheses) const
{
#ifdef OBJ_REC_RANSAC_VERBOSE
  printf("ObjRecRANSAC::%s():\n  grouping %i hypotheses ... ", __func__, num_hypotheses); fflush (stdout);
#endif

  // Compute the bounds for the positional discretization
  float b[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  scene_octree_.getBounds (b);
  float enlr = scene_bounds_enlargement_factor_*std::max (std::max (b[1]-b[0], b[3]-b[2]), b[5]-b[4]);
  b[0] -= enlr; b[1] += enlr;
  b[2] -= enlr; b[3] += enlr;
  b[4] -= enlr; b[5] += enlr;

  // Build the output octree which will contain the grouped hypotheses
  HypothesisCreator hypothesis_creator;
  grouped_hypotheses.build (b, position_discretization_, &hypothesis_creator);

  // Build the rigid transform space
  transform_space.build (b, position_discretization_, rotation_discretization_);
  float transformed_point[3];

  // Add all rigid transforms to the discrete rigid transform space
  for (const auto &hypothesis : hypotheses)
  {
    // Transform the center of mass of the model
    aux::transform (hypothesis.rigid_transform_, hypothesis.obj_model_->getOctreeCenterOfMass (), transformed_point);

    // Now add the rigid transform at the right place
    transform_space.addRigidTransform (hypothesis.obj_model_, transformed_point, hypothesis.rigid_transform_);
  }

  std::list<RotationSpace*>& rotation_spaces = transform_space.getRotationSpaces ();
  int num_accepted = 0;

#ifdef OBJ_REC_RANSAC_VERBOSE
  printf("ObjRecRANSAC::%s(): done\n  testing the cluster representatives ...\n", __func__); fflush (stdout);
  // These are some variables needed when printing the recognition progress
  float progress_factor = 100.0f/static_cast<float> (transform_space.getNumberOfOccupiedRotationSpaces ());
  int num_done = 0;
#endif

  // Now take the best hypothesis from each rotation space
  for (const auto &rotation_space : rotation_spaces)
  {
    const std::map<std::string, ModelLibrary::Model*>& models = model_library_.getModels ();
    Hypothesis best_hypothesis;
    best_hypothesis.match_confidence_ = 0.0f;

    // For each model in the library
    for (const auto &model : models)
    {
      // Build a hypothesis based on the entry with most votes
      Hypothesis hypothesis (model.second);

      if ( !rotation_space->getTransformWithMostVotes (model.second, hypothesis.rigid_transform_) )
        continue;

      int int_match;
      int penalty;
      this->testHypothesis (&hypothesis, int_match, penalty);

      // For better code readability
      float num_full_leaves = static_cast<float> (hypothesis.obj_model_->getOctree ().getFullLeaves ().size ());
      float match_thresh = num_full_leaves*visibility_;
      int penalty_thresh = static_cast<int> (num_full_leaves*relative_num_of_illegal_pts_ + 0.5f);

      // Check if this hypothesis is OK
      if ( int_match >= match_thresh && penalty <= penalty_thresh )
      {
        if ( do_icp_hypotheses_refinement_ && int_match > 3 )
        {
          // Convert from array to 4x4 matrix
          Eigen::Matrix<float, 4, 4> mat;
          aux::array12ToMatrix4x4 (hypothesis.rigid_transform_, mat);
          // Perform registration
          trimmed_icp_.align (
              hypothesis.obj_model_->getPointsForRegistration (),
              static_cast<int> (static_cast<float> (int_match)*frac_of_points_for_icp_refinement_),
              mat);
          aux::matrix4x4ToArray12 (mat, hypothesis.rigid_transform_);

          this->testHypothesis (&hypothesis, int_match, penalty);
        }

        if ( hypothesis.match_confidence_ > best_hypothesis.match_confidence_ )
          best_hypothesis = hypothesis;
      }
    }

    if ( best_hypothesis.match_confidence_ > 0.0f )
    {
      const float *c = rotation_space->getCenter ();
      HypothesisOctree::Node* node = grouped_hypotheses.createLeaf (c[0], c[1], c[2]);

      node->setData (best_hypothesis);
      ++num_accepted;
    }

#ifdef OBJ_REC_RANSAC_VERBOSE
    // Update the progress
    printf ("\r  %.1f%% ", (static_cast<float> (++num_done))*progress_factor); fflush (stdout);
#endif
  }

#ifdef OBJ_REC_RANSAC_VERBOSE
  printf("done\n  %i accepted.\n", num_accepted);
#endif

  return (num_accepted);
}

//===============================================================================================================================================

void
pcl::recognition::ObjRecRANSAC::buildGraphOfCloseHypotheses (HypothesisOctree& hypotheses, ORRGraph<Hypothesis>& graph) const
{
#ifdef OBJ_REC_RANSAC_VERBOSE
  printf ("ObjRecRANSAC::%s(): building the graph ... ", __func__); fflush (stdout);
#endif

  std::vector<HypothesisOctree::Node*> hypo_leaves = hypotheses.getFullLeaves ();
  int i = 0;

  graph.resize (static_cast<int> (hypo_leaves.size ()));

  for ( auto hypo = hypo_leaves.begin () ; hypo != hypo_leaves.end () ; ++hypo, ++i )
    (*hypo)->getData ().setLinearId (i);

  i = 0;

  // Now create the graph connectivity such that each two neighboring rotation spaces are neighbors in the graph
  for ( auto hypo = hypo_leaves.cbegin () ; hypo != hypo_leaves.cend () ; ++hypo, ++i )
  {
    // Compute the fitness of the graph node
    graph.getNodes ()[i]->setFitness (static_cast<int> ((*hypo)->getData ().explained_pixels_.size ()));
    graph.getNodes ()[i]->setData ((*hypo)->getData ());

    // Get the neighbors of the current rotation space
    const std::set<HypothesisOctree::Node*>& neighbors = (*hypo)->getNeighbors ();

    for (const auto &neighbor : neighbors)
      graph.insertDirectedEdge ((*hypo)->getData ().getLinearId (), neighbor->getData ().getLinearId ());
  }

#ifdef OBJ_REC_RANSAC_VERBOSE
  printf ("done\n");
#endif
}

//===============================================================================================================================================

void
pcl::recognition::ObjRecRANSAC::filterGraphOfCloseHypotheses (ORRGraph<Hypothesis>& graph, std::vector<Hypothesis>& out) const
{
#ifdef OBJ_REC_RANSAC_VERBOSE
  printf ("ObjRecRANSAC::%s(): building the graph ... ", __func__); fflush (stdout);
#endif

  std::list<ORRGraph<Hypothesis>::Node*> on_nodes, off_nodes;
  graph.computeMaximalOnOffPartition (on_nodes, off_nodes);

  // Copy the data from the on_nodes to the list 'out'
  for (const auto &on_node : on_nodes)
    out.push_back (on_node->getData ());

#ifdef OBJ_REC_RANSAC_VERBOSE
  printf ("done [%i remaining hypotheses]\n", static_cast<int> (out.size ()));
#endif
}

//===============================================================================================================================================

void
pcl::recognition::ObjRecRANSAC::buildGraphOfConflictingHypotheses (const BVHH& bvh, ORRGraph<Hypothesis*>& graph) const
{
#ifdef OBJ_REC_RANSAC_VERBOSE
  printf ("ObjRecRANSAC::%s(): building the conflict graph ... ", __func__); fflush (stdout);
#endif

  const std::vector<BVHH::BoundedObject*>* bounded_objects = bvh.getInputObjects ();

  if ( !bounded_objects )
  {
#ifdef OBJ_REC_RANSAC_VERBOSE
    printf("done\n");
#endif
    return;
  }

  int lin_id = 0;

  // There are as many graph nodes as bounded objects
  graph.resize (static_cast<int> (bounded_objects->size ()));

  // Setup the hypotheses' ids
  for ( auto obj = bounded_objects->begin () ; obj != bounded_objects->end () ; ++obj, ++lin_id )
  {
    (*obj)->getData ()->setLinearId (lin_id);
    graph.getNodes ()[lin_id]->setData ((*obj)->getData ());
  }

  using ordered_int_pair = std::pair<int,int>;
  // This is one is to make sure that we do not compute the same set intersection twice
  std::set<ordered_int_pair, bool(*)(const ordered_int_pair&, const ordered_int_pair&)> ordered_hypotheses_ids (aux::compareOrderedPairs<int>);

  // Project the hypotheses onto the "range image" and store in each pixel the corresponding hypothesis id
  for (const auto &bounded_object : *bounded_objects)
  {
    // For better code readability
    Hypothesis *hypo1 = bounded_object->getData ();

    // Get the bounds of the current hypothesis
    float bounds[6];
    hypo1->computeBounds (bounds);

    // Check if these bounds intersect other hypotheses' bounds
    std::list<BVHH::BoundedObject*> intersected_objects;
    bvh.intersect (bounds, intersected_objects);

    for (const auto &intersected_object : intersected_objects)
    {
      // For better code readability
      Hypothesis *hypo2 = intersected_object->getData ();

      // Build an ordered int pair out of the hypotheses ids
      std::pair<int,int> id_pair;
      if ( hypo1->getLinearId () < hypo2->getLinearId () )
      {
        id_pair.first  = hypo1->getLinearId ();
        id_pair.second = hypo2->getLinearId ();
      }
      else
      {
        id_pair.first  = hypo2->getLinearId ();
        id_pair.second = hypo1->getLinearId ();
      }

      // Make sure that we do not compute the same set intersection twice
      std::pair<std::set<ordered_int_pair, bool(*)(const ordered_int_pair&, const ordered_int_pair&)>::iterator, bool> res = ordered_hypotheses_ids.insert (id_pair);

      if ( !res.second )
        continue; // We've already computed that std::set intersection -> check the next pair

      // Do the more involved intersection test based on a set intersection of the range image pixels which explained by the hypotheses
      std::set<int> id_intersection;

      // Compute the ids intersection of 'obj' and 'it'
      std::set_intersection (hypo1->explained_pixels_.begin (),
                             hypo1->explained_pixels_.end (),
                             hypo2->explained_pixels_.begin (),
                             hypo2->explained_pixels_.end (),
                             std::inserter (id_intersection, id_intersection.begin ()));

      // Compute the intersection fractions
      float frac_1 = static_cast<float> (id_intersection.size ())/static_cast <float> (hypo1->explained_pixels_.size ());
      float frac_2 = static_cast<float> (id_intersection.size ())/static_cast <float> (hypo2->explained_pixels_.size ());

      // Check if the intersection set is large enough, i.e., if there is a conflict
      if ( frac_1 > intersection_fraction_ || frac_2 > intersection_fraction_ )
        graph.insertUndirectedEdge (hypo1->getLinearId (), hypo2->getLinearId ());
    }
  }

#ifdef OBJ_REC_RANSAC_VERBOSE
	printf("done\n");
#endif
}

//===============================================================================================================================================

void
pcl::recognition::ObjRecRANSAC::filterGraphOfConflictingHypotheses (ORRGraph<Hypothesis*>& graph, std::list<ObjRecRANSAC::Output>& recognized_objects) const
{
#ifdef OBJ_REC_RANSAC_VERBOSE
  printf ("ObjRecRANSAC::%s(): filtering the conflict graph ... ", __func__); fflush (stdout);
#endif

  std::vector<ORRGraph<Hypothesis*>::Node*> &nodes = graph.getNodes ();

  // Compute the penalty for each graph node
  for (auto &node : nodes)
  {
    std::size_t num_of_explained = 0;

    // Accumulate the number of pixels the neighbors are explaining
    for (const auto &neigh : node->getNeighbors ())
      num_of_explained += neigh->getData ()->explained_pixels_.size ();

    // Now compute the fitness for the node
    node->setFitness (static_cast<int> (node->getData ()->explained_pixels_.size ()) - static_cast<int> (num_of_explained));
  }

  // Leave the fitest leaves on, such that there are no neighboring ON nodes
  std::list<ORRGraph<Hypothesis*>::Node*> on_nodes, off_nodes;
  graph.computeMaximalOnOffPartition (on_nodes, off_nodes);

  // The ON nodes correspond to accepted solutions
  for (const auto &on_node : on_nodes)
  {
    recognized_objects.emplace_back(on_node->getData ()->obj_model_->getObjectName (),
                                    on_node->getData ()->rigid_transform_,
                                    on_node->getData ()->match_confidence_,
                                    on_node->getData ()->obj_model_->getUserData ()
    );
  }

#ifdef OBJ_REC_RANSAC_VERBOSE
  printf ("done\n");
#endif
}

//===============================================================================================================================================

inline void
pcl::recognition::ObjRecRANSAC::testHypothesis (Hypothesis* hypothesis, int& match, int& penalty) const
{
  match = 0;
  penalty = 0;

  // For better code readability
  const std::vector<ORROctree::Node*>& full_model_leaves = hypothesis->obj_model_->getOctree ().getFullLeaves ();
  const float* rigid_transform = hypothesis->rigid_transform_;
  float transformed_point[3];

  // The match/penalty loop
  for (const auto &full_model_leaf : full_model_leaves)
  {
    // Transform the model point with the current rigid transform
    aux::transform (rigid_transform, full_model_leaf->getData ()->getPoint (), transformed_point);

    // Get the pixel 'transformed_point' lies in
    const ORROctreeZProjection::Pixel* pixel = scene_octree_proj_.getPixel (transformed_point);
    // Check if we have a valid pixel
    if ( pixel == nullptr )
      continue;

    if ( transformed_point[2] < pixel->z1 () ) // The transformed model point overshadows a pixel -> penalize the hypothesis
      ++penalty;
    else if ( transformed_point[2] <= pixel->z2 () ) // The point is OK
    {
      ++match;
      // 'hypo_it' explains 'pixel' => insert the pixel's id in the id set of 'hypo_it'
      hypothesis->explained_pixels_.insert (pixel->getId ());
    }
  }

  hypothesis->match_confidence_ = static_cast<float> (match)/static_cast<float> (hypothesis->obj_model_->getOctree ().getFullLeaves ().size ());
}

//===============================================================================================================================================

inline void
pcl::recognition::ObjRecRANSAC::testHypothesisNormalBased (Hypothesis* hypothesis, float& match) const
{
  match = 0.0f;

  // For better code readability
  const std::vector<ORROctree::Node*>& full_model_leaves = hypothesis->obj_model_->getOctree ().getFullLeaves ();
  const float* rigid_transform = hypothesis->rigid_transform_;
  float transformed_point[3];

  // The match/penalty loop
  for (const auto &full_model_leaf : full_model_leaves)
  {
    // Transform the model point with the current rigid transform
    aux::transform (rigid_transform, full_model_leaf->getData ()->getPoint (), transformed_point);

    // Get the pixel 'transformed_point' lies in
    const ORROctreeZProjection::Pixel* pixel = scene_octree_proj_.getPixel (transformed_point);
    // Check if we have a valid pixel
    if ( pixel == nullptr )
      continue;

    // Check if the point is OK
    if ( pixel->z1 () <= transformed_point[2] && transformed_point[2] <= pixel->z2 () )
    {
      // 'hypo_it' explains 'pixel' => insert the pixel's id in the id set of 'hypo_it'
      hypothesis->explained_pixels_.insert (pixel->getId ());

      // Compute the match based on the normal agreement
      const std::set<ORROctree::Node*, bool(*)(ORROctree::Node*,ORROctree::Node*)>* nodes = scene_octree_proj_.getOctreeNodes (transformed_point);

      auto n = nodes->begin ();
      ORROctree::Node *closest_node = *n;
      float min_sqr_dist = aux::sqrDistance3 (closest_node->getData ()->getPoint (), transformed_point);

      for ( ++n ; n != nodes->end () ; ++n )
      {
        float sqr_dist = aux::sqrDistance3 ((*n)->getData ()->getPoint (), transformed_point);
        if ( sqr_dist < min_sqr_dist )
        {
          closest_node = *n;
          min_sqr_dist = sqr_dist;
        }
      }

      float rotated_normal[3];
      aux::mult3x3 (rigid_transform, closest_node->getData ()->getNormal (), rotated_normal);

      match += aux::dot3 (rotated_normal, full_model_leaf->getData ()->getNormal ());
    }
  }

  hypothesis->match_confidence_ = match/static_cast<float> (hypothesis->obj_model_->getOctree ().getFullLeaves ().size ());
}

//===============================================================================================================================================

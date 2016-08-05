/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2013-2014, Willow Garage, Inc.
 *  Copyright (c) 2014-2016, Open Source Robotics Foundation
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
 *   * Neither the name of Open Source Robotics Foundation nor the names of its
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
 */

/** \author Jia Pan */

#ifndef FCL_CONTINUOUS_COLLISION_H
#define FCL_CONTINUOUS_COLLISION_H

#include <iostream>
#include "fcl/collision_object.h"
#include "fcl/continuous_collision_object.h"
#include "fcl/collision_data.h"
#include "fcl/ccd/conservative_advancement.h"
#include "fcl/ccd/motion.h"
#include "fcl/BVH/BVH_model.h"

namespace fcl
{

/// @brief continous collision checking between two objects
template <typename Scalar>
Scalar continuousCollide(
    const CollisionGeometry<Scalar>* o1,
    const Transform3<Scalar>& tf1_beg,
    const Transform3<Scalar>& tf1_end,
    const CollisionGeometry<Scalar>* o2,
    const Transform3<Scalar>& tf2_beg,
    const Transform3<Scalar>& tf2_end,
    const ContinuousCollisionRequest<Scalar>& request,
    ContinuousCollisionResult<Scalar>& result);

template <typename Scalar>
Scalar continuousCollide(
    const CollisionObject<Scalar>* o1,
    const Transform3<Scalar>& tf1_end,
    const CollisionObject<Scalar>* o2,
    const Transform3<Scalar>& tf2_end,
    const ContinuousCollisionRequest<Scalar>& request,
    ContinuousCollisionResult<Scalar>& result);

template <typename Scalar>
Scalar collide(
    const ContinuousCollisionObject<Scalar>* o1,
    const ContinuousCollisionObject<Scalar>* o2,
    const ContinuousCollisionRequest<Scalar>& request,
    ContinuousCollisionResult<Scalar>& result);

//============================================================================//
//                                                                            //
//                              Implementations                               //
//                                                                            //
//============================================================================//

//==============================================================================
template<typename GJKSolver>
ConservativeAdvancementFunctionMatrix<GJKSolver>& getConservativeAdvancementFunctionLookTable()
{
  static ConservativeAdvancementFunctionMatrix<GJKSolver> table;
  return table;
}

template <typename Scalar>
MotionBasePtr getMotionBase(
    const Transform3<Scalar>& tf_beg,
    const Transform3<Scalar>& tf_end,
    CCDMotionType motion_type)
{
  switch(motion_type)
  {
  case CCDM_TRANS:
    return MotionBasePtr(new TranslationMotion(tf_beg, tf_end));
    break;
  case CCDM_LINEAR:
    return MotionBasePtr(new InterpMotion(tf_beg, tf_end));
    break;
  case CCDM_SCREW:
    return MotionBasePtr(new ScrewMotion(tf_beg, tf_end));
    break;
  case CCDM_SPLINE:
    return MotionBasePtr(new SplineMotion(tf_beg, tf_end));
    break;
  default:
    return MotionBasePtr();
  }
}

template <typename Scalar>
Scalar continuousCollideNaive(
    const CollisionGeometry<Scalar>* o1,
    const MotionBase* motion1,
    const CollisionGeometry<Scalar>* o2,
    const MotionBase* motion2,
    const ContinuousCollisionRequest<Scalar>& request,
    ContinuousCollisionResult<Scalar>& result)
{
  std::size_t n_iter = std::min(request.num_max_iterations, (std::size_t)ceil(1 / request.toc_err));
  Transform3<Scalar> cur_tf1, cur_tf2;
  for(std::size_t i = 0; i < n_iter; ++i)
  {
    Scalar t = i / (Scalar) (n_iter - 1);
    motion1->integrate(t);
    motion2->integrate(t);

    motion1->getCurrentTransform(cur_tf1);
    motion2->getCurrentTransform(cur_tf2);

    CollisionRequest<Scalar> c_request;
    CollisionResult<Scalar> c_result;

    if(collide(o1, cur_tf1, o2, cur_tf2, c_request, c_result))
    {
      result.is_collide = true;
      result.time_of_contact = t;
      result.contact_tf1 = cur_tf1;
      result.contact_tf2 = cur_tf2;
      return t;
    }
  }

  result.is_collide = false;
  result.time_of_contact = Scalar(1);
  return result.time_of_contact;
}

namespace details
{
template<typename BV>
typename BV::Scalar continuousCollideBVHPolynomial(
    const CollisionGeometry<typename BV::Scalar>* o1_,
    const TranslationMotion* motion1,
    const CollisionGeometry<typename BV::Scalar>* o2_,
    const TranslationMotion* motion2,
    const ContinuousCollisionRequest<typename BV::Scalar>& request,
    ContinuousCollisionResult<typename BV::Scalar>& result)
{
  using Scalar = typename BV::Scalar;

  const BVHModel<BV>* o1__ = static_cast<const BVHModel<BV>*>(o1_);
  const BVHModel<BV>* o2__ = static_cast<const BVHModel<BV>*>(o2_);

  // ugly, but lets do it now.
  BVHModel<BV>* o1 = const_cast<BVHModel<BV>*>(o1__);
  BVHModel<BV>* o2 = const_cast<BVHModel<BV>*>(o2__);
  std::vector<Vector3d> new_v1(o1->num_vertices);
  std::vector<Vector3d> new_v2(o2->num_vertices);

  for(std::size_t i = 0; i < new_v1.size(); ++i)
    new_v1[i] = o1->vertices[i] + motion1->getVelocity();
  for(std::size_t i = 0; i < new_v2.size(); ++i)
    new_v2[i] = o2->vertices[i] + motion2->getVelocity();

  o1->beginUpdateModel();
  o1->updateSubModel(new_v1);
  o1->endUpdateModel(true, true);

  o2->beginUpdateModel();
  o2->updateSubModel(new_v2);
  o2->endUpdateModel(true, true);

  MeshContinuousCollisionTraversalNode<BV> node;
  CollisionRequest<Scalar> c_request;

  motion1->integrate(0);
  motion2->integrate(0);
  Transform3<Scalar> tf1, tf2;
  motion1->getCurrentTransform(tf1);
  motion2->getCurrentTransform(tf2);
  if(!initialize<BV>(node, *o1, tf1, *o2, tf2, c_request))
    return -1.0;

  collide(&node);

  result.is_collide = (node.pairs.size() > 0);
  result.time_of_contact = node.time_of_contact;

  if(result.is_collide)
  {
    motion1->integrate(node.time_of_contact);
    motion2->integrate(node.time_of_contact);
    motion1->getCurrentTransform(tf1);
    motion2->getCurrentTransform(tf2);
    result.contact_tf1 = tf1;
    result.contact_tf2 = tf2;
  }

  return result.time_of_contact;
}

} // namespace details

template <typename Scalar>
Scalar continuousCollideBVHPolynomial(
    const CollisionGeometry<Scalar>* o1,
    const TranslationMotion* motion1,
    const CollisionGeometry<Scalar>* o2,
    const TranslationMotion* motion2,
    const ContinuousCollisionRequest<Scalar>& request,
    ContinuousCollisionResult<Scalar>& result)
{
  switch(o1->getNodeType())
  {
  case BV_AABB:
    if(o2->getNodeType() == BV_AABB)
      return details::continuousCollideBVHPolynomial<AABBd>(o1, motion1, o2, motion2, request, result);
    break;
  case BV_OBB:
    if(o2->getNodeType() == BV_OBB)
      return details::continuousCollideBVHPolynomial<OBBd>(o1, motion1, o2, motion2, request, result);
    break;
  case BV_RSS:
    if(o2->getNodeType() == BV_RSS)
      return details::continuousCollideBVHPolynomial<RSSd>(o1, motion1, o2, motion2, request, result);
    break;
  case BV_kIOS:
    if(o2->getNodeType() == BV_kIOS)
      return details::continuousCollideBVHPolynomial<kIOSd>(o1, motion1, o2, motion2, request, result);
    break;
  case BV_OBBRSS:
    if(o2->getNodeType() == BV_OBBRSS)
      return details::continuousCollideBVHPolynomial<OBBRSSd>(o1, motion1, o2, motion2, request, result);
    break;
  case BV_KDOP16:
    if(o2->getNodeType() == BV_KDOP16)
      return details::continuousCollideBVHPolynomial<KDOPd<16> >(o1, motion1, o2, motion2, request, result);
    break;
  case BV_KDOP18:
    if(o2->getNodeType() == BV_KDOP18)
      return details::continuousCollideBVHPolynomial<KDOPd<18> >(o1, motion1, o2, motion2, request, result);
    break;
  case BV_KDOP24:
    if(o2->getNodeType() == BV_KDOP24)
      return details::continuousCollideBVHPolynomial<KDOPd<24> >(o1, motion1, o2, motion2, request, result);
    break;
  default:
    ;
  }

  std::cerr << "Warning: BV type not supported by polynomial solver CCD" << std::endl;

  return -1;
}

namespace details
{

template<typename NarrowPhaseSolver>
typename NarrowPhaseSolver::Scalar continuousCollideConservativeAdvancement(
    const CollisionGeometry<typename NarrowPhaseSolver::Scalar>* o1,
    const MotionBase* motion1,
    const CollisionGeometry<typename NarrowPhaseSolver::Scalar>* o2,
    const MotionBase* motion2,
    const NarrowPhaseSolver* nsolver_,
    const ContinuousCollisionRequest<typename NarrowPhaseSolver::Scalar>& request,
    ContinuousCollisionResult<typename NarrowPhaseSolver::Scalar>& result)
{
  using Scalar = typename NarrowPhaseSolver::Scalar;

  const NarrowPhaseSolver* nsolver = nsolver_;
  if(!nsolver_)
    nsolver = new NarrowPhaseSolver();

  const auto& looktable = getConservativeAdvancementFunctionLookTable<NarrowPhaseSolver>();

  NODE_TYPE node_type1 = o1->getNodeType();
  NODE_TYPE node_type2 = o2->getNodeType();

  Scalar res = -1;

  if(!looktable.conservative_advancement_matrix[node_type1][node_type2])
  {
    std::cerr << "Warning: collision function between node type " << node_type1 << " and node type " << node_type2 << " is not supported"<< std::endl;
  }
  else
  {
    res = looktable.conservative_advancement_matrix[node_type1][node_type2](o1, motion1, o2, motion2, nsolver, request, result);
  }

  if(!nsolver_)
    delete nsolver;

  if(result.is_collide)
  {
    motion1->integrate(result.time_of_contact);
    motion2->integrate(result.time_of_contact);

    Transform3<Scalar> tf1, tf2;
    motion1->getCurrentTransform(tf1);
    motion2->getCurrentTransform(tf2);
    result.contact_tf1 = tf1;
    result.contact_tf2 = tf2;
  }

  return res;
}

} // namespace details

template <typename Scalar>
Scalar continuousCollideConservativeAdvancement(
    const CollisionGeometry<Scalar>* o1,
    const MotionBase* motion1,
    const CollisionGeometry<Scalar>* o2,
    const MotionBase* motion2,
    const ContinuousCollisionRequest<Scalar>& request,
    ContinuousCollisionResult<Scalar>& result)
{
  switch(request.gjk_solver_type)
  {
  case GST_LIBCCD:
    {
      GJKSolver_libccd<Scalar> solver;
      return details::continuousCollideConservativeAdvancement(o1, motion1, o2, motion2, &solver, request, result);
    }
  case GST_INDEP:
    {
      GJKSolver_indep<Scalar> solver;
      return details::continuousCollideConservativeAdvancement(o1, motion1, o2, motion2, &solver, request, result);
    }
  default:
    return -1;
  }
}

template <typename Scalar>
Scalar continuousCollide(
    const CollisionGeometry<Scalar>* o1,
    const MotionBase* motion1,
    const CollisionGeometry<Scalar>* o2,
    const MotionBase* motion2,
    const ContinuousCollisionRequest<Scalar>& request,
    ContinuousCollisionResult<Scalar>& result)
{
  switch(request.ccd_solver_type)
  {
  case CCDC_NAIVE:
    return continuousCollideNaive(o1, motion1,
                                  o2, motion2,
                                  request,
                                  result);
    break;
  case CCDC_CONSERVATIVE_ADVANCEMENT:
    return continuousCollideConservativeAdvancement(o1, motion1,
                                                    o2, motion2,
                                                    request,
                                                    result);
    break;
  case CCDC_RAY_SHOOTING:
    if(o1->getObjectType() == OT_GEOM && o2->getObjectType() == OT_GEOM && request.ccd_motion_type == CCDM_TRANS)
    {

    }
    else
      std::cerr << "Warning! Invalid continuous collision setting" << std::endl;
    break;
  case CCDC_POLYNOMIAL_SOLVER:
    if(o1->getObjectType() == OT_BVH && o2->getObjectType() == OT_BVH && request.ccd_motion_type == CCDM_TRANS)
    {
      return continuousCollideBVHPolynomial(o1, (const TranslationMotion*)motion1,
                                            o2, (const TranslationMotion*)motion2,
                                            request, result);
    }
    else
      std::cerr << "Warning! Invalid continuous collision checking" << std::endl;
    break;
  default:
    std::cerr << "Warning! Invalid continuous collision setting" << std::endl;
  }

  return -1;
}

template <typename Scalar>
Scalar continuousCollide(
    const CollisionGeometry<Scalar>* o1,
    const Transform3<Scalar>& tf1_beg,
    const Transform3<Scalar>& tf1_end,
    const CollisionGeometry<Scalar>* o2,
    const Transform3<Scalar>& tf2_beg,
    const Transform3<Scalar>& tf2_end,
    const ContinuousCollisionRequest<Scalar>& request,
    ContinuousCollisionResult<Scalar>& result)
{
  MotionBasePtr motion1 = getMotionBase(tf1_beg, tf1_end, request.ccd_motion_type);
  MotionBasePtr motion2 = getMotionBase(tf2_beg, tf2_end, request.ccd_motion_type);

  return continuousCollide(o1, motion1.get(), o2, motion2.get(), request, result);
}

template <typename Scalar>
Scalar continuousCollide(
    const CollisionObject<Scalar>* o1,
    const Transform3<Scalar>& tf1_end,
    const CollisionObject<Scalar>* o2,
    const Transform3<Scalar>& tf2_end,
    const ContinuousCollisionRequest<Scalar>& request,
    ContinuousCollisionResult<Scalar>& result)
{
  return continuousCollide(o1->collisionGeometry().get(), o1->getTransform(), tf1_end,
                           o2->collisionGeometry().get(), o2->getTransform(), tf2_end,
                           request, result);
}

template <typename Scalar>
Scalar collide(
    const ContinuousCollisionObject<Scalar>* o1,
    const ContinuousCollisionObject<Scalar>* o2,
    const ContinuousCollisionRequest<Scalar>& request,
    ContinuousCollisionResult<Scalar>& result)
{
  return continuousCollide(o1->collisionGeometry().get(), o1->getMotion(),
                           o2->collisionGeometry().get(), o2->getMotion(),
                           request, result);
}

} // namespace fcl

#endif

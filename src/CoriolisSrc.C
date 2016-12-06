/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/

#include <CoriolisSrc.h>
#include <Realm.h>
#include <SolutionOptions.h>

// stk_mesh/base/fem
#include <stk_mesh/base/MetaData.hpp>

namespace sierra{
namespace nalu{

//==========================================================================
// Class Definition
//==========================================================================
// CoriolisSrc
//==========================================================================
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
CoriolisSrc::CoriolisSrc(Realm &realm) {

  pi_ = std::acos(-1.0);

  stk::mesh::MetaData & meta_data = realm.meta_data();

  // extract user parameters from solution options
  earthAngularVelocity_ = realm.solutionOptions_->earthAngularVelocity_;
  latitude_ = realm.solutionOptions_->latitude_ * pi_ / 180.0;
  nDim_ = meta_data.spatial_dimension();
  if (nDim_ != 3) 
    throw std::runtime_error("CoriolisSrc: nDim_ != 3");
  eastVector_.resize(nDim_);
  northVector_.resize(nDim_);
  upVector_.resize(nDim_);
  eastVector_ = realm.solutionOptions_->eastVector_;
  northVector_ = realm.solutionOptions_->northVector_;

  // normalize the east and north vectors
  double magE = std::sqrt(eastVector_[0]*eastVector_[0]+eastVector_[1]*eastVector_[1]+eastVector_[2]*eastVector_[2]);
  double magN = std::sqrt(northVector_[0]*northVector_[0]+northVector_[1]*northVector_[1]+northVector_[2]*northVector_[2]);
  for (int i=0; i<nDim_; ++i) {
    eastVector_[i] /= magE;
    northVector_[i] /= magN;
  }

  // calculate the 'up' unit vector
  cross_product(eastVector_, northVector_, upVector_);

  // some factors that do not change
  sinphi_ = std::sin(latitude_);
  cosphi_ = std::cos(latitude_);
  corfac_ = 2.0*earthAngularVelocity_;

  // Jacobian entries
  Jxy_ = corfac_ * (eastVector_[0]*northVector_[1]-northVector_[0]*eastVector_[1])*sinphi_
                 + (upVector_[0]*eastVector_[1]-eastVector_[0]*upVector_[1])*cosphi_;
  Jxz_ = corfac_ * (eastVector_[0]*northVector_[2]-northVector_[0]*eastVector_[2])*sinphi_
                 + (upVector_[0]*eastVector_[2]-eastVector_[0]*upVector_[2])*cosphi_;
  Jyz_ = corfac_ * (eastVector_[1]*northVector_[2]-northVector_[1]*eastVector_[2])*sinphi_
                 + (upVector_[1]*eastVector_[2]-eastVector_[1]*upVector_[2])*cosphi_;
}

//--------------------------------------------------------------------------
//-------- cross_product ----------------------------------------------------
//--------------------------------------------------------------------------
void
CoriolisSrc::cross_product(
  std::vector<double> u, std::vector<double> v, std::vector<double> cross)
{
  cross[0] =   u[1]*v[2] - u[2]*v[1];
  cross[1] = -(u[0]*v[2] - u[2]*v[0]);
  cross[2] =   u[0]*v[1] - u[1]*v[0];
}

} // namespace nalu
} // namespace Sierra
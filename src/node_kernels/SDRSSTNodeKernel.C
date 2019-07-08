/*------------------------------------------------------------------------*/
/*  Copyright 2019 National Renewable Energy Laboratory.                  */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/

#include "node_kernels/SDRSSTNodeKernel.h"
#include "Realm.h"
#include "SolutionOptions.h"
#include "SimdInterface.h"
#include "utils/StkHelpers.h"

#include "stk_mesh/base/MetaData.hpp"

namespace sierra {
namespace nalu {

SDRSSTNodeKernel::SDRSSTNodeKernel(
  const stk::mesh::MetaData& meta
) : NGPNodeKernel<SDRSSTNodeKernel>(),
    tkeID_(get_field_ordinal(meta, "turbulent_ke")),
    sdrID_(get_field_ordinal(meta, "specific_dissipation_rate")),
    densityID_(get_field_ordinal(meta, "density")),
    tviscID_(get_field_ordinal(meta, "turbulent_viscosity")),
    dudxID_(get_field_ordinal(meta, "dudx")),
    dkdxID_(get_field_ordinal(meta, "dkdx")),
    dwdxID_(get_field_ordinal(meta, "dwdx")),
    dualNodalVolumeID_(get_field_ordinal(meta, "dual_nodal_volume")),
    fOneBlendID_(get_field_ordinal(meta, "sst_f_one_blending")),
    nDim_(meta.spatial_dimension())
{}

void
SDRSSTNodeKernel::setup(Realm& realm)
{
  const auto& fieldMgr = realm.ngp_field_manager();

  tke_             = fieldMgr.get_field<double>(tkeID_);
  sdr_             = fieldMgr.get_field<double>(sdrID_);
  density_         = fieldMgr.get_field<double>(densityID_);
  tvisc_           = fieldMgr.get_field<double>(tviscID_);
  dudx_            = fieldMgr.get_field<double>(dudxID_);
  dkdx_            = fieldMgr.get_field<double>(dkdxID_);
  dwdx_            = fieldMgr.get_field<double>(dwdxID_);
  dualNodalVolume_ = fieldMgr.get_field<double>(dualNodalVolumeID_);
  fOneBlend_       = fieldMgr.get_field<double>(fOneBlendID_);

  const std::string dofName = "specific_dissipation_rate";
  relaxFac_ = realm.solutionOptions_->get_relaxation_factor(dofName);

  // Update turbulence model constants
  betaStar_ = realm.get_turb_model_constant(TM_betaStar);
  tkeProdLimitRatio_ = realm.get_turb_model_constant(TM_tkeProdLimitRatio);
  sigmaWTwo_ = realm.get_turb_model_constant(TM_sigmaWTwo);
  betaOne_ = realm.get_turb_model_constant(TM_betaOne);
  betaTwo_ = realm.get_turb_model_constant(TM_betaTwo);
  gammaOne_ = realm.get_turb_model_constant(TM_gammaOne);
  gammaTwo_ = realm.get_turb_model_constant(TM_gammaTwo);
}

void
SDRSSTNodeKernel::execute(
  NodeKernelTraits::LhsType& lhs,
  NodeKernelTraits::RhsType& rhs,
  const stk::mesh::FastMeshIndex& node)
{
  using DblType = NodeKernelTraits::DblType;

  const DblType tke       = tke_.get(node, 0);
  const DblType sdr       = sdr_.get(node, 0);
  const DblType density   = density_.get(node, 0);
  const DblType tvisc     = tvisc_.get(node, 0);
  const DblType dVol      = dualNodalVolume_.get(node, 0);
  const DblType fOneBlend = fOneBlend_.get(node, 0);

  DblType Pk = 0.0;
  DblType crossDiff = 0.0;
  for (int i=0; i < nDim_; ++i) {
    crossDiff += dkdx_.get(node, i) * dwdx_.get(node, i);
    const int offset = nDim_ * i;
    for (int j=0; j < nDim_; ++j) {
      const auto dudxij = dudx_.get(node, offset+j);
      Pk += dudxij * (dudxij + dudx_.get(node, j*nDim_ + i));
    }
  }
  Pk *= tvisc;

  const DblType Dk = betaStar_ * density * sdr * tke;

  // Clip production term
  Pk = stk::math::min(tkeProdLimitRatio_ * Dk, Pk);

  // Blend constants for SDR
  const DblType omf1 = (1.0 - fOneBlend);
  const DblType beta = fOneBlend * betaOne_ + omf1 * betaTwo_;
  const DblType gamma = fOneBlend * gammaOne_ + omf1 * gammaTwo_;
  const DblType sigmaD = 2.0 * omf1 * sigmaWTwo_;

  // Production term with appropriate clipping of tvisc
  const DblType Pw = gamma * density * Pk / stk::math::max(tvisc, 1.0e-16);
  const DblType Dw = beta * density * sdr * sdr;
  const DblType Sw = sigmaD * density * crossDiff / sdr;

  rhs(0) += (Pw - Dw + Sw) * dVol;
  lhs(0, 0) += (2.0 * beta * density * sdr + stk::math::max(Sw / sdr, 0.0)) *
               dVol / relaxFac_;
}

} // namespace nalu
}  // sierra
// Copyright 2017 National Technology & Engineering Solutions of Sandia, LLC
// (NTESS), National Renewable Energy Laboratory, University of Texas Austin,
// Northwest Research Associates. Under the terms of Contract DE-NA0003525
// with NTESS, the U.S. Government retains certain rights in this software.
//
// This software is released under the BSD 3-clause license. See LICENSE file
// for more details.
//

#include "ngp_algorithms/MdotEdgeAlg.h"
#include "ngp_utils/NgpLoopUtils.h"
#include "ngp_utils/NgpFieldOps.h"
#include "ngp_utils/NgpFieldManager.h"
#include "Realm.h"
#include "SolutionOptions.h"
#include "utils/StkHelpers.h"
#include "stk_mesh/base/NgpMesh.hpp"
#include "stk_mesh/base/NgpField.hpp"

namespace sierra {
namespace nalu {

MdotEdgeAlg::MdotEdgeAlg(Realm& realm, stk::mesh::Part* part)
  : Algorithm(realm, part),
    coordinates_(
      get_field_ordinal(realm.meta_data(), realm.get_coordinates_name())),
    velocity_(get_field_ordinal(
      realm.meta_data(),
      realm.does_mesh_move() && !realm.has_mesh_deformation() ? "velocity_rtm"
                                                              : "velocity")),
    pressure_(get_field_ordinal(realm.meta_data(), "pressure")),
    densityNp1_(
      get_field_ordinal(realm.meta_data(), "density", stk::mesh::StateNP1)),
    Gpdx_(get_field_ordinal(realm.meta_data(), "dpdx")),
    edgeAreaVec_(get_field_ordinal(
      realm.meta_data(), "edge_area_vector", stk::topology::EDGE_RANK)),
    Udiag_(get_field_ordinal(realm.meta_data(), "momentum_diag")),
    massFlowRate_(get_field_ordinal(
      realm.meta_data(), "mass_flow_rate", stk::topology::EDGE_RANK))
{
}

void
MdotEdgeAlg::execute()
{
  using EntityInfoType = nalu_ngp::EntityInfo<stk::mesh::NgpMesh>;
  constexpr int NDimMax = 3;
  const auto& meta = realm_.meta_data();
  const int ndim = meta.spatial_dimension();

  const std::string dofName = "pressure";
  const DblType nocFac = (realm_.get_noc_usage(dofName)) ? 1.0 : 0.0;

  // Interpolation option for rho*U
  const DblType interpTogether = realm_.get_mdot_interp();
  const DblType om_interpTogether = (1.0 - interpTogether);

  // STK stk::mesh::NgpField instances for capture by lambda
  auto ngpMesh = realm_.ngp_mesh();
  auto& fieldMgr = realm_.ngp_field_manager();
  auto coordinates = fieldMgr.get_field<double>(coordinates_);
  auto velocity = fieldMgr.get_field<double>(velocity_);
  auto Gpdx = fieldMgr.get_field<double>(Gpdx_);
  auto density = fieldMgr.get_field<double>(densityNp1_);
  auto pressure = fieldMgr.get_field<double>(pressure_);
  auto udiag = fieldMgr.get_field<double>(Udiag_);
  auto edgeAreaVec = fieldMgr.get_field<double>(edgeAreaVec_);

  stk::mesh::NgpField<double> edgeFaceVelMag;

  bool needs_gcl = false;
  if (realm_.has_mesh_deformation()) {
    needs_gcl = true;
    edgeFaceVelMag_ = get_field_ordinal(
      realm_.meta_data(), "edge_face_velocity_mag", stk::topology::EDGE_RANK);
    edgeFaceVelMag = fieldMgr.get_field<double>(edgeFaceVelMag_);
    edgeFaceVelMag.sync_to_device();
  }
  auto mdot = fieldMgr.get_field<double>(massFlowRate_);

  const bool add_balanced_forcing =
    realm_.solutionOptions_->use_balanced_buoyancy_force_;

  DblType gravity[3] = {};
  if (add_balanced_forcing) {
    const auto& solnOptsGravity =
      realm_.solutionOptions_->get_gravity_vector(ndim);
    for (int idim = 0; idim < ndim; ++idim) {
      gravity[idim] = solnOptsGravity[idim];
    }
  }

  auto source = !add_balanced_forcing
                  ? fieldMgr.get_field<double>(Gpdx_)
                  : fieldMgr.get_field<double>(
                      get_field_ordinal(realm_.meta_data(), "buoyancy_source"));
  auto source_mask = !add_balanced_forcing
                       ? fieldMgr.get_field<double>(densityNp1_)
                       : fieldMgr.get_field<double>(get_field_ordinal(
                           realm_.meta_data(), "buoyancy_source_mask"));

  mdot.clear_sync_state();
  coordinates.sync_to_device();
  velocity.sync_to_device();
  Gpdx.sync_to_device();
  density.sync_to_device();
  pressure.sync_to_device();
  udiag.sync_to_device();
  edgeAreaVec.sync_to_device();
  source.sync_to_device();
  source_mask.sync_to_device();

  const stk::mesh::Selector sel = meta.locally_owned_part() &
                                  stk::mesh::selectUnion(partVec_) &
                                  !(realm_.get_inactive_selector());

  nalu_ngp::run_edge_algorithm(
    "compute_mdot_edge_interior", ngpMesh, sel,
    KOKKOS_LAMBDA(const EntityInfoType& einfo) {
      NALU_ALIGNED DblType av[NDimMax];

      for (int d = 0; d < ndim; ++d)
        av[d] = edgeAreaVec.get(einfo.meshIdx, d);

      const auto& nodes = einfo.entityNodes;
      const auto nodeL = ngpMesh.fast_mesh_index(nodes[0]);
      const auto nodeR = ngpMesh.fast_mesh_index(nodes[1]);

      const DblType pressureL = pressure.get(nodeL, 0);
      const DblType pressureR = pressure.get(nodeR, 0);

      const DblType densityL = density.get(nodeL, 0);
      const DblType densityR = density.get(nodeR, 0);

      const DblType udiagL = udiag.get(nodeL, 0);
      const DblType udiagR = udiag.get(nodeR, 0);

      const DblType projTimeScale = 0.5 * (1.0 / udiagL + 1.0 / udiagR);
      const DblType rhoIp = 0.5 * (densityL + densityR);

      DblType axdx = 0.0;
      DblType asq = 0.0;
      for (int d = 0; d < ndim; ++d) {
        const DblType dxj =
          coordinates.get(nodeR, d) - coordinates.get(nodeL, d);
        asq += av[d] * av[d];
        axdx += av[d] * dxj;
      }
      const DblType inv_axdx = 1.0 / axdx;

      DblType tmdot = -projTimeScale * (pressureR - pressureL) * asq * inv_axdx;

      if (add_balanced_forcing) {
        const DblType masked_weights =
          0.5 * (source_mask.get(nodeL, 0) + source_mask.get(nodeR, 0));
        for (int d = 0; d < ndim; ++d) {
          tmdot += projTimeScale * av[d] * gravity[d] * rhoIp * masked_weights;
        }
      }

      if (needs_gcl) {
        tmdot -= rhoIp * edgeFaceVelMag.get(einfo.meshIdx, 0);
      }
      for (int d = 0; d < ndim; ++d) {
        const DblType dxj =
          coordinates.get(nodeR, d) - coordinates.get(nodeL, d);
        // non-orthogonal correction
        const DblType kxj = av[d] - asq * inv_axdx * dxj;
        const DblType rhoUjIp = 0.5 * (densityR * velocity.get(nodeR, d) +
                                       densityL * velocity.get(nodeL, d));
        const DblType ujIp =
          0.5 * (velocity.get(nodeR, d) + velocity.get(nodeL, d));
        DblType GjIp =
          0.5 * (Gpdx.get(nodeR, d) / udiagR + Gpdx.get(nodeL, d) / udiagL);
        if (add_balanced_forcing) {
          GjIp -=
            0.5 *
            ((source_mask.get(nodeR, 0) * source.get(nodeR, d)) / (udiagR) +
             (source_mask.get(nodeL, 0) * source.get(nodeL, d)) / (udiagL));
        }

        tmdot +=
          (interpTogether * rhoUjIp + om_interpTogether * rhoIp * ujIp + GjIp) *
            av[d] -
          kxj * GjIp * nocFac;
      }

      // Update edge field
      mdot.get(einfo.meshIdx, 0) = tmdot;
    });

  // Flag that the field has been modified on device for future sync
  mdot.modify_on_device();
}

} // namespace nalu
} // namespace sierra

/*------------------------------------------------------------------------*/
/*  Copyright 2014 National Renewable Energy Laboratory.                  */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/

#ifndef MOMENTUMNSOELEMKERNEL_H
#define MOMENTUMNSOELEMKERNEL_H

#include "Kernel.h"
#include "FieldTypeDef.h"

#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Entity.hpp>

#include <Kokkos_Core.hpp>

namespace sierra {
namespace nalu {

class SolutionOptions;
class MasterElement;
class ElemDataRequests;
class ScratchViews;

/** NSO for momentum equation
 *
 */
template<typename AlgTraits>
class MomentumNSOElemKernel : public Kernel
{
public:
  MomentumNSOElemKernel(
    const stk::mesh::BulkData&,
    const SolutionOptions&,
    VectorFieldType*,
    GenericFieldType*,
    ScalarFieldType*,
    const double,
    const double,
    ElemDataRequests&);

  virtual ~MomentumNSOElemKernel() {}

  virtual void setup(const TimeIntegrator&);

  virtual void execute(
    SharedMemView<double**>&,
    SharedMemView<double*>&,
    ScratchViews&);

private:
  MomentumNSOElemKernel() = delete;

  VectorFieldType *velocityNm1_{nullptr};
  VectorFieldType *velocityN_{nullptr};
  VectorFieldType *velocityNp1_{nullptr};
  ScalarFieldType *densityNm1_{nullptr};
  ScalarFieldType *densityN_{nullptr};
  ScalarFieldType *densityNp1_{nullptr};
  ScalarFieldType *pressure_{nullptr};
  VectorFieldType *velocityRTM_{nullptr};
  VectorFieldType *coordinates_{nullptr};
  ScalarFieldType *viscosity_{nullptr};
  GenericFieldType *Gju_{nullptr};

  const int *lrscv_;

  double dt_{0.0};
  double gamma1_{0.0};
  double gamma2_{0.0};
  double gamma3_{0.0};
  const double Cupw_{0.1};
  const double fourthFac_;
  const double altResFac_;
  const double om_altResFac_;
  const double nonConservedForm_{0.0};
  const double includeDivU_;
  const bool shiftedGradOp_;
  const double small_{1.0e-16};

  // fixed scratch space
  Kokkos::View<double[AlgTraits::numScsIp_][AlgTraits::nodesPerElement_]> v_shape_function_{"v_shape_function"};
  Kokkos::View<double[AlgTraits::nDim_][AlgTraits::nDim_]> v_kd_{"v_kd"};
};

}  // nalu
}  // sierra

#endif /* MOMENTUMNSOELEMKERNEL_H */
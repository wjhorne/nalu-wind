/*------------------------------------------------------------------------*/
/*  Copyright 2019 National Renewable Energy Laboratory.                  */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/

#include "kernels/UnitTestKernelUtils.h"
#include "UnitTestUtils.h"
#include "UnitTestHelperObjects.h"

#include "node_kernels/MomentumMassBDFNodeKernel.h"

#ifndef KOKKOS_ENABLE_CUDA
namespace {
namespace bdf_golds {
namespace momentum_mass {

static constexpr double rhs[24] =
{0, 0, 0,
 -0.056021853088904, -1.0112712429687, 0,
 1.0112712429687, -0.056021853088904, 0,
 0.53838846959557, -0.65043217577338, 0,
 0, 0, 0,
 -0.056021853088904, -1.0112712429687, 0,
 1.0112712429687, -0.056021853088904, 0,
 0.53838846959557, -0.65043217577338, 0, };

} // momentum_mass
} // bdf_golds
} // anonymous namespace
#endif

TEST_F(MomentumKernelHex8Mesh, NGP_momentum_mass_node)
{
  // Only execute for 1 processor runs
  if (bulk_.parallel_size() > 1) return;

  fill_mesh_and_init_fields();

  const unsigned nDofs = 3;

  sierra::nalu::TimeIntegrator timeIntegrator;
  timeIntegrator.timeStepN_ = 0.1;
  timeIntegrator.timeStepNm1_ = 0.1;
  timeIntegrator.gamma1_ = 1.0;
  timeIntegrator.gamma2_ = -1.0;
  timeIntegrator.gamma3_ = 0.0;

  unit_test_utils::NodeHelperObjects helperObjs(
    bulk_, stk::topology::HEX_8, nDofs, partVec_[0]);

  helperObjs.realm.timeIntegrator_ = &timeIntegrator;

  helperObjs.nodeAlg->add_kernel<sierra::nalu::MomentumMassBDFNodeKernel>(bulk_);

  helperObjs.execute();

#ifndef KOKKOS_ENABLE_CUDA
  EXPECT_EQ(helperObjs.linsys->lhs_.extent(0), 24u);
  EXPECT_EQ(helperObjs.linsys->lhs_.extent(1), 24u);
  EXPECT_EQ(helperObjs.linsys->rhs_.extent(0), 24u);
  EXPECT_EQ(helperObjs.linsys->numSumIntoCalls_, 8);

  // Exact LHS expected
  std::vector<double> lhsExact(576, 0.0);
  for (int i=0; i<24; i++)
    lhsExact[i*24+i] = 1.25;

  namespace gold_values = bdf_golds::momentum_mass;
  unit_test_kernel_utils::expect_all_near(
    helperObjs.linsys->rhs_, gold_values::rhs, 1.0e-12);
  unit_test_kernel_utils::expect_all_near_2d(
    helperObjs.linsys->lhs_, lhsExact.data());
#endif

}
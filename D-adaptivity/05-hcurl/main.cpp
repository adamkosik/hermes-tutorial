#include "hermes2d.h"

using namespace Hermes;
using namespace Hermes::Hermes2D;
using namespace Hermes::Hermes2D::RefinementSelectors;

typedef std::complex<double> complex;

//  This example comes with an exact solution, and it describes the diffraction
//  of an electromagnetic wave from a re-entrant corner. Convergence graphs saved
//  (both exact error and error estimate, and both wrt. dof number and cpu time).
//
//  PDE: time-harmonic Maxwell's equations
//
//  Known exact solution, see functions exact_sol_val(), exact_sol(), exact()
//
//  Domain: L-shape domain
//
//  Meshes: you can use either "lshape3q.mesh" (quadrilateral mesh) or
//          "lshape3t.mesh" (triangular mesh). See the mesh->load(...) command below.
//
//  BC: perfect conductor on boundary markers 1 and 6 (essential BC)
//      impedance boundary condition on rest of boundary (natural BC)
//
//  The following parameters can be changed:

// Set to "false" to suppress Hermes OpenGL visualization.
const bool HERMES_VISUALIZATION = true;
// Initial polynomial degree. NOTE: The meaning is different from
// standard continuous elements in the space H1. Here, P_INIT refers
// to the maximum poly order of the tangential component, and polynomials
// of degree P_INIT + 1 are present in element interiors. P_INIT = 0
// is for Whitney elements.
const int P_INIT = 2;
// Number of initial uniform mesh refinements.
const int INIT_REF_NUM = 1;
// Parameter influencing the candidate selection.
const double THRESHOLD = 0.3;
// Predefined list of element refinement candidates. Possible values are
// H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
// H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
// See User Documentation for details.
const CandList CAND_LIST = H2D_HP_ANISO;
// Stopping criterion for adaptivity (rel. error tolerance between the
// reference mesh and coarse mesh solution in percent).
const double ERR_STOP = 1.0;
// Error calculation & adaptivity.
DefaultErrorCalculator<double, HERMES_H1_NORM> errorCalculator(RelativeErrorToGlobalNorm, 1);
// Stopping criterion for an adaptivity step.
AdaptStoppingCriterionSingleElement<complex> stoppingCriterion(THRESHOLD);
// Adaptivity processor class.
Adapt<complex> adaptivity(&errorCalculator, &stoppingCriterion);
// Selector.
H1ProjBasedSelector<complex> selector(CAND_LIST);

// Problem parameters.
const double MU_R   = 1.0;
const double KAPPA  = 1.0;
const double LAMBDA = 1.0;

// Bessel functions, exact solution, and weak forms.
#include "definitions.cpp"

int main(int argc, char* argv[])
{
  // Load the mesh.
  MeshSharedPtr mesh(new Mesh);
  MeshReaderH2D mloader;
  mloader.load("lshape3q.mesh", mesh);    // quadrilaterals
  //mloader.load("lshape3t.mesh", mesh);  // triangles

  // Perform initial mesh refinemets.
  for (int i = 0; i < INIT_REF_NUM; i++)  mesh->refine_all_elements();

  // Initialize boundary conditions.
  Hermes::Hermes2D::DefaultEssentialBCConst<std::complex<double> > bc_essential(Hermes::vector<std::string>("Corner_horizontal",
    "Corner_vertical"), 0);
  EssentialBCs<std::complex<double> > bcs(&bc_essential);

  // Create an Hcurl space with default shapeset.
  SpaceSharedPtr<std::complex<double> > space(new HcurlSpace<std::complex<double> >(mesh, &bcs, P_INIT));
  int ndof = space->get_num_dofs();

  // Initialize the weak formulation.
  CustomWeakForm wf(MU_R, KAPPA);

  // Initialize coarse and reference mesh solutions.
  MeshFunctionSharedPtr<std::complex<double> >  sln(new Solution<std::complex<double> >), ref_sln(new Solution<std::complex<double> >);

  // Initialize exact solution.
  MeshFunctionSharedPtr<std::complex<double> > sln_exact(new CustomExactSolution(mesh));

  // Initialize refinement selector.
  HcurlProjBasedSelector<std::complex<double> > selector(CAND_LIST, H2DRS_DEFAULT_ORDER);

  // Initialize views.
  Views::VectorView v_view("Solution (magnitude)", new Views::WinGeom(0, 0, 460, 350));
  v_view.set_min_max_range(0, 1.5);
  Views::OrderView  o_view("Polynomial orders", new Views::WinGeom(470, 0, 400, 350));

  // DOF and CPU convergence graphs.
  SimpleGraph graph_dof_est, graph_cpu_est,
    graph_dof_exact, graph_cpu_exact;

  DiscreteProblem<std::complex<double> > dp(&wf, space);

  // Perform Newton's iteration and translate the resulting coefficient vector into a Solution.
  Hermes::Hermes2D::NewtonSolver<std::complex<double> > newton(&dp);

  Views::Linearizer lin;
  Views::Orderizer ord;
  Views::Vectorizer vec;

  // Adaptivity loop:
  int as = 1; bool done = false;
  do
  {
    // Construct globally refined reference mesh and setup reference space.
    Mesh::ReferenceMeshCreator ref_mesh_creator(mesh);
    MeshSharedPtr ref_mesh = ref_mesh_creator.create_ref_mesh();
    Space<std::complex<double> >::ReferenceSpaceCreator ref_space_creator(space, ref_mesh);
    SpaceSharedPtr<std::complex<double> > ref_space = ref_space_creator.create_ref_space();

    newton.set_space(ref_space);
    int ndof_ref = ref_space->get_num_dofs();

    // Initial coefficient vector for the Newton's method.
    std::complex<double>* coeff_vec = new std::complex<double>[ndof_ref];
    memset(coeff_vec, 0, ndof_ref * sizeof(std::complex<double>));
    
    try
    {
      newton.solve(coeff_vec);
    }
    catch(Hermes::Exceptions::Exception& e)
    {
      e.print_msg();
    }
    Hermes::Hermes2D::Solution<std::complex<double> >::vector_to_solution(newton.get_sln_vector(), ref_space, ref_sln);

    // Project the fine mesh solution onto the coarse mesh.
    OGProjection<std::complex<double> > ogProjection;
    ogProjection.project_global(space, ref_sln, sln);

    // View the coarse mesh solution and polynomial orders.
    if(HERMES_VISUALIZATION)
    {
      MeshFunctionSharedPtr<double> real_filter(new RealFilter(sln));
      v_view.show(real_filter);
      o_view.show(space);
      lin.save_solution_vtk(real_filter, "sln.vtk", "a");
      ord.save_mesh_vtk(space, "mesh.vtk");
      lin.free();
    }

    // Calculate element errors and total error estimate.
    Adapt<std::complex<double> >* adaptivity = new Adapt<std::complex<double> >(space);
    adaptivity->set_verbose_output(true);

    double err_est_rel = adaptivity->calc_err_est(sln, ref_sln) * 100;

    Hermes::Mixins::Loggable::Static::info("\nError estimate: %f%%.\n", err_est_rel);

    // Calculate exact error.
    bool solutions_for_adapt = false;
    double err_exact_rel = adaptivity->calc_err_exact(sln, sln_exact, solutions_for_adapt) * 100;

    // Add entry to DOF and CPU convergence graphs.
    graph_dof_est.add_values(space->get_num_dofs(), err_est_rel);
    graph_dof_est.save("conv_dof_est.dat");
    graph_dof_exact.add_values(space->get_num_dofs(), err_exact_rel);
    graph_dof_exact.save("conv_dof_exact.dat");

    // If err_est_rel too large, adapt the mesh.
    if(err_est_rel < ERR_STOP) done = true;
    else
    {
      done = adaptivity.adapt(&selector);

      // Increase the counter of performed adaptivity steps.
      if(done == false)  as++;
    }
    if(space->get_num_dofs() >= NDOF_STOP) done = true;

    // Clean up.
    delete [] coeff_vec;
    delete adaptivity;
  }
  while (done == false);

  // Show the reference solution - the final result.
  if(HERMES_VISUALIZATION)
  {
    v_view.set_title("Fine mesh solution (magnitude)");
    MeshFunctionSharedPtr<double> real_filter(new RealFilter(ref_sln));
    v_view.show(real_filter);

    // Wait for all views to be closed.
    Views::View::wait();
  }
  return 0;
}
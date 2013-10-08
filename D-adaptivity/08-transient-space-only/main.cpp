#include "definitions.h"

using namespace RefinementSelectors;
using namespace Views;

//  This example is derived from example P03-timedep/03-nonlinear
//  and it shows how automatic adaptivity in space can be combined with 
//  arbitrary Runge-Kutta methods in time. The example uses fixed time 
//  step size.
//
//  For a list of available R-K methods see the file hermes_common/tables.h.
//
//  PDE: time-dependent heat transfer equation with nonlinear thermal
//  conductivity, du/dt = div[lambda(u) grad u] + f.
//
//  Nonlinearity: lambda(u) = 1 + pow(u, alpha).
//
//  Domain: square (-10, 10)^2.
//
//  BC: Nonconstant Dirichlet.
//
//  IC: Custom initial condition matching the BC.
//
//  The following parameters can be changed:

// Number of initial uniform mesh refinements.
const int INIT_REF_NUM = 2;                       
// Initial polynomial degree of all mesh elements.
const int P_INIT = 2;                             
// Time step. 
double time_step = 0.05;                           
// Time interval length.
const double T_FINAL = 2.0;                       

// Adaptivity
// Every UNREF_FREQth time step the mesh is derefined.
const int UNREF_FREQ = 1;                         
// 1... mesh reset to basemesh and poly degrees to P_INIT.   
// 2... one ref. layer shaved off, poly degrees reset to P_INIT.
// 3... one ref. layer shaved off, poly degrees decreased by one. 
const int UNREF_METHOD = 3;                       
// Parameter influencing the candidate selection.
const double THRESHOLD = 0.3;                        
// Predefined list of element refinement candidates. Possible values are
// H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
// H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
const CandList CAND_LIST = H2D_HP_ANISO;                      
// Stopping criterion for adaptivity.
const double ERR_STOP = 1.0; 
// Error calculation & adaptivity.
DefaultErrorCalculator<double, HERMES_H1_NORM> errorCalculator(RelativeErrorToGlobalNorm, 1);
// Stopping criterion for an adaptivity step.
AdaptStoppingCriterionSingleElement<double> stoppingCriterion(THRESHOLD);
// Adaptivity processor class.
Adapt<double> adaptivity(&errorCalculator, &stoppingCriterion);
// Selector.
H1ProjBasedSelector<double> selector(CAND_LIST);

// Newton's method
// Stopping criterion for Newton on fine mesh.
const double NEWTON_TOL = 1e-5;                   
// Maximum allowed number of Newton iterations.
const int NEWTON_MAX_ITER = 20;                   

// Choose one of the following time-integration methods, or define your own Butcher's table. The last number
// in the name of each method is its order. The one before last, if present, is the number of stages.
// Explicit methods:
//   Explicit_RK_1, Explicit_RK_2, Explicit_RK_3, Explicit_RK_4.
// Implicit methods:
//   Implicit_RK_1, Implicit_Crank_Nicolson_2_2, Implicit_SIRK_2_2, Implicit_ESIRK_2_2, Implicit_SDIRK_2_2,
//   Implicit_Lobatto_IIIA_2_2, Implicit_Lobatto_IIIB_2_2, Implicit_Lobatto_IIIC_2_2, Implicit_Lobatto_IIIA_3_4,
//   Implicit_Lobatto_IIIB_3_4, Implicit_Lobatto_IIIC_3_4, Implicit_Radau_IIA_3_5, Implicit_SDIRK_5_4.
// Embedded explicit methods:
//   Explicit_HEUN_EULER_2_12_embedded, Explicit_BOGACKI_SHAMPINE_4_23_embedded, Explicit_FEHLBERG_6_45_embedded,
//   Explicit_CASH_KARP_6_45_embedded, Explicit_DORMAND_PRINCE_7_45_embedded.
// Embedded implicit methods:
//   Implicit_SDIRK_CASH_3_23_embedded, Implicit_ESDIRK_TRBDF2_3_23_embedded, Implicit_ESDIRK_TRX2_3_23_embedded,
//   Implicit_SDIRK_BILLINGTON_3_23_embedded, Implicit_SDIRK_CASH_5_24_embedded, Implicit_SDIRK_CASH_5_34_embedded,
//   Implicit_DIRK_ISMAIL_7_45_embedded.
ButcherTableType butcher_table_type = Implicit_RK_1;

// Problem parameters.
// Parameter for nonlinear thermal conductivity.
const double alpha = 4.0;                         
const double heat_src = 1.0;

int main(int argc, char* argv[])
{
  // Choose a Butcher's table or define your own.
  ButcherTable bt(butcher_table_type);
  if (bt.is_explicit()) Hermes::Mixins::Loggable::Static::info("Using a %d-stage explicit R-K method.", bt.get_size());
  if (bt.is_diagonally_implicit()) Hermes::Mixins::Loggable::Static::info("Using a %d-stage diagonally implicit R-K method.", bt.get_size());
  if (bt.is_fully_implicit()) Hermes::Mixins::Loggable::Static::info("Using a %d-stage fully implicit R-K method.", bt.get_size());

  // Load the mesh.
  MeshSharedPtr mesh(new Mesh), basemesh(new Mesh);
  MeshReaderH2D mloader;
  mloader.load("square.mesh", basemesh);

  // Perform initial mesh refinements.
  for(int i = 0; i < INIT_REF_NUM; i++) basemesh->refine_all_elements(0, true);
  mesh->copy(basemesh);
  
  // Initialize boundary conditions.
  EssentialBCNonConst bc_essential("Bdy");
  EssentialBCs<double> bcs(&bc_essential);

  // Create an H1 space with default shapeset.
  SpaceSharedPtr<double> space(new H1Space<double>(mesh, &bcs, P_INIT));
  int ndof_coarse = space->get_num_dofs();

  // Previous time level solution (initialized by initial condition).
  MeshFunctionSharedPtr<double> sln_time_prev(new CustomInitialCondition(mesh));

  // Initialize the weak formulation
  CustomNonlinearity lambda(alpha);
  Hermes2DFunction<double> f(heat_src);
  WeakFormsH1::DefaultWeakFormPoisson<double> wf(HERMES_ANY, &lambda, &f);

  // Next time level solution.
  MeshFunctionSharedPtr<double> sln_time_new(new Solution<double>(mesh));

  // Create a refinement selector.
  H1ProjBasedSelector<double> selector(CAND_LIST, H2DRS_DEFAULT_ORDER);

  // Visualize initial condition.
  char title[100];
  ScalarView view("Initial condition", new WinGeom(0, 0, 440, 350));
  OrderView ordview("Initial mesh", new WinGeom(445, 0, 410, 350));
  view.show(sln_time_prev);
  ordview.show(space);
  
  // Initialize Runge-Kutta time stepping.
  RungeKutta<double> runge_kutta(&wf, space, &bt);
      
  // Time stepping loop.
  double current_time = 0; int ts = 1;
  do 
  {
    // Periodic global derefinement.
    if (ts > 1 && ts % UNREF_FREQ == 0) 
    {
      Hermes::Mixins::Loggable::Static::info("Global mesh derefinement.");
      switch (UNREF_METHOD) {
        case 1: mesh->copy(basemesh);
                space->set_uniform_order(P_INIT);
                break;
        case 2: mesh->unrefine_all_elements();
                space->set_uniform_order(P_INIT);
                break;
        case 3: mesh->unrefine_all_elements();
                space->adjust_element_order(-1, -1, P_INIT, P_INIT);
                break;
      }

      // Important. Since the space was changed, we need to re-assign DOFs.
      // Try commenting this and see that an exception is thrown.
      space->assign_dofs();
      try
      {
        ndof_coarse = Space<double>::get_num_dofs(space);
      }
      catch(Hermes::Exceptions::Exception& e)
      {
        e.print_msg();
      }
    }

    // Spatial adaptivity loop. Note: sln_time_prev must not be changed 
    // during spatial adaptivity. 
    bool done = false; int as = 1;
    double err_est;
    do {
      Hermes::Mixins::Loggable::Static::info("Time step %d, adaptivity step %d:", ts, as);

      // Construct globally refined reference mesh and setup reference space.
      Mesh::ReferenceMeshCreator ref_mesh_creator(mesh);
      MeshSharedPtr ref_mesh = ref_mesh_creator.create_ref_mesh();
      Space<double>::ReferenceSpaceCreator ref_space_creator(space, ref_mesh);
      SpaceSharedPtr<double> ref_space = ref_space_creator.create_ref_space();
      int ndof_ref = Space<double>::get_num_dofs(ref_space);

      // Perform one Runge-Kutta time step according to the selected Butcher's table.
      try
      {
        runge_kutta.set_space(ref_space);
        runge_kutta.set_verbose_output(true);
        runge_kutta.set_time(current_time);
        runge_kutta.set_time_step(time_step);
        runge_kutta.rk_time_step_newton(sln_time_prev, sln_time_new);
      }
      catch(Exceptions::Exception& e)
      {
        std::cout << e.what();
        
      }

      // Project the fine mesh solution onto the coarse mesh.
      MeshFunctionSharedPtr<double> sln_coarse(new Solution<double>);
      Hermes::Mixins::Loggable::Static::info("Projecting fine mesh solution on coarse mesh for error estimation.");
      OGProjection<double>::project_global(space, sln_time_new, sln_coarse); 

      // Calculate element errors and total error estimate.
      Hermes::Mixins::Loggable::Static::info("Calculating error estimate.");
      Adapt<double>* adaptivity = new Adapt<double>(space);
      double err_est_rel_total = adaptivity->calc_err_est(sln_coarse, sln_time_new) * 100;

      // Report results.
      Hermes::Mixins::Loggable::Static::info("ndof_coarse: %d, ndof_ref: %d, err_est_rel: %g%%", 
           Space<double>::get_num_dofs(space), Space<double>::get_num_dofs(ref_space), err_est_rel_total);

      // If err_est too large, adapt the mesh.
      if (err_est_rel_total < ERR_STOP) done = true;
      else 
      {
        Hermes::Mixins::Loggable::Static::info("Adapting the coarse mesh.");
        done = adaptivity.adapt(&selector);

        if (Space<double>::get_num_dofs(space) >= NDOF_STOP) 
          done = true;
        else
          // Increase the counter of performed adaptivity steps.
          as++;
      }
      
      // Visualize the solution and mesh.
      char title[100];
      sprintf(title, "Solution, time %g", current_time);
      view.set_title(title);
      view.show_mesh(false);
      view.show(sln_time_new);
      sprintf(title, "Mesh, time %g", current_time);
      ordview.set_title(title);
      ordview.show(space);

      // Clean up.
      delete adaptivity;
    }
    while (done == false);

    sln_time_prev->copy(sln_time_new);

    // Increase current time and counter of time steps.
    current_time += time_step;
    ts++;
  }
  while (current_time < T_FINAL);

  // Wait for all views to be closed.
  View::wait();
  return 0;
}

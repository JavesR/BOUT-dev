#include <bout.hxx>
#include <boutmain.hxx>
#include <initialprofiles.hxx>
#include <derivs.hxx>
#include <math.h>
#include "mathematica.h"


void solution(Field3D &f, BoutReal t, BoutReal D);
int error_monitor(Solver *solver, BoutReal simtime, int iter, int NOUT);
BoutReal MS(BoutReal t, BoutReal  x, BoutReal  y, BoutReal  z);
BoutReal dxMS(BoutReal t, BoutReal  x, BoutReal  y, BoutReal  z);
Field3D MMS_Source(BoutReal t);



const BoutReal PI = 3.141592653589793;

Field3D N;
Field3D E_N, S,source; //N error vector, solution,source

BoutReal mu_N; // Parallel collisional diffusion coefficient
BoutReal Lx, Ly, Lz;

int physics_init(bool restarting) {
  // Get the options
  Options *meshoptions = Options::getRoot()->getSection("mesh");

  meshoptions->get("Lx",Lx,1.0);
  meshoptions->get("Ly",Ly,1.0);

  /*this assumes equidistant grid*/
  mesh->dx = Lx/(mesh->getMX());
  mesh->dy = Ly/(mesh->getMY());

  SAVE_ONCE2(Lx,Ly);

  Options *cytooptions = Options::getRoot()->getSection("cyto");
  cytooptions->get("dis", mu_N, 1);

  SAVE_ONCE(mu_N);

  //set mesh
  mesh->g11 = 1.0;
  mesh->g22 = 1.0;
  mesh->g33 = 1.0;
  mesh->g12 = 0.0;
  mesh->g13 = 0.0;
  mesh->g23 = 0.0;

  mesh->g_11 = 1.0;
  mesh->g_22 = 1.0;
  mesh->g_33 = 1.0;
  mesh->g_12 = 0.0;
  mesh->g_13 = 0.0;
  mesh->g_23 = 0.0;
  mesh->geometry();

  //Dirichlet everywhere except inner x-boundary Neumann
  N.addBndryFunction(MS,BNDRY_ALL);
  N.addBndryFunction(dxMS,BNDRY_XIN);

  //Dirichlet boundary conditions everywhere
  //N.addBndryFunction(MS,BNDRY_ALL);

  // Tell BOUT++ to solve N
  bout_solve(N, "N");


  //Set initial condition to MS at t = 0.
  for (int xi = mesh->xstart; xi < mesh->xend +1; xi++){
    for (int yj = mesh->ystart; yj < mesh->yend + 1; yj++){
      for (int zk = 0; zk < mesh->ngz+1; zk++) {
        N[xi][yj][zk] = MS(0.,mesh->GlobalX(xi)*Lx,mesh->GlobalY(yj)*Ly,mesh->dz*zk);
      }
    }
  }

  E_N.allocate();
  SAVE_REPEAT(E_N);
  S.allocate();
  SAVE_REPEAT(S);
  source.allocate();
  SAVE_REPEAT(source);

  error_monitor(NULL, 0,  0, 0);
  solver->addMonitor(error_monitor);

  return 0;
}

int physics_run(BoutReal t) {
  mesh->communicate(N); // Communicate guard cells

  //update time-dependent boundary conditions
  N.applyBoundary(t);

  ddt(N) = mu_N* D2DX2(N);


  //add MMS source term
  ddt(N) += MMS_Source(t);
  return 0;
}


//Manufactured solution
BoutReal MS(BoutReal t, BoutReal  x, BoutReal  y, BoutReal  z) {
  return (BoutReal)0.9 + 0.9*x + 0.2*Cos(10*t)*Sin(5.*Power(x,2));
}

//x-derivative of MS. For Neumann bnd cond
BoutReal dxMS(BoutReal t, BoutReal  x, BoutReal  y, BoutReal  z) {
  return 0.9 + 2.*x*Cos(10*t)*Cos(5.*Power(x,2));
}


//Manufactured solution
void solution(Field3D &f, BoutReal t, BoutReal D) {
  int bx = (mesh->ngx - (mesh->xend - mesh->xstart + 1)) / 2;
  int by = (mesh->ngy - (mesh->yend - mesh->ystart + 1)) / 2;
  BoutReal x,y,z;

  for (int xi = mesh->xstart - bx; xi < mesh->xend + bx + 1; xi++){
    for (int yj = mesh->ystart - by; yj < mesh->yend + by + 1; yj++){
      x = mesh->GlobalX(xi)*Lx;
      y = mesh->GlobalY(yj)*Ly;//GlobalY not fixed yet
      for (int zk = 0; zk < mesh->ngz ; zk++) {
        z = mesh->dz*zk;
        f[xi][yj][zk] = MS(t,x,y,z);
      }
    }

  }

}

//Source term calculated in and cut and pasted from Mathematica.
//\partial_t MS - \mu_N \partial^2_{xx } N = MMS_Source
Field3D MMS_Source(BoutReal t)
{
  BoutReal x,y,z;
  Field3D result;
  result.allocate();

  int xi,yj,zk;

  for(xi=mesh->xstart;xi<mesh->xend+1;xi++)
    for(yj=mesh->ystart;yj < mesh->yend+1;yj++){
      for(zk=0;zk<mesh->ngz;zk++){
        x = mesh->GlobalX(xi)*Lx;
        y = mesh->GlobalY(yj)*Ly;
        z = zk*mesh->dz;
        result[xi][yj][zk] = -2.*Sin(10*t)*Sin(5.*Power(x,2)) + Cos(10*t)*
          (-2.*Cos(5.*Power(x,2)) + 20.*Power(x,2)*Sin(5.*Power(x,2)));
      }
    }
  return result;
}

int error_monitor(Solver *solver, BoutReal simtime, int iter, int NOUT) {
  solution(S, simtime, mu_N);

  //Calculate the error. norms are calculated in the post-processing
  E_N = 0.;
  for (int xi = mesh->xstart; xi < mesh->xend + 1; xi++){
    for (int yj = mesh->ystart ; yj < mesh->yend + 1; yj++){
      for (int zk = 0; zk < mesh->ngz-1 ; zk++) {
        E_N[xi][yj][zk] = N[xi][yj][zk] - S[xi][yj][zk];
      }
    }
  }
  source = MMS_Source(simtime);
  return 0;
}


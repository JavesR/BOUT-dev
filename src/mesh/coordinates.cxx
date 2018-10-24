/**************************************************************************
 * Differential geometry
 * Calculates the covariant metric tensor, and christoffel symbol terms
 * given the contravariant metric tensor terms
 **************************************************************************/

#include <bout/assert.hxx>
#include <bout/constants.hxx>
#include <bout/coordinates.hxx>
#include <msg_stack.hxx>
#include <output.hxx>
#include <utils.hxx>

#include <derivs.hxx>
#include <fft.hxx>
#include <interpolation.hxx>

#include <globals.hxx>

// use anonymous namespace so this utility function is not available outside this file
namespace {
  /// Interpolate a Field2D to a new CELL_LOC with interp_to.
  /// Communicates to set internal guard cells.
  /// Boundary guard cells are set by extrapolating from the grid, like
  /// 'free_o3' boundary conditions
  /// Corner guard cells are set to BoutNaN
  Field2D interpolateAndExtrapolate(const Field2D &f, CELL_LOC location, bool extrap_at_branch_cut) {
    Mesh* localmesh = f.getMesh();
    Field2D result = interp_to(f, location, RGN_NOBNDRY);
    // Ensure result's data is unique. Otherwise result might be a duplicate of
    // f (if no interpolation is needed, e.g. if interpolation is in the
    // z-direction); then f would be communicated. Since this function is used
    // on geometrical quantities that might not be periodic in y even on closed
    // field lines (due to dependence on integrated shear), we don't want to
    // communicate f. We will sort out result's boundary guard cells below, but
    // not f's so we don't want to change f.
    result.allocate();
    localmesh->communicate(result);

    // Extrapolate into boundaries so that differential geometry terms can be
    // interpolated if necessary
    // Note: cannot use applyBoundary("free_o3") here because applyBoundary()
    // would try to create a new Coordinates object since we have not finished
    // initializing yet, leading to an infinite recursion.
    // Also, here we interpolate for the boundary points at xstart/ystart and
    // (xend+1)/(yend+1) instead of extrapolating.
    for (auto bndry : localmesh->getBoundaries()) {
      int extrap_start = 0;
      if ( (location == CELL_XLOW) && (bndry->bx>0) )
        extrap_start = 1;
      else if ( (location == CELL_YLOW) && (bndry->by>0) )
        extrap_start = 1;
      for(bndry->first(); !bndry->isDone(); bndry->next1d()) {
        // interpolate extra boundary point that is missed by interp_to, if
        // necessary
        if (extrap_start>0) {
          // note that either bx or by is >0 here
          result(bndry->x, bndry->y) =
            ( 9.*(f(bndry->x-bndry->bx, bndry->y-bndry->by)
                  + f(bndry->x, bndry->y))
              - f(bndry->x-2*bndry->bx, bndry->y-2*bndry->by)
              - f(bndry->x+bndry->bx, bndry->y+bndry->by)
            )/16.;
        }

        // set boundary guard cells
        if ((bndry->bx != 0 && localmesh->GlobalNx-2*bndry->width >= 3) || (bndry->by != 0 && localmesh->GlobalNy-2*bndry->width >= 3)) {
          if (bndry->bx != 0 && localmesh->LocalNx == 1 && bndry->width == 1) {
            throw BoutException("Not enough points in the x-direction on this "
                "processor for extrapolation needed to use staggered grids. "
                "Increase number of x-guard cells MXG or decrease number of "
                "processors in the x-direction NXPE.");
          }
          if (bndry->by != 0 && localmesh->LocalNy == 1 && bndry->width == 1) {
            throw BoutException("Not enough points in the y-direction on this "
                "processor for extrapolation needed to use staggered grids. "
                "Increase number of y-guard cells MYG or decrease number of "
                "processors in the y-direction NYPE.");
          }
          // extrapolate into boundary guard cells if there are enough grid points
          for(int i=extrap_start;i<bndry->width;i++) {
            int xi = bndry->x + i*bndry->bx;
            int yi = bndry->y + i*bndry->by;
            result(xi, yi) = 3.0*result(xi - bndry->bx, yi - bndry->by) - 3.0*result(xi - 2*bndry->bx, yi - 2*bndry->by) + result(xi - 3*bndry->bx, yi - 3*bndry->by);
          }
        } else {
          // not enough grid points to extrapolate, set equal to last grid point
          for(int i=extrap_start;i<bndry->width;i++) {
            result(bndry->x + i*bndry->bx, bndry->y + i*bndry->by) = result(bndry->x - bndry->bx, bndry->y - bndry->by);
          }
        }
      }
    }

    if (extrap_at_branch_cut) {
      // Extrapolate into guard cells at branch cuts on core/PF field lines
      // This overwrites any communicated values in the guard cells
      int firstjup = location==CELL_YLOW ? localmesh->yend+1 : localmesh->yend;
      for (int i=localmesh->xstart; i<=localmesh->xend; i++) {
        // Lower processor boundary
        if (localmesh->hasBranchCutDown(i)) {
          for (int j=localmesh->ystart-1; j>0; j--) {
            result(i, j) = 3.0*result(i, j+1) - 3.0*result(i, j+2) + result(i, j+3);
          }
        }
        // Upper processor boundary
        if (localmesh->hasBranchCutUp(i)) {
          if (location == CELL_YLOW) {
            // interpolate boundary point, to be symmetric with lower boundary
            int j = localmesh->yend;
            result(i, j) =
              ( 9.*(f(i, j-1) + f(i, j)) - f(i, j-2) - f(i, j+1))/16.;
          }
          for (int j=firstjup; j<localmesh->LocalNy; j++) {
            result(i, j) = 3.0*result(i, j-1) - 3.0*result(i, j-2) + result(i, j-3);
          }
        }
      }
    }

    // Set corner guard cells
    for (int i=0; i<localmesh->xstart; i++) {
      for (int j=0; j<localmesh->ystart; j++) {
        result(i, j) = BoutNaN;
        result(i, localmesh->LocalNy-1-j) = BoutNaN;
        result(localmesh->LocalNx-1-i, j) = BoutNaN;
        result(localmesh->LocalNx-1-i, localmesh->LocalNy-1-j) = BoutNaN;
      }
    }

    return result;
  }

  Field2D interpXLOWToXYCorner(const Field2D &f, bool extrap_at_branch_cut) {

    Mesh* localmesh = f.getMesh();

    // Only makes sense to use this routine if we can interpolate (4-point
    // stencil) in both x- and y-directions
    ASSERT1(localmesh->xstart > 1 && localmesh->ystart > 1);

    Field2D result = f;
    result.allocate(); // ensure we don't change f

    ASSERT1(result.getLocation() == CELL_XLOW); // check input f is at XLOW

    // Shift outer x-boundary points from f one grid point inwards to
    // interpolate/communicate/extrapolate them as if they were regular grid
    // cells at the outer boundary
    Field2D temp_for_xguards(0., localmesh);
    for (auto bndry : localmesh->getBoundaries()) {
      if (bndry->bx > 0) {
        // outer x-boundary
        for(bndry->first(); !bndry->isDone(); bndry->next1d()) {
          for (int i = localmesh->xend-1; i<localmesh->LocalNx; i++) {
            temp_for_xguards(i-1, bndry->y) = result(i, bndry->y);
          }
        }

        // Set y-guard cells by extrapolation.
        // This will be overwritten by communicate() unless the guard cells are
        // at a y-boundary.
        for (int i = localmesh->xend-2; i<=localmesh->xend; i++) {
          for (int j = localmesh->ystart-1; j>=0; j--) {
            temp_for_xguards(i, j) = 3.0*temp_for_xguards(i, j+1) -
              3.0*temp_for_xguards(i, j+2) + temp_for_xguards(i, j+3);
          }
          for (int j = localmesh->yend+1; j<localmesh->LocalNy; j++) {
            temp_for_xguards(i, j) = 3.0*temp_for_xguards(i, j-1) -
              3.0*temp_for_xguards(i, j-2) + temp_for_xguards(i, j-3);
          }
        }
      }
    }
    localmesh->communicate(temp_for_xguards);

    // pretend f is at CELL_CENTRE to interpolate in y-direction
    result.setLocation(CELL_CENTRE);
    temp_for_xguards.setLocation(CELL_CENTRE); // should be CELL_CENTRE already, but be explicit
    // interpolate grid points to XY-corner and extrapolate guard cells
    result = interpolateAndExtrapolate(result, CELL_YLOW, extrap_at_branch_cut);
    // Same for upper x-boundary points
    temp_for_xguards = interpolateAndExtrapolate(temp_for_xguards, CELL_YLOW, extrap_at_branch_cut);

    // Copy over outer x-boundary points into guard cells
    for (auto bndry : localmesh->getBoundaries()) {
      if (bndry->bx > 0) {
        // outer x-boundary
        for(bndry->first(); !bndry->isDone(); bndry->next1d()) {
          for (int i = localmesh->xend-1; i<localmesh->LocalNx; i++) {
            result(i, bndry->y) = temp_for_xguards(i-1, bndry->y);
          }
        }
      }
    }

    // Pretend result is at CELL_CENTRE because there is no XY-corner location.
    // The output from this function is only intended to be used in boundary
    // conditions where a y-boundary condition is set on a CELL_XLOW field or
    // an x-boundary condition on a CELL_YLOW field.
    // The most likely mistake is trying to add this to XLOW or YLOW field (it
    // should only be used element-wise in boundary condition loops). Setting
    // CELL_CENTRE will catch such errors.
    result.setLocation(CELL_CENTRE);

    return result;
  }
}

std::shared_ptr<Coordinates> Coordinates::getCoordinates(Mesh *mesh_in) {

  std::shared_ptr<Coordinates> result{new Coordinates(mesh_in)};

  if (mesh_in->get(result->dx, "dx")) {
    output_warn.write("\tWARNING: differencing quantity 'dx' not found. Set to 1.0\n");
    result->dx = 1.0;
  }

  if (mesh_in->periodicX) {
    mesh_in->communicate(result->dx);
  }

  if (mesh_in->get(result->dy, "dy")) {
    output_warn.write("\tWARNING: differencing quantity 'dy' not found. Set to 1.0\n");
    result->dy = 1.0;
  }

  result->nz = mesh_in->LocalNz;

  if (mesh_in->get(result->dz, "dz")) {
    // Couldn't read dz from input
    int zperiod;
    BoutReal ZMIN, ZMAX;
    Options *options = Options::getRoot();
    if (options->isSet("zperiod")) {
      OPTION(options, zperiod, 1);
      ZMIN = 0.0;
      ZMAX = 1.0 / static_cast<BoutReal>(zperiod);
    } else {
      OPTION(options, ZMIN, 0.0);
      OPTION(options, ZMAX, 1.0);

      zperiod = ROUND(1.0 / (ZMAX - ZMIN));
    }

    result->dz = (ZMAX - ZMIN) * TWOPI / result->nz;
  }

  // Diagonal components of metric tensor g^{ij} (default to 1)
  mesh_in->get(result->g11, "g11", 1.0);
  mesh_in->get(result->g22, "g22", 1.0);
  mesh_in->get(result->g33, "g33", 1.0);

  // Off-diagonal elements. Default to 0
  mesh_in->get(result->g12, "g12", 0.0);
  mesh_in->get(result->g13, "g13", 0.0);
  mesh_in->get(result->g23, "g23", 0.0);

  // Check input metrics
  if ((!finite(result->g11)) || (!finite(result->g22)) || (!finite(result->g33))) {
    throw BoutException("\tERROR: Diagonal metrics are not finite!\n");
  }
  if ((min(result->g11) <= 0.0) || (min(result->g22) <= 0.0) || (min(result->g33) <= 0.0)) {
    throw BoutException("\tERROR: Diagonal metrics are negative!\n");
  }
  if ((!finite(result->g12)) || (!finite(result->g13)) || (!finite(result->g23))) {
    throw BoutException("\tERROR: Off-diagonal metrics are not finite!\n");
  }

  /// Find covariant metric components
  // Check if any of the components are present
  if (mesh_in->sourceHasVar("g_11") or mesh_in->sourceHasVar("g_22") or
      mesh_in->sourceHasVar("g_33") or mesh_in->sourceHasVar("g_12") or
      mesh_in->sourceHasVar("g_13") or mesh_in->sourceHasVar("g_23")) {
    // Check that all components are present
    if (mesh_in->sourceHasVar("g_11") and mesh_in->sourceHasVar("g_22") and
        mesh_in->sourceHasVar("g_33") and mesh_in->sourceHasVar("g_12") and
        mesh_in->sourceHasVar("g_13") and mesh_in->sourceHasVar("g_23")) {
      mesh_in->get(result->g_11, "g_11");
      mesh_in->get(result->g_22, "g_22");
      mesh_in->get(result->g_33, "g_33");
      mesh_in->get(result->g_12, "g_12");
      mesh_in->get(result->g_13, "g_13");
      mesh_in->get(result->g_23, "g_23");

      output_warn.write("\tWARNING! Covariant components of metric tensor set manually. "
                        "Contravariant components NOT recalculated\n");

    } else {
      output_warn.write("Not all covariant components of metric tensor found. "
                        "Calculating all from the contravariant tensor\n");
      /// Calculate contravariant metric components if not found
      if (result->calcCovariant()) {
        throw BoutException("Error in calcCovariant call");
      }
    }
  } else {
    /// Calculate contravariant metric components if not found
    if (result->calcCovariant()) {
      throw BoutException("Error in calcCovariant call");
    }
  }

  /// Calculate Jacobian and Bxy
  if (result->jacobian())
    throw BoutException("Error in jacobian call");

  // Attempt to read J from the grid file
  Field2D Jcalc = result->J;
  if (mesh_in->get(result->J, "J")) {
    output_warn.write("\tWARNING: Jacobian 'J' not found. Calculating from metric tensor\n");
    result->J = Jcalc;
  } else {
    // Compare calculated and loaded values
    output_warn.write("\tMaximum difference in J is %e\n", max(abs(result->J - Jcalc)));

    // Re-evaluate Bxy using new J
    result->Bxy = sqrt(result->g_22) / result->J;
  }

  // Attempt to read Bxy from the grid file
  Field2D Bcalc = result->Bxy;
  if (mesh_in->get(result->Bxy, "Bxy")) {
    output_warn.write("\tWARNING: Magnitude of B field 'Bxy' not found. Calculating from "
                      "metric tensor\n");
    result->Bxy = Bcalc;
  } else {
    output_warn.write("\tMaximum difference in Bxy is %e\n", max(abs(result->Bxy - Bcalc)));
    // Check Bxy
    if (!finite(result->Bxy)) {
      throw BoutException("\tERROR: Bxy not finite everywhere!\n");
    }
  }

  //////////////////////////////////////////////////////
  /// Calculate Christoffel symbols. Needs communication
  if (result->geometry()) {
    throw BoutException("Differential geometry failed\n");
  }

  if (mesh_in->get(result->ShiftTorsion, "ShiftTorsion")) {
    output_warn.write("\tWARNING: No Torsion specified for zShift. Derivatives may not be correct\n");
    result->ShiftTorsion = 0.0;
  }

  //////////////////////////////////////////////////////

  // Try to read the shift angle from the grid file
  // NOTE: All processors should know the twist-shift angle (for invert_parderiv)
  result->ShiftAngle.resize(mesh_in->LocalNx);
  if (mesh_in->get(result->ShiftAngle, "ShiftAngle", mesh_in->LocalNx, mesh_in->XGLOBAL(0))) {
    output_warn.write("WARNING: Twist-shift angle 'ShiftAngle' not found.");
    result->ShiftAngle.resize(0); // leave ShiftAngle empty
  }

  // try to read zShift from grid
  if(mesh_in->get(result->zShift, "zShift", 0)) {
    // No zShift variable. Try qinty in BOUT grid files
    mesh_in->get(result->zShift, "qinty", 0);
  }
  mesh_in->communicate(result->zShift);

  // don't extrapolate zShift, set guard cells correctly using ShiftAngle
  if (!result->ShiftAngle.empty()) {
    // Correct for discontinuity at branch-cut
    for (int x=0; x<mesh_in->LocalNx; x++) {
      if (mesh_in->hasBranchCutDown(x)) {
        for (int y=0; y<mesh_in->ystart; y++) {
          result->zShift(x, y) -= result->ShiftAngle[x];
        }
      }
      if (mesh_in->hasBranchCutUp(x)) {
        for (int y=mesh_in->yend+1; y<mesh_in->LocalNy; y++) {
          result->zShift(x, y) += result->ShiftAngle[x];
        }
      }
    }
  }

  if (mesh_in->IncIntShear) {
    if (mesh_in->get(result->IntShiftTorsion, "IntShiftTorsion")) {
      output_warn.write("\tWARNING: No Integrated torsion specified\n");
      result->IntShiftTorsion = 0.0;
    }
  }

  return result;
}

std::shared_ptr<Coordinates> Coordinates::getCoordinatesStaggered(Mesh *mesh_in, const CELL_LOC loc, const Coordinates* coords_in) {

  std::shared_ptr<Coordinates> result{new Coordinates(mesh_in)};

  result->location = loc;

  result->dx = interpolateAndExtrapolate(coords_in->dx, result->location, false);
  result->dy = interpolateAndExtrapolate(coords_in->dy, result->location, false);

  result->nz = mesh_in->LocalNz;

  result->dz = coords_in->dz;

  // Diagonal components of metric tensor g^{ij}
  result->g11 = interpolateAndExtrapolate(coords_in->g11, result->location, mesh_in->hasBranchCut());
  result->g22 = interpolateAndExtrapolate(coords_in->g22, result->location, mesh_in->hasBranchCut());
  result->g33 = interpolateAndExtrapolate(coords_in->g33, result->location, mesh_in->hasBranchCut());

  // Off-diagonal elements.
  result->g12 = interpolateAndExtrapolate(coords_in->g12, result->location, mesh_in->hasBranchCut());
  result->g13 = interpolateAndExtrapolate(coords_in->g13, result->location, mesh_in->hasBranchCut());
  result->g23 = interpolateAndExtrapolate(coords_in->g23, result->location, mesh_in->hasBranchCut());

  if (!coords_in->ShiftAngle.empty()) {
    if (result->location == CELL_XLOW) {
      // Need to interpolate ShiftAngle CELL_CENTRE->CELL_XLOW
      result->ShiftAngle.resize(mesh_in->LocalNx);
      stencil s;
      for (int x=mesh_in->xstart; x<=mesh_in->xend; x++) {
        s.mm = coords_in->ShiftAngle[x-2];
        s.m = coords_in->ShiftAngle[x-1];
        s.p = coords_in->ShiftAngle[x];
        s.pp = coords_in->ShiftAngle[x+1];
        result->ShiftAngle[x] = interp(s);
      }
    } else {
      result->ShiftAngle = coords_in->ShiftAngle;
    }
  }

  // don't extrapolate zShift, set guard cells correctly using ShiftAngle
  result->zShift = interpolateAndExtrapolate(coords_in->zShift, result->location, false);
  mesh_in->communicate(result->zShift);
  if (!result->ShiftAngle.empty()) {
    // Correct for discontinuity at branch-cut
    for (int x=0; x<mesh_in->LocalNx; x++) {
      if (mesh_in->hasBranchCutDown(x)) {
        for (int y=0; y<mesh_in->ystart; y++) {
          result->zShift(x, y) -= result->ShiftAngle[x];
        }
      }
      if (mesh_in->hasBranchCutUp(x)) {
        for (int y=mesh_in->yend+1; y<mesh_in->LocalNy; y++) {
          result->zShift(x, y) += result->ShiftAngle[x];
        }
      }
    }
  }

  // Check input metrics
  if ((!finite(result->g11, RGN_NOBNDRY)) || (!finite(result->g22, RGN_NOBNDRY)) || (!finite(result->g33, RGN_NOBNDRY))) {
    throw BoutException("\tERROR: Interpolated diagonal metrics are not finite!\n");
  }
  if ((min(result->g11) <= 0.0) || (min(result->g22) <= 0.0) || (min(result->g33) <= 0.0)) {
    throw BoutException("\tERROR: Interpolated diagonal metrics are negative!\n");
  }
  if ((!finite(result->g12, RGN_NOBNDRY)) || (!finite(result->g13, RGN_NOBNDRY)) || (!finite(result->g23, RGN_NOBNDRY))) {
    throw BoutException("\tERROR: Interpolated off-diagonal metrics are not finite!\n");
  }

  /// Always calculate contravariant metric components so that they are
  /// consistent with the interpolated covariant components
  if (result->calcCovariant()) {
    throw BoutException("Error in calcCovariant call");
  }

  /// Calculate Jacobian and Bxy
  if (result->jacobian())
    throw BoutException("Error in jacobian call");

  //////////////////////////////////////////////////////
  /// Calculate Christoffel symbols. Needs communication
  if (result->geometry()) {
    throw BoutException("Differential geometry failed\n");
  }

  result->ShiftTorsion = interpolateAndExtrapolate(coords_in->ShiftTorsion, result->location, false);

  //////////////////////////////////////////////////////

  if (mesh_in->IncIntShear) {
    result->IntShiftTorsion = interpolateAndExtrapolate(coords_in->IntShiftTorsion, result->location, true);
  }

  return result;
}

std::shared_ptr<Coordinates> Coordinates::getCoordinatesXYCorner(Mesh *mesh_in) {

  std::shared_ptr<Coordinates> result{new Coordinates(mesh_in)};

  Coordinates* coords_xlow = mesh_in->getCoordinates(CELL_XLOW);

  // hack with setLocation because interp_to doesn't know how to interpolate to
  // XYCorner, so we pretend that the CELL_XLOW field is at CELL_CENTRE and
  // interpolate to CELL_YLOW. Finally set location to CELL_CENTRE; these
  // fields will be used with staggered fields (CELL_XLOW, CELL_YLOW) but
  // should only be used pointwise (i.e. through operator()); we want
  // Field2D/Field3D operations to fail, but there is no 'null' CELL_LOC; using
  // CELL_CENTRE seems OK for now.
  // Also note that interpolateAndExtrapolate sets the upper/outer boundary
  // value (which interp_to skips) by interpolating
  result->dx = interpXLOWToXYCorner(coords_xlow->dx, false);
  result->dy = interpXLOWToXYCorner(coords_xlow->dy, false);

  result->nz = mesh_in->LocalNz;

  result->dz = coords_xlow->dz;

  // Diagonal components of metric tensor g^{ij}
  result->g11 = interpXLOWToXYCorner(coords_xlow->g11, mesh_in->hasBranchCut());
  result->g22 = interpXLOWToXYCorner(coords_xlow->g22, mesh_in->hasBranchCut());
  result->g33 = interpXLOWToXYCorner(coords_xlow->g33, mesh_in->hasBranchCut());

  // Off-diagonal elements.
  result->g12 = interpXLOWToXYCorner(coords_xlow->g12, mesh_in->hasBranchCut());
  result->g13 = interpXLOWToXYCorner(coords_xlow->g13, mesh_in->hasBranchCut());
  result->g23 = interpXLOWToXYCorner(coords_xlow->g23, mesh_in->hasBranchCut());

  result->ShiftAngle = coords_xlow->ShiftAngle;

  // don't extrapolate zShift, set guard cells correctly using ShiftAngle
  result->zShift = interpXLOWToXYCorner(coords_xlow->zShift, false);
  mesh_in->communicate(result->zShift);
  // Correct for discontinuity at branch-cut
  if (!result->ShiftAngle.empty()) {
    for (int x=0; x<mesh_in->LocalNx; x++) {
      if (mesh_in->hasBranchCutDown(x)) {
        for (int y=0; y<mesh_in->ystart; y++) {
          result->zShift(x, y) -= result->ShiftAngle[x];
        }
      }
      if (mesh_in->hasBranchCutUp(x)) {
        for (int y=mesh_in->yend+1; y<mesh_in->LocalNy; y++) {
          result->zShift(x, y) += result->ShiftAngle[x];
        }
      }
    }
  }

  // Check input metrics
  if ((!finite(result->g11, RGN_NOBNDRY)) || (!finite(result->g22, RGN_NOBNDRY)) || (!finite(result->g33, RGN_NOBNDRY))) {
    throw BoutException("\tERROR: Interpolated diagonal metrics are not finite!\n");
  }
  if ((min(result->g11) <= 0.0) || (min(result->g22) <= 0.0) || (min(result->g33) <= 0.0)) {
    throw BoutException("\tERROR: Interpolated diagonal metrics are negative!\n");
  }
  if ((!finite(result->g12, RGN_NOBNDRY)) || (!finite(result->g13, RGN_NOBNDRY)) || (!finite(result->g23, RGN_NOBNDRY))) {
    throw BoutException("\tERROR: Interpolated off-diagonal metrics are not finite!\n");
  }

  /// Always calculate contravariant metric components so that they are
  /// consistent with the interpolated covariant components
  if (result->calcCovariant()) {
    throw BoutException("Error in calcCovariant call");
  }

  /// Calculate Jacobian and Bxy
  if (result->jacobian())
    throw BoutException("Error in jacobian call");

  //////////////////////////////////////////////////////
  /// Calculate Christoffel symbols. Needs communication
  if (result->geometry()) {
    throw BoutException("Differential geometry failed\n");
  }

  result->ShiftTorsion = interpXLOWToXYCorner(coords_xlow->ShiftTorsion, mesh_in->hasBranchCut());

  //////////////////////////////////////////////////////

  if (mesh_in->IncIntShear) {
    result->IntShiftTorsion = interpXLOWToXYCorner(coords_xlow->IntShiftTorsion, false);
  }

  return result;
}

void Coordinates::outputVars(Datafile &file) {
  file.add(dx, "dx", false);
  file.add(dy, "dy", false);
  file.add(dz, "dz", false);
  file.add(d1_dx, "d1_dx", false);
  file.add(d1_dy, "d1_dy", false);

  file.add(g11, "g11", false);
  file.add(g22, "g22", false);
  file.add(g33, "g33", false);
  file.add(g12, "g12", false);
  file.add(g13, "g13", false);
  file.add(g23, "g23", false);

  file.add(g_11, "g_11", false);
  file.add(g_22, "g_22", false);
  file.add(g_33, "g_33", false);
  file.add(g_12, "g_12", false);
  file.add(g_13, "g_13", false);
  file.add(g_23, "g_23", false);

  file.add(G1_11, "G1_11", false);
  file.add(G1_22, "G1_22", false);
  file.add(G1_33, "G1_33", false);
  file.add(G1_12, "G1_12", false);
  file.add(G1_13, "G1_13", false);
  file.add(G1_23, "G1_23", false);
  file.add(G2_11, "G2_11", false);
  file.add(G2_22, "G2_22", false);
  file.add(G2_33, "G2_33", false);
  file.add(G2_12, "G2_12", false);
  file.add(G2_13, "G2_13", false);
  file.add(G2_23, "G2_23", false);
  file.add(G3_11, "G3_11", false);
  file.add(G3_22, "G3_22", false);
  file.add(G3_33, "G3_33", false);
  file.add(G3_12, "G3_12", false);
  file.add(G3_13, "G3_13", false);
  file.add(G3_23, "G3_23", false);

  file.add(G1, "G1", false);
  file.add(G2, "G2", false);
  file.add(G3, "G3", false);

  file.add(J, "J", false);
  file.add(Bxy, "Bxy", false);

  file.add(zShift, "zShift", false);

  file.add(ShiftTorsion, "ShiftTorsion", false);
  file.add(IntShiftTorsion, "IntShiftTorsion", false);
}

int Coordinates::geometry() {
  TRACE("Coordinates::geometry");

  output_progress.write("Calculating differential geometry terms\n");

  if (min(abs(dx)) < 1e-8)
    throw BoutException("dx magnitude less than 1e-8");

  if (min(abs(dy)) < 1e-8)
    throw BoutException("dy magnitude less than 1e-8");

  if (fabs(dz) < 1e-8)
    throw BoutException("dz magnitude less than 1e-8");

  // Check input metrics
  if ((!finite(g11, RGN_NOBNDRY)) || (!finite(g22, RGN_NOBNDRY)) || (!finite(g33, RGN_NOBNDRY))) {
    throw BoutException("\tERROR: Diagonal metrics are not finite!\n");
  }
  if ((min(g11) <= 0.0) || (min(g22) <= 0.0) || (min(g33) <= 0.0)) {
    throw BoutException("\tERROR: Diagonal metrics are negative!\n");
  }
  if ((!finite(g12, RGN_NOBNDRY)) || (!finite(g13, RGN_NOBNDRY)) || (!finite(g23, RGN_NOBNDRY))) {
    throw BoutException("\tERROR: Off-diagonal metrics are not finite!\n");
  }

  if ((!finite(g_11, RGN_NOBNDRY)) || (!finite(g_22, RGN_NOBNDRY)) || (!finite(g_33, RGN_NOBNDRY))) {
    throw BoutException("\tERROR: Diagonal g_ij metrics are not finite!\n");
  }
  if ((min(g_11) <= 0.0) || (min(g_22) <= 0.0) || (min(g_33) <= 0.0)) {
    throw BoutException("\tERROR: Diagonal g_ij metrics are negative!\n");
  }
  if ((!finite(g_12, RGN_NOBNDRY)) || (!finite(g_13, RGN_NOBNDRY)) || (!finite(g_23, RGN_NOBNDRY))) {
    throw BoutException("\tERROR: Off-diagonal g_ij metrics are not finite!\n");
  }

  // Calculate Christoffel symbol terms (18 independent values)
  // Note: This calculation is completely general: metric
  // tensor can be 2D or 3D. For 2D, all DDZ terms are zero

  G1_11 = 0.5 * g11 * DDX(g_11) + g12 * (DDX(g_12) - 0.5 * DDY(g_11)) +
          g13 * (DDX(g_13) - 0.5 * DDZ(g_11));
  G1_22 = g11 * (DDY(g_12) - 0.5 * DDX(g_22)) + 0.5 * g12 * DDY(g_22) +
          g13 * (DDY(g_23) - 0.5 * DDZ(g_22));
  G1_33 = g11 * (DDZ(g_13) - 0.5 * DDX(g_33)) + g12 * (DDZ(g_23) - 0.5 * DDY(g_33)) +
          0.5 * g13 * DDZ(g_33);
  G1_12 = 0.5 * g11 * DDY(g_11) + 0.5 * g12 * DDX(g_22) +
          0.5 * g13 * (DDY(g_13) + DDX(g_23) - DDZ(g_12));
  G1_13 = 0.5 * g11 * DDZ(g_11) + 0.5 * g12 * (DDZ(g_12) + DDX(g_23) - DDY(g_13)) +
          0.5 * g13 * DDX(g_33);
  G1_23 = 0.5 * g11 * (DDZ(g_12) + DDY(g_13) - DDX(g_23)) +
          0.5 * g12 * (DDZ(g_22) + DDY(g_23) - DDY(g_23))
          // + 0.5 *g13*(DDZ(g_32) + DDY(g_33) - DDZ(g_23));
          // which equals
          + 0.5 * g13 * DDY(g_33);

  G2_11 = 0.5 * g12 * DDX(g_11) + g22 * (DDX(g_12) - 0.5 * DDY(g_11)) +
          g23 * (DDX(g_13) - 0.5 * DDZ(g_11));
  G2_22 = g12 * (DDY(g_12) - 0.5 * DDX(g_22)) + 0.5 * g22 * DDY(g_22) +
          g23 * (DDY(g23) - 0.5 * DDZ(g_22));
  G2_33 = g12 * (DDZ(g_13) - 0.5 * DDX(g_33)) + g22 * (DDZ(g_23) - 0.5 * DDY(g_33)) +
          0.5 * g23 * DDZ(g_33);
  G2_12 = 0.5 * g12 * DDY(g_11) + 0.5 * g22 * DDX(g_22) +
          0.5 * g23 * (DDY(g_13) + DDX(g_23) - DDZ(g_12));
  G2_13 =
      // 0.5 *g21*(DDZ(g_11) + DDX(g_13) - DDX(g_13))
      // which equals
      0.5 * g12 * (DDZ(g_11) + DDX(g_13) - DDX(g_13))
      // + 0.5 *g22*(DDZ(g_21) + DDX(g_23) - DDY(g_13))
      // which equals
      + 0.5 * g22 * (DDZ(g_12) + DDX(g_23) - DDY(g_13))
      // + 0.5 *g23*(DDZ(g_31) + DDX(g_33) - DDZ(g_13));
      // which equals
      + 0.5 * g23 * DDX(g_33);
  G2_23 = 0.5 * g12 * (DDZ(g_12) + DDY(g_13) - DDX(g_23)) + 0.5 * g22 * DDZ(g_22) +
          0.5 * g23 * DDY(g_33);

  G3_11 = 0.5 * g13 * DDX(g_11) + g23 * (DDX(g_12) - 0.5 * DDY(g_11)) +
          g33 * (DDX(g_13) - 0.5 * DDZ(g_11));
  G3_22 = g13 * (DDY(g_12) - 0.5 * DDX(g_22)) + 0.5 * g23 * DDY(g_22) +
          g33 * (DDY(g_23) - 0.5 * DDZ(g_22));
  G3_33 = g13 * (DDZ(g_13) - 0.5 * DDX(g_33)) + g23 * (DDZ(g_23) - 0.5 * DDY(g_33)) +
          0.5 * g33 * DDZ(g_33);
  G3_12 =
      // 0.5 *g31*(DDY(g_11) + DDX(g_12) - DDX(g_12))
      // which equals to
      0.5 * g13 * DDY(g_11)
      // + 0.5 *g32*(DDY(g_21) + DDX(g_22) - DDY(g_12))
      // which equals to
      + 0.5 * g23 * DDX(g_22)
      //+ 0.5 *g33*(DDY(g_31) + DDX(g_32) - DDZ(g_12));
      // which equals to
      + 0.5 * g33 * (DDY(g_13) + DDX(g_23) - DDZ(g_12));
  G3_13 = 0.5 * g13 * DDZ(g_11) + 0.5 * g23 * (DDZ(g_12) + DDX(g_23) - DDY(g_13)) +
          0.5 * g33 * DDX(g_33);
  G3_23 = 0.5 * g13 * (DDZ(g_12) + DDY(g_13) - DDX(g_23)) + 0.5 * g23 * DDZ(g_22) +
          0.5 * g33 * DDY(g_33);

  G1 = (DDX(J * g11) + DDY(J * g12) + DDZ(J * g13)) / J;
  G2 = (DDX(J * g12) + DDY(J * g22) + DDZ(J * g23)) / J;
  G3 = (DDX(J * g13) + DDY(J * g23) + DDZ(J * g33)) / J;

  // Communicate christoffel symbol terms
  output_progress.write("\tCommunicating connection terms\n");

  FieldGroup com;

  com.add(G1_11);
  com.add(G1_22);
  com.add(G1_33);
  com.add(G1_12);
  com.add(G1_13);
  com.add(G1_23);

  com.add(G2_11);
  com.add(G2_22);
  com.add(G2_33);
  com.add(G2_12);
  com.add(G2_13);
  com.add(G2_23);

  com.add(G3_11);
  com.add(G3_22);
  com.add(G3_33);
  com.add(G3_12);
  com.add(G3_13);
  com.add(G3_23);

  com.add(G1);
  com.add(G2);
  com.add(G3);

  localmesh->communicate(com);

  //////////////////////////////////////////////////////
  /// Non-uniform meshes. Need to use DDX, DDY

  OPTION(Options::getRoot(), non_uniform, true);

  Field2D d2x, d2y; // d^2 x / d i^2
  // Read correction for non-uniform meshes
  if (localmesh->get(d2x, "d2x")) {
    output_warn.write(
        "\tWARNING: differencing quantity 'd2x' not found. Calculating from dx\n");
    d1_dx = localmesh->indexDDX(1. / dx); // d/di(1/dx)
  } else {
    d1_dx = -d2x / (dx * dx);
  }

  if (localmesh->get(d2y, "d2y")) {
    output_warn.write(
        "\tWARNING: differencing quantity 'd2y' not found. Calculating from dy\n");
    d1_dy = localmesh->indexDDY(1. / dy); // d/di(1/dy)
  } else {
    d1_dy = -d2y / (dy * dy);
  }

  return 0;
}

int Coordinates::calcCovariant() {
  TRACE("Coordinates::calcCovariant");

  // Make sure metric elements are allocated
  g_11.allocate();
  g_22.allocate();
  g_33.allocate();
  g_12.allocate();
  g_13.allocate();
  g_23.allocate();

  g_11.setLocation(location);
  g_22.setLocation(location);
  g_33.setLocation(location);
  g_12.setLocation(location);
  g_13.setLocation(location);
  g_23.setLocation(location);

  // Perform inversion of g^{ij} to get g_{ij}
  // NOTE: Currently this bit assumes that metric terms are Field2D objects

  auto a = Matrix<BoutReal>(3, 3);

  for (int jx = 0; jx < localmesh->LocalNx; jx++) {
    for (int jy = 0; jy < localmesh->LocalNy; jy++) {
      // set elements of g
      a(0, 0) = g11(jx, jy);
      a(1, 1) = g22(jx, jy);
      a(2, 2) = g33(jx, jy);

      a(0, 1) = a(1, 0) = g12(jx, jy);
      a(1, 2) = a(2, 1) = g23(jx, jy);
      a(0, 2) = a(2, 0) = g13(jx, jy);

      // invert
      if (invert3x3(a)) {
        output_error.write("\tERROR: metric tensor is singular at (%d, %d)\n", jx, jy);
        return 1;
      }

      // put elements into g_{ij}
      g_11(jx, jy) = a(0, 0);
      g_22(jx, jy) = a(1, 1);
      g_33(jx, jy) = a(2, 2);

      g_12(jx, jy) = a(0, 1);
      g_13(jx, jy) = a(0, 2);
      g_23(jx, jy) = a(1, 2);
    }
  }

  BoutReal maxerr;
  maxerr = BOUTMAX(max(abs((g_11 * g11 + g_12 * g12 + g_13 * g13) - 1)),
                   max(abs((g_12 * g12 + g_22 * g22 + g_23 * g23) - 1)),
                   max(abs((g_13 * g13 + g_23 * g23 + g_33 * g33) - 1)));

  output_info.write("\tLocal maximum error in diagonal inversion is %e\n", maxerr);

  maxerr = BOUTMAX(max(abs(g_11 * g12 + g_12 * g22 + g_13 * g23)),
                   max(abs(g_11 * g13 + g_12 * g23 + g_13 * g33)),
                   max(abs(g_12 * g13 + g_22 * g23 + g_23 * g33)));

  output_info.write("\tLocal maximum error in off-diagonal inversion is %e\n", maxerr);

  return 0;
}

int Coordinates::calcContravariant() {
  TRACE("Coordinates::calcContravariant");

  // Make sure metric elements are allocated
  g11.allocate();
  g22.allocate();
  g33.allocate();
  g12.allocate();
  g13.allocate();
  g23.allocate();

  // Perform inversion of g_{ij} to get g^{ij}
  // NOTE: Currently this bit assumes that metric terms are Field2D objects

  auto a = Matrix<BoutReal>(3, 3);

  for (int jx = 0; jx < localmesh->LocalNx; jx++) {
    for (int jy = 0; jy < localmesh->LocalNy; jy++) {
      // set elements of g
      a(0, 0) = g_11(jx, jy);
      a(1, 1) = g_22(jx, jy);
      a(2, 2) = g_33(jx, jy);

      a(0, 1) = a(1, 0) = g_12(jx, jy);
      a(1, 2) = a(2, 1) = g_23(jx, jy);
      a(0, 2) = a(2, 0) = g_13(jx, jy);

      // invert
      if (invert3x3(a)) {
        output_error.write("\tERROR: metric tensor is singular at (%d, %d)\n", jx, jy);
        return 1;
      }

      // put elements into g_{ij}
      g11(jx, jy) = a(0, 0);
      g22(jx, jy) = a(1, 1);
      g33(jx, jy) = a(2, 2);

      g12(jx, jy) = a(0, 1);
      g13(jx, jy) = a(0, 2);
      g23(jx, jy) = a(1, 2);
    }
  }

  BoutReal maxerr;
  maxerr = BOUTMAX(max(abs((g_11 * g11 + g_12 * g12 + g_13 * g13) - 1)),
                   max(abs((g_12 * g12 + g_22 * g22 + g_23 * g23) - 1)),
                   max(abs((g_13 * g13 + g_23 * g23 + g_33 * g33) - 1)));

  output_info.write("\tMaximum error in diagonal inversion is %e\n", maxerr);

  maxerr = BOUTMAX(max(abs(g_11 * g12 + g_12 * g22 + g_13 * g23)),
                   max(abs(g_11 * g13 + g_12 * g23 + g_13 * g33)),
                   max(abs(g_12 * g13 + g_22 * g23 + g_23 * g33)));

  output_info.write("\tMaximum error in off-diagonal inversion is %e\n", maxerr);
  return 0;
}

int Coordinates::jacobian() {
  TRACE("Coordinates::jacobian");
  // calculate Jacobian using g^-1 = det[g^ij], J = sqrt(g)

  Field2D g = g11 * g22 * g33 + 2.0 * g12 * g13 * g23 - g11 * g23 * g23 -
              g22 * g13 * g13 - g33 * g12 * g12;

  // Check that g is positive
  if (min(g) < 0.0) {
    throw BoutException("The determinant of g^ij is somewhere less than 0.0");
  }
  J = 1. / sqrt(g);

  // Check jacobian
  if (!finite(J, RGN_NOBNDRY)) {
    throw BoutException("\tERROR: Jacobian not finite everywhere!\n");
  }
  if (min(abs(J)) < 1.0e-10) {
    throw BoutException("\tERROR: Jacobian becomes very small\n");
  }

  if (min(g_22) < 0.0) {
    throw BoutException("g_22 is somewhere less than 0.0");
  }
  Bxy = sqrt(g_22) / J;

  return 0;
}

/*******************************************************************************
 * Operators
 *
 *******************************************************************************/

const Field2D Coordinates::DDX(const Field2D &f, CELL_LOC loc, DIFF_METHOD method, REGION region) {
  ASSERT1(location == loc || loc == CELL_DEFAULT);
  return localmesh->indexDDX(f, loc, method, region) / dx;
}

const Field2D Coordinates::DDY(const Field2D &f, CELL_LOC loc, DIFF_METHOD method, REGION region) {
  ASSERT1(location == loc || loc == CELL_DEFAULT);
  return localmesh->indexDDY(f, loc, method, region) / dy;
}

const Field2D Coordinates::DDZ(const Field2D &f, CELL_LOC loc,
                               DIFF_METHOD UNUSED(method), REGION UNUSED(region)) {
  ASSERT1(location == loc || loc == CELL_DEFAULT);
  ASSERT1(f.getMesh() == localmesh);
  auto result = Field2D(0.0, localmesh);
  result.setLocation(location);
  return result;
}

#include <derivs.hxx>

/////////////////////////////////////////////////////////
// Parallel gradient

const Field2D Coordinates::Grad_par(const Field2D &var, CELL_LOC outloc,
                                    DIFF_METHOD method) {
  TRACE("Coordinates::Grad_par( Field2D )");
  ASSERT1(location == outloc || (outloc == CELL_DEFAULT && location == var.getLocation()));

  return DDY(var, outloc, method) / sqrt(g_22);
}

const Field3D Coordinates::Grad_par(const Field3D &var, CELL_LOC outloc,
                                    DIFF_METHOD method) {
  TRACE("Coordinates::Grad_par( Field3D )");
  ASSERT1(location == outloc || outloc == CELL_DEFAULT);

  return ::DDY(var, outloc, method) / sqrt(g_22);
}

/////////////////////////////////////////////////////////
// Vpar_Grad_par
// vparallel times the parallel derivative along unperturbed B-field

const Field2D Coordinates::Vpar_Grad_par(const Field2D &v, const Field2D &f,
                                         CELL_LOC outloc,
                                         DIFF_METHOD method) {
  ASSERT1(location == outloc || (outloc == CELL_DEFAULT && location == f.getLocation()));
  return VDDY(v, f, outloc, method) / sqrt(g_22);
}

const Field3D Coordinates::Vpar_Grad_par(const Field3D &v, const Field3D &f, CELL_LOC outloc,
                                         DIFF_METHOD method) {
  ASSERT1(location == outloc || outloc == CELL_DEFAULT);
  return VDDY(v, f, outloc, method) / sqrt(g_22);
}

/////////////////////////////////////////////////////////
// Parallel divergence

const Field2D Coordinates::Div_par(const Field2D &f, CELL_LOC outloc,
                                   DIFF_METHOD method) {
  TRACE("Coordinates::Div_par( Field2D )");
  ASSERT1(location == outloc || outloc == CELL_DEFAULT);

  // Need Bxy at location of f, which might be different from location of this
  // Coordinates object
  Field2D Bxy_floc = f.getCoordinates()->Bxy;

  return Bxy * Grad_par(f / Bxy_floc, outloc, method);
}

const Field3D Coordinates::Div_par(const Field3D &f, CELL_LOC outloc,
                                   DIFF_METHOD method) {
  TRACE("Coordinates::Div_par( Field3D )");
  ASSERT1(location == outloc || outloc == CELL_DEFAULT);
  
  // Need Bxy at location of f, which might be different from location of this
  // Coordinates object
  Field2D Bxy_floc = f.getCoordinates()->Bxy;

  if (!f.hasYupYdown()) {
    // No yup/ydown fields. The Grad_par operator will
    // shift to field aligned coordinates
    return Bxy * Grad_par(f / Bxy_floc, outloc, method);
  }

  // Need to modify yup and ydown fields
  Field3D f_B = f / Bxy_floc;
  if (&f.yup() == &f) {
    // Identity, yup and ydown point to same field
    f_B.mergeYupYdown();
  } else {
    // Distinct fields
    f_B.splitYupYdown();
    f_B.yup() = f.yup() / Bxy_floc;
    f_B.ydown() = f.ydown() / Bxy_floc;
  }
  return Bxy * Grad_par(f_B, outloc, method);
}

/////////////////////////////////////////////////////////
// second parallel derivative (b dot Grad)(b dot Grad)
// Note: For parallel Laplacian use Laplace_par

const Field2D Coordinates::Grad2_par2(const Field2D &f, CELL_LOC outloc, DIFF_METHOD method) {
  TRACE("Coordinates::Grad2_par2( Field2D )");
  ASSERT1(location == outloc || (outloc == CELL_DEFAULT && location == f.getLocation()));

  Field2D sg = sqrt(g_22);
  Field2D result = DDY(1. / sg, outloc, method) * DDY(f, outloc, method) / sg + D2DY2(f, outloc, method) / g_22;

  return result;
}

const Field3D Coordinates::Grad2_par2(const Field3D &f, CELL_LOC outloc, DIFF_METHOD method) {
  TRACE("Coordinates::Grad2_par2( Field3D )");
  if (outloc == CELL_DEFAULT) {
    outloc = f.getLocation();
  }
  ASSERT1(location == outloc);

  Field2D sg(localmesh);
  Field3D result(localmesh), r2(localmesh);

  sg = sqrt(g_22);
  sg = DDY(1. / sg, outloc, method) / sg;


  result = ::DDY(f, outloc, method);

  r2 = D2DY2(f, outloc, method) / g_22;

  result = sg * result + r2;

  ASSERT2(result.getLocation() == outloc);

  return result;
}

/////////////////////////////////////////////////////////
// perpendicular Laplacian operator

#include <invert_laplace.hxx> // Delp2 uses same coefficients as inversion code

const Field2D Coordinates::Delp2(const Field2D &f, CELL_LOC outloc) {
  TRACE("Coordinates::Delp2( Field2D )");
  ASSERT1(location == outloc || outloc == CELL_DEFAULT);

  Field2D result = G1 * DDX(f, outloc) + g11 * D2DX2(f, outloc);

  return result;
}

const Field3D Coordinates::Delp2(const Field3D &f, CELL_LOC outloc) {
  TRACE("Coordinates::Delp2( Field3D )");
  if (outloc == CELL_DEFAULT) {
    outloc = f.getLocation();
  }
  ASSERT1(location == outloc);

  if (localmesh->GlobalNx == 1 && localmesh->GlobalNz == 1) {
    // copy mesh, location, etc
    return f*0;
  }
  ASSERT2(localmesh->xstart > 0); // Need at least one guard cell

  ASSERT2(f.getLocation() == outloc);

  Field3D result(localmesh);
  result.allocate();
  result.setLocation(f.getLocation());

  int ncz = localmesh->LocalNz;

  // Allocate memory
  auto ft = Matrix<dcomplex>(localmesh->LocalNx, ncz / 2 + 1);
  auto delft = Matrix<dcomplex>(localmesh->LocalNx, ncz / 2 + 1);

  // Loop over all y indices
  for (int jy = 0; jy < localmesh->LocalNy; jy++) {

    // Take forward FFT

    for (int jx = 0; jx < localmesh->LocalNx; jx++)
      rfft(&f(jx, jy, 0), ncz, &ft(jx, 0));

    // Loop over kz
    for (int jz = 0; jz <= ncz / 2; jz++) {
      dcomplex a, b, c;

      // No smoothing in the x direction
      for (int jx = localmesh->xstart; jx <= localmesh->xend; jx++) {
        // Perform x derivative

        laplace_tridag_coefs(jx, jy, jz, a, b, c, nullptr, nullptr, outloc);

        delft(jx, jz) = a * ft(jx - 1, jz) + b * ft(jx, jz) + c * ft(jx + 1, jz);
      }
    }

    // Reverse FFT
    for (int jx = localmesh->xstart; jx <= localmesh->xend; jx++) {

      irfft(&delft(jx, 0), ncz, &result(jx, jy, 0));
    }

    // Boundaries
    for (int jz = 0; jz < ncz; jz++) {
      for (int jx = 0; jx < localmesh->xstart; jx++) {
        result(jx, jy, jz) = 0.0;
      }
      for (int jx = localmesh->xend + 1; jx < localmesh->LocalNx; jx++) {
        result(jx, jy, jz) = 0.0;
      }
    }
  }

  ASSERT2(result.getLocation() == f.getLocation());

  return result;
}

const FieldPerp Coordinates::Delp2(const FieldPerp &f, CELL_LOC outloc) {
  TRACE("Coordinates::Delp2( FieldPerp )");

  if (outloc == CELL_DEFAULT) outloc = f.getLocation();

  ASSERT1(location == outloc);
  ASSERT2(f.getLocation() == outloc);

  FieldPerp result(localmesh);
  result.allocate();
  result.setLocation(outloc);

  int jy = f.getIndex();
  result.setIndex(jy);

  int ncz = localmesh->LocalNz;

  // Allocate memory
  auto ft = Matrix<dcomplex>(localmesh->LocalNx, ncz / 2 + 1);
  auto delft = Matrix<dcomplex>(localmesh->LocalNx, ncz / 2 + 1);

  // Take forward FFT
  for (int jx = 0; jx < localmesh->LocalNx; jx++)
    rfft(&f(jx, 0), ncz, &ft(jx, 0));

  // Loop over kz
  for (int jz = 0; jz <= ncz / 2; jz++) {

    // No smoothing in the x direction
    for (int jx = 2; jx < (localmesh->LocalNx - 2); jx++) {
      // Perform x derivative

      dcomplex a, b, c;
      laplace_tridag_coefs(jx, jy, jz, a, b, c);

      delft(jx, jz) = a * ft(jx - 1, jz) + b * ft(jx, jz) + c * ft(jx + 1, jz);
    }
  }

  // Reverse FFT
  for (int jx = 1; jx < (localmesh->LocalNx - 1); jx++) {
    irfft(&delft(jx, 0), ncz, &result(jx, 0));
  }

  // Boundaries
  for (int jz = 0; jz < ncz; jz++) {
    result(0, jz) = 0.0;
    result(localmesh->LocalNx - 1, jz) = 0.0;
  }

  return result;
}

const Field2D Coordinates::Laplace_par(const Field2D &f, CELL_LOC outloc) {
  ASSERT1(location == outloc || outloc == CELL_DEFAULT);
  return D2DY2(f, outloc) / g_22 + DDY(J / g_22, outloc) * DDY(f, outloc) / J;
}

const Field3D Coordinates::Laplace_par(const Field3D &f, CELL_LOC outloc) {
  ASSERT1(location == outloc || outloc == CELL_DEFAULT);
  return D2DY2(f, outloc) / g_22 + DDY(J / g_22, outloc) * ::DDY(f, outloc) / J;
}

// Full Laplacian operator on scalar field

const Field2D Coordinates::Laplace(const Field2D &f, CELL_LOC outloc) {
  TRACE("Coordinates::Laplace( Field2D )");
  ASSERT1(location == outloc || outloc == CELL_DEFAULT);

  Field2D result =
      G1 * DDX(f, outloc) + G2 * DDY(f, outloc) + g11 * D2DX2(f, outloc)
      + g22 * D2DY2(f, outloc) + g12 * (D2DXDY(f, outloc) + D2DYDX(f, outloc));

  return result;
}

const Field3D Coordinates::Laplace(const Field3D &f, CELL_LOC outloc) {
  TRACE("Coordinates::Laplace( Field3D )");
  ASSERT1(location == outloc || outloc == CELL_DEFAULT);

  Field3D result = G1 * ::DDX(f, outloc) + G2 * ::DDY(f, outloc) + G3 * ::DDZ(f, outloc) + g11 * D2DX2(f, outloc) +
                   g22 * D2DY2(f, outloc) + g33 * D2DZ2(f, outloc) +
                   g12 * (D2DXDY(f, outloc) + D2DYDX(f, outloc))
                   + g13 * (D2DXDZ(f, outloc) + D2DZDX(f, outloc))
                   + g23 * (D2DYDZ(f, outloc) + D2DZDY(f, outloc));

  ASSERT2(result.getLocation() == f.getLocation());

  return result;
}

/*!
 * \file CNSSolver.cpp
 * \brief Main subrotuines for solving Finite-Volume Navier-Stokes flow problems.
 * \author F. Palacios, T. Economon
 * \version 7.0.3 "Blackbird"
 *
 * SU2 Project Website: https://su2code.github.io
 *
 * The SU2 Project is maintained by the SU2 Foundation
 * (http://su2foundation.org)
 *
 * Copyright 2012-2020, SU2 Contributors (cf. AUTHORS.md)
 *
 * SU2 is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * SU2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with SU2. If not, see <http://www.gnu.org/licenses/>.
 */


#include "../../include/solvers/CNSSolver.hpp"
#include "../../include/variables/CNSVariable.hpp"
#include "../../../Common/include/toolboxes/printing_toolbox.hpp"

CNSSolver::CNSSolver(void) : CEulerSolver() { }

CNSSolver::CNSSolver(CGeometry *geometry, CConfig *config, unsigned short iMesh) :
           CEulerSolver(geometry, config, iMesh, true) {

  /*--- This constructor only allocates/inits what is extra to CEulerSolver. ---*/

  unsigned short iMarker, iDim;
  unsigned long iVertex;

  /*--- Store the values of the temperature and the heat flux density at the boundaries,
   used for coupling with a solid donor cell ---*/
  unsigned short nHeatConjugateVar = 4;

  HeatConjugateVar = new su2double** [nMarker];
  for (iMarker = 0; iMarker < nMarker; iMarker++) {
    HeatConjugateVar[iMarker] = new su2double* [nVertex[iMarker]];
    for (iVertex = 0; iVertex < nVertex[iMarker]; iVertex++) {
      HeatConjugateVar[iMarker][iVertex] = new su2double [nHeatConjugateVar]();
      HeatConjugateVar[iMarker][iVertex][0] = config->GetTemperature_FreeStreamND();
    }
  }

  /*--- Allocates a 2D array with variable "outer" sizes and init to 0. ---*/

  auto Alloc2D = [](unsigned long M, const unsigned long* N, su2double**& X) {
    X = new su2double* [M];
    for(unsigned long i = 0; i < M; ++i)
      X[i] = new su2double [N[i]] ();
  };

  /*--- Allocates a 3D array with variable "middle" sizes and init to 0. ---*/

  auto Alloc3D = [](unsigned long M, const unsigned long* N, unsigned long P, su2double***& X) {
    X = new su2double** [M];
    for(unsigned long i = 0; i < M; ++i) {
      X[i] = new su2double* [N[i]];
      for(unsigned long j = 0; j < N[i]; ++j)
        X[i][j] = new su2double [P] ();
    }
  };

  /*--- Heat flux in all the markers ---*/

  Alloc2D(nMarker, nVertex, HeatFlux);
  Alloc2D(nMarker, nVertex, HeatFluxTarget);

  /*--- Y plus in all the markers ---*/

  Alloc2D(nMarker, nVertex, YPlus);

  /*--- Skin friction in all the markers ---*/

  CSkinFriction = new su2double** [nMarker];
  for (iMarker = 0; iMarker < nMarker; iMarker++) {
    CSkinFriction[iMarker] = new su2double*[nDim];
    for (iDim = 0; iDim < nDim; iDim++) {
      CSkinFriction[iMarker][iDim] = new su2double[nVertex[iMarker]] ();
    }
  }

  /*--- Non dimensional aerodynamic coefficients ---*/

  ViscCoeff.allocate(nMarker);
  SurfaceViscCoeff.allocate(config->GetnMarker_Monitoring());

  /*--- Heat flux and buffet coefficients ---*/

  HF_Visc = new su2double[nMarker];
  MaxHF_Visc = new su2double[nMarker];

  Surface_HF_Visc = new su2double[config->GetnMarker_Monitoring()];
  Surface_MaxHF_Visc = new su2double[config->GetnMarker_Monitoring()];

  /*--- Buffet sensor in all the markers and coefficients ---*/

  if(config->GetBuffet_Monitoring() || config->GetKind_ObjFunc() == BUFFET_SENSOR){

    Alloc2D(nMarker, nVertex, Buffet_Sensor);
    Buffet_Metric = new su2double[nMarker];
    Surface_Buffet_Metric = new su2double[config->GetnMarker_Monitoring()];

  }

  /*--- Read farfield conditions from config ---*/

  Viscosity_Inf   = config->GetViscosity_FreeStreamND();
  Prandtl_Lam     = config->GetPrandtl_Lam();
  Prandtl_Turb    = config->GetPrandtl_Turb();
  Tke_Inf         = config->GetTke_FreeStreamND();


  /*--- Set the SGS model in case an LES simulation is carried out.
   Make a distinction between the SGS models used and set SGSModel and
  SGSModelUsed accordingly. ---*/

  SGSModel = NULL;
  SGSModelUsed = false;

  switch( config->GetKind_SGS_Model() ) {
    /* No LES, so no SGS model needed.
     Set the pointer to NULL and the boolean to false. */
    case NO_SGS_MODEL: case IMPLICIT_LES:
      SGSModel = NULL;
      SGSModelUsed = false;
      break;
    case SMAGORINSKY:
      SGSModel     = new CSmagorinskyModel;
      SGSModelUsed = true;
      break;
    case WALE:
      SGSModel     = new CWALEModel;
      SGSModelUsed = true;
      break;
    case VREMAN:
      SGSModel     = new CVremanModel;
      SGSModelUsed = true;
      break;
    case SIGMA:
      SGSModel     = new CSIGMAModel;
      SGSModelUsed = true;
      break;
    default:
      SU2_MPI::Error("Unknown SGS model encountered", CURRENT_FUNCTION);
  }

  /*--- Set the wall model to NULL ---*/

  WallModel = NULL;

  if (config->GetWall_Models()){

    /*--- Set the WMLES class  ---*/
    /*--- First allocate the auxiliary variables ---*/

    Alloc2D(nMarker, nVertex, TauWall_WMLES);
    Alloc2D(nMarker, nVertex, HeatFlux_WMLES);
    Alloc3D(nMarker, nVertex, nDim, FlowDirTan_WMLES);
    Alloc3D(nMarker, nVertex, nDim, VelTimeFilter_WMLES);

    /*--- Check if the Wall models or Wall functions are unique. ---*/
    /*--- OBS: All the markers must have the same wall model/function ---*/

    vector<unsigned short> WallFunctions_;
    vector<string> WallFunctionsMarker_;
    for(unsigned short iMarker=0; iMarker<nMarker; ++iMarker) {
      switch (config->GetMarker_All_KindBC(iMarker)) {
        case ISOTHERMAL:
        case HEAT_FLUX: {
          string Marker_Tag = config->GetMarker_All_TagBound(iMarker);
          if(config->GetWallFunction_Treatment(Marker_Tag) != NO_WALL_FUNCTION){
            WallFunctions_.push_back(config->GetWallFunction_Treatment(Marker_Tag));
            WallFunctionsMarker_.push_back(Marker_Tag);
          }
          break;
        }
        default:  /* Just to avoid a compiler warning. */
          break;
      }
    }

    if (!WallFunctions_.empty()){
      sort(WallFunctions_.begin(), WallFunctions_.end());
      vector<unsigned short>::iterator it = std::unique( WallFunctions_.begin(), WallFunctions_.end() );
      WallFunctions_.erase(it, WallFunctions_.end());

      if(WallFunctions_.size() == 1) {
        switch (config->GetWallFunction_Treatment(WallFunctionsMarker_[0])) {
          case EQUILIBRIUM_WALL_MODEL:
            WallModel = new CWallModel1DEQ(config,WallFunctionsMarker_[0]);
            break;
          case LOGARITHMIC_WALL_MODEL:
            WallModel = new CWallModelLogLaw(config,WallFunctionsMarker_[0]);
            break;
          case ALGEBRAIC_WALL_MODEL:
            WallModel = new CWallModelAlgebraic(config,WallFunctionsMarker_[0]);
            break;
          case APGLL_WALL_MODEL:
            WallModel = new CWallModelAPGLL(config,WallFunctionsMarker_[0]);
            break;
          case TEMPLATE_WALL_MODEL:
            WallModel = new CWallModelTemplate(config,WallFunctionsMarker_[0]);
          break;

          default:
            break;
        }
      }
      else{
        SU2_MPI::Error("Wall function/model type must be the same in all wall BCs", CURRENT_FUNCTION);
      }
    }
  }

  /*--- Initialize the seed values for forward mode differentiation. ---*/

  switch(config->GetDirectDiff()) {
    case D_VISCOSITY:
      SU2_TYPE::SetDerivative(Viscosity_Inf, 1.0);
      break;
    default:
      /*--- Already done upstream. ---*/
      break;
  }

}

CNSSolver::~CNSSolver(void) {

  unsigned short iMarker, iDim;

  unsigned long iVertex;

  delete [] Buffet_Metric;
  delete [] HF_Visc;
  delete [] MaxHF_Visc;

  delete [] Surface_HF_Visc;
  delete [] Surface_MaxHF_Visc;
  delete [] Surface_Buffet_Metric;

  if (CSkinFriction != NULL) {
    for (iMarker = 0; iMarker < nMarker; iMarker++) {
      for (iDim = 0; iDim < nDim; iDim++) {
        delete [] CSkinFriction[iMarker][iDim];
      }
      delete [] CSkinFriction[iMarker];
    }
    delete [] CSkinFriction;
  }

  if (HeatConjugateVar != NULL) {
    for (iMarker = 0; iMarker < nMarker; iMarker++) {
      for (iVertex = 0; iVertex < nVertex[iMarker]; iVertex++) {
        delete [] HeatConjugateVar[iMarker][iVertex];
      }
      delete [] HeatConjugateVar[iMarker];
    }
    delete [] HeatConjugateVar;
  }

  if (Buffet_Sensor != NULL) {
    for (iMarker = 0; iMarker < nMarker; iMarker++){
      delete [] Buffet_Sensor[iMarker];
    }
    delete [] Buffet_Sensor;
  }

  if( SGSModel  != NULL) delete SGSModel;
  if( WallModel != NULL) delete WallModel;

}

void CNSSolver::Preprocessing(CGeometry *geometry, CSolver **solver_container, CConfig *config, unsigned short iMesh,
                              unsigned short iRKStep, unsigned short RunTime_EqSystem, bool Output) {

  unsigned long InnerIter   = config->GetInnerIter();
  bool cont_adjoint         = config->GetContinuous_Adjoint();
  bool limiter_flow         = (config->GetKind_SlopeLimit_Flow() != NO_LIMITER) && (InnerIter <= config->GetLimiterIter());
  bool limiter_turb         = (config->GetKind_SlopeLimit_Turb() != NO_LIMITER) && (InnerIter <= config->GetLimiterIter());
  bool limiter_adjflow      = (cont_adjoint && (config->GetKind_SlopeLimit_AdjFlow() != NO_LIMITER) && (InnerIter <= config->GetLimiterIter()));
  bool van_albada           = config->GetKind_SlopeLimit_Flow() == VAN_ALBADA_EDGE;
  bool wall_functions       = config->GetWall_Functions();
  bool wall_models          = config->GetWall_Models();

  /*--- Common preprocessing steps (implemented by CEulerSolver) ---*/

  CommonPreprocessing(geometry, solver_container, config, iMesh, iRKStep, RunTime_EqSystem, Output);

  /*--- Compute gradient for MUSCL reconstruction. ---*/

  if (config->GetReconstructionGradientRequired() && (iMesh == MESH_0)) {
    switch (config->GetKind_Gradient_Method_Recon()) {
      case GREEN_GAUSS:
        SetPrimitive_Gradient_GG(geometry, config, true); break;
      case LEAST_SQUARES:
      case WEIGHTED_LEAST_SQUARES:
        SetPrimitive_Gradient_LS(geometry, config, true); break;
      default: break;
    }
  }

  /*--- Compute gradient of the primitive variables ---*/

  if (config->GetKind_Gradient_Method() == GREEN_GAUSS) {
    SetPrimitive_Gradient_GG(geometry, config);
  }
  else if (config->GetKind_Gradient_Method() == WEIGHTED_LEAST_SQUARES) {
    SetPrimitive_Gradient_LS(geometry, config);
  }

  /*--- Compute the limiter in case we need it in the turbulence model or to limit the
   *    viscous terms (check this logic with JST and 2nd order turbulence model) ---*/

  if ((iMesh == MESH_0) && (limiter_flow || limiter_turb || limiter_adjflow) && !Output && !van_albada) {
    SetPrimitive_Limiter(geometry, config);
  }

  /*--- Evaluate the vorticity and strain rate magnitude ---*/

  SU2_OMP_MASTER
  {
    StrainMag_Max = 0.0;
    Omega_Max = 0.0;
  }
  SU2_OMP_BARRIER

  nodes->SetVorticity_StrainMag();

  su2double strainMax = 0.0, omegaMax = 0.0;

  SU2_OMP(for schedule(static,omp_chunk_size) nowait)
  for (unsigned long iPoint = 0; iPoint < nPoint; iPoint++) {

    su2double StrainMag = nodes->GetStrainMag(iPoint);
    const su2double* Vorticity = nodes->GetVorticity(iPoint);
    su2double Omega = sqrt(Vorticity[0]*Vorticity[0]+ Vorticity[1]*Vorticity[1]+ Vorticity[2]*Vorticity[2]);

    strainMax = max(strainMax, StrainMag);
    omegaMax = max(omegaMax, Omega);

  }
  SU2_OMP_CRITICAL
  {
    StrainMag_Max = max(StrainMag_Max, strainMax);
    Omega_Max = max(Omega_Max, omegaMax);
  }

  if ((iMesh == MESH_0) && (config->GetComm_Level() == COMM_FULL)) {
    SU2_OMP_BARRIER
    SU2_OMP_MASTER
    {
      su2double MyOmega_Max = Omega_Max;
      su2double MyStrainMag_Max = StrainMag_Max;

      SU2_MPI::Allreduce(&MyStrainMag_Max, &StrainMag_Max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
      SU2_MPI::Allreduce(&MyOmega_Max, &Omega_Max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
    }
    SU2_OMP_BARRIER
  }

  /*--- Calculate the eddy viscosity using a SGS model ---*/

  if (SGSModelUsed){
    SU2_OMP_MASTER
    Setmut_LES(geometry, solver_container, config);
    SU2_OMP_BARRIER
  }

  /*--- Compute the wall shear stress from the wall functions ---*/

  if (wall_functions) {
    SU2_OMP_MASTER
    SetTauWall_WF(geometry, solver_container, config);
    SetEddyViscFirstPoint(geometry, solver_container, config);
    SU2_OMP_BARRIER
  }

  /*--- Compute the wall shear stress from the wall model ---*/

  if (wall_models && (iRKStep==0) && (iMesh == MESH_0)){
    SU2_OMP_MASTER
    SetTauWallHeatFlux_WMLES1stPoint(geometry, solver_container, config, iRKStep);
    SU2_OMP_BARRIER
  }

}

unsigned long CNSSolver::SetPrimitive_Variables(CSolver **solver_container, CConfig *config, bool Output) {

  /*--- Number of non-physical points, local to the thread, needs
   *    further reduction if function is called in parallel ---*/
  unsigned long nonPhysicalPoints = 0;

  const unsigned short turb_model = config->GetKind_Turb_Model();
  const bool tkeNeeded = (turb_model == SST) || (turb_model == SST_SUST);

  SU2_OMP_FOR_STAT(omp_chunk_size)
  for (unsigned long iPoint = 0; iPoint < nPoint; iPoint ++) {

    /*--- Retrieve the value of the kinetic energy (if needed). ---*/

    su2double eddy_visc = 0.0, turb_ke = 0.0;

    if (turb_model != NONE && solver_container[TURB_SOL] != nullptr) {
      eddy_visc = solver_container[TURB_SOL]->GetNodes()->GetmuT(iPoint);
      if (tkeNeeded) turb_ke = solver_container[TURB_SOL]->GetNodes()->GetSolution(iPoint,0);

      if (config->GetKind_HybridRANSLES() != NO_HYBRIDRANSLES) {
        su2double DES_LengthScale = solver_container[TURB_SOL]->GetNodes()->GetDES_LengthScale(iPoint);
        nodes->SetDES_LengthScale(iPoint, DES_LengthScale);
      }
    }

    if (turb_model == NONE && SGSModelUsed)
        eddy_visc = solver_container[FLOW_SOL]->GetNodes()->GetEddyViscosity(iPoint);

    /*--- Compressible flow, primitive variables nDim+5, (T, vx, vy, vz, P, rho, h, c, lamMu, eddyMu, ThCond, Cp) ---*/

    bool physical = static_cast<CNSVariable*>(nodes)->SetPrimVar(iPoint, eddy_visc, turb_ke, GetFluidModel());
    nodes->SetSecondaryVar(iPoint, GetFluidModel());

    /*--- Check for non-realizable states for reporting. ---*/

    nonPhysicalPoints += !physical;

  }

  return nonPhysicalPoints;
}

void CNSSolver::Viscous_Residual(unsigned long iEdge, CGeometry *geometry, CSolver **solver_container,
                                 CNumerics *numerics, CConfig *config) {

  const bool implicit  = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
  const bool tkeNeeded = (config->GetKind_Turb_Model() == SST) ||
                         (config->GetKind_Turb_Model() == SST_SUST);

  CVariable* turbNodes = nullptr;
  if (tkeNeeded) turbNodes = solver_container[TURB_SOL]->GetNodes();

  /*--- Points, coordinates and normal vector in edge ---*/

  auto iPoint = geometry->edge[iEdge]->GetNode(0);
  auto jPoint = geometry->edge[iEdge]->GetNode(1);

  numerics->SetCoord(geometry->node[iPoint]->GetCoord(),
                     geometry->node[jPoint]->GetCoord());

  numerics->SetNormal(geometry->edge[iEdge]->GetNormal());

  /*--- Primitive and secondary variables. ---*/

  numerics->SetPrimitive(nodes->GetPrimitive(iPoint),
                         nodes->GetPrimitive(jPoint));

  numerics->SetSecondary(nodes->GetSecondary(iPoint),
                         nodes->GetSecondary(jPoint));

  /*--- Gradients. ---*/

  numerics->SetPrimVarGradient(nodes->GetGradient_Primitive(iPoint),
                               nodes->GetGradient_Primitive(jPoint));

  /*--- Turbulent kinetic energy. ---*/

  if (tkeNeeded)
    numerics->SetTurbKineticEnergy(turbNodes->GetSolution(iPoint,0),
                                   turbNodes->GetSolution(jPoint,0));

  /*--- Wall shear stress values (wall functions) ---*/

  numerics->SetTauWall(nodes->GetTauWall(iPoint),
                       nodes->GetTauWall(jPoint));

  numerics->SetTauWall_Flag(nodes->GetTauWall_Flag(iPoint),
                            nodes->GetTauWall_Flag(jPoint));

  /*--- Compute and update residual ---*/

  auto residual = numerics->ComputeResidual(config);

  if (ReducerStrategy) {
    EdgeFluxes.SubtractBlock(iEdge, residual);
    if (implicit)
      Jacobian.UpdateBlocksSub(iEdge, residual.jacobian_i, residual.jacobian_j);
  }
  else {
    LinSysRes.SubtractBlock(iPoint, residual);
    LinSysRes.AddBlock(jPoint, residual);

    if (implicit)
      Jacobian.UpdateBlocksSub(iEdge, iPoint, jPoint, residual.jacobian_i, residual.jacobian_j);
  }

}

void CNSSolver::Friction_Forces(CGeometry *geometry, CConfig *config) {

  unsigned long iVertex, iPoint, iPointNormal;
  unsigned short Boundary, Monitoring, iMarker, iMarker_Monitoring, iDim, jDim;
  su2double Viscosity = 0.0, div_vel, WallDist[3] = {0.0, 0.0, 0.0},
  Area, WallShearStress, TauNormal, factor, RefTemp, RefVel2, RefDensity, GradTemperature, Density = 0.0, WallDistMod, FrictionVel,
  Mach2Vel, Mach_Motion, UnitNormal[3] = {0.0, 0.0, 0.0}, TauElem[3] = {0.0, 0.0, 0.0}, TauTangent[3] = {0.0, 0.0, 0.0},
  Tau[3][3] = {{0.0, 0.0, 0.0},{0.0, 0.0, 0.0},{0.0, 0.0, 0.0}}, Cp, thermal_conductivity, MaxNorm = 8.0,
  Grad_Vel[3][3] = {{0.0, 0.0, 0.0},{0.0, 0.0, 0.0},{0.0, 0.0, 0.0}}, Grad_Temp[3] = {0.0, 0.0, 0.0},
  delta[3][3] = {{1.0, 0.0, 0.0},{0.0,1.0,0.0},{0.0,0.0,1.0}};
  su2double AxiFactor;
  const su2double *Coord = nullptr, *Coord_Normal = nullptr, *Normal = nullptr;

  string Marker_Tag, Monitoring_Tag;

  su2double Alpha = config->GetAoA()*PI_NUMBER/180.0;
  su2double Beta = config->GetAoS()*PI_NUMBER/180.0;
  su2double RefArea = config->GetRefArea();
  su2double RefLength = config->GetRefLength();
  su2double RefHeatFlux = config->GetHeat_Flux_Ref();
  su2double Gas_Constant = config->GetGas_ConstantND();
  const su2double *Origin = nullptr;

  if (config->GetnMarker_Monitoring() != 0) { Origin = config->GetRefOriginMoment(0); }

  su2double Prandtl_Lam = config->GetPrandtl_Lam();
  bool QCR = config->GetQCR();
  bool axisymmetric = config->GetAxisymmetric();

  /*--- Evaluate reference values for non-dimensionalization.
   For dynamic meshes, use the motion Mach number as a reference value
   for computing the force coefficients. Otherwise, use the freestream values,
   which is the standard convention. ---*/

  RefTemp = Temperature_Inf;
  RefDensity = Density_Inf;
  if (dynamic_grid) {
    Mach2Vel = sqrt(Gamma*Gas_Constant*RefTemp);
    Mach_Motion = config->GetMach_Motion();
    RefVel2 = (Mach_Motion*Mach2Vel)*(Mach_Motion*Mach2Vel);
  } else {
    RefVel2 = 0.0;
    for (iDim = 0; iDim < nDim; iDim++)
      RefVel2  += Velocity_Inf[iDim]*Velocity_Inf[iDim];
  }

  factor = 1.0 / (0.5*RefDensity*RefArea*RefVel2);

  /*--- Variables initialization ---*/

  AllBoundViscCoeff.setZero();
  SurfaceViscCoeff.setZero();

  AllBound_HF_Visc = 0.0;  AllBound_MaxHF_Visc = 0.0;

  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
    Surface_HF_Visc[iMarker_Monitoring]  = 0.0; Surface_MaxHF_Visc[iMarker_Monitoring]   = 0.0;
  }

  /*--- Loop over the Navier-Stokes markers ---*/

  for (iMarker = 0; iMarker < nMarker; iMarker++) {

    Boundary = config->GetMarker_All_KindBC(iMarker);
    Monitoring = config->GetMarker_All_Monitoring(iMarker);

    /*--- Obtain the origin for the moment computation for a particular marker ---*/

    if (Monitoring == YES) {
      for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
        Monitoring_Tag = config->GetMarker_Monitoring_TagBound(iMarker_Monitoring);
        Marker_Tag = config->GetMarker_All_TagBound(iMarker);
        if (Marker_Tag == Monitoring_Tag)
          Origin = config->GetRefOriginMoment(iMarker_Monitoring);
      }
    }

    if ((Boundary == HEAT_FLUX) || (Boundary == ISOTHERMAL) || (Boundary == HEAT_FLUX) || (Boundary == CHT_WALL_INTERFACE)) {

      /*--- Forces initialization at each Marker ---*/

      ViscCoeff.setZero(iMarker);

      HF_Visc[iMarker] = 0.0;    MaxHF_Visc[iMarker] = 0.0;

      su2double ForceViscous[MAXNDIM] = {0.0}, MomentViscous[MAXNDIM] = {0.0};
      su2double MomentX_Force[MAXNDIM] = {0.0}, MomentY_Force[MAXNDIM] = {0.0}, MomentZ_Force[MAXNDIM] = {0.0};

      /*--- Loop over the vertices to compute the forces ---*/

      for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {

        iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
        iPointNormal = geometry->vertex[iMarker][iVertex]->GetNormal_Neighbor();

        Coord = geometry->node[iPoint]->GetCoord();
        Coord_Normal = geometry->node[iPointNormal]->GetCoord();

        Normal = geometry->vertex[iMarker][iVertex]->GetNormal();

        for (iDim = 0; iDim < nDim; iDim++) {
          for (jDim = 0 ; jDim < nDim; jDim++) {
            Grad_Vel[iDim][jDim] = nodes->GetGradient_Primitive(iPoint,iDim+1, jDim);
          }
          Grad_Temp[iDim] = nodes->GetGradient_Primitive(iPoint,0, iDim);
        }

        Viscosity = nodes->GetLaminarViscosity(iPoint);
        Density = nodes->GetDensity(iPoint);

        Area = 0.0; for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim]; Area = sqrt(Area);


        for (iDim = 0; iDim < nDim; iDim++) {
          UnitNormal[iDim] = Normal[iDim]/Area;
        }

        /*--- Evaluate Tau ---*/

        div_vel = 0.0; for (iDim = 0; iDim < nDim; iDim++) div_vel += Grad_Vel[iDim][iDim];

        for (iDim = 0; iDim < nDim; iDim++) {
          for (jDim = 0 ; jDim < nDim; jDim++) {
            Tau[iDim][jDim] = Viscosity*(Grad_Vel[jDim][iDim] + Grad_Vel[iDim][jDim]) - TWO3*Viscosity*div_vel*delta[iDim][jDim];
          }
        }

        /*--- If necessary evaluate the QCR contribution to Tau ---*/

        if (QCR) {
          su2double den_aux, c_cr1=0.3, O_ik, O_jk;
          unsigned short kDim;

          /*--- Denominator Antisymmetric normalized rotation tensor ---*/

          den_aux = 0.0;
          for (iDim = 0 ; iDim < nDim; iDim++)
            for (jDim = 0 ; jDim < nDim; jDim++)
              den_aux += Grad_Vel[iDim][jDim] * Grad_Vel[iDim][jDim];
          den_aux = sqrt(max(den_aux,1E-10));

          /*--- Adding the QCR contribution ---*/

          for (iDim = 0 ; iDim < nDim; iDim++){
            for (jDim = 0 ; jDim < nDim; jDim++){
              for (kDim = 0 ; kDim < nDim; kDim++){
                O_ik = (Grad_Vel[iDim][kDim] - Grad_Vel[kDim][iDim])/ den_aux;
                O_jk = (Grad_Vel[jDim][kDim] - Grad_Vel[kDim][jDim])/ den_aux;
                Tau[iDim][jDim] -= c_cr1 * (O_ik * Tau[jDim][kDim] + O_jk * Tau[iDim][kDim]);
              }
            }
          }
        }

        /*--- Project Tau in each surface element ---*/

        for (iDim = 0; iDim < nDim; iDim++) {
          TauElem[iDim] = 0.0;
          for (jDim = 0; jDim < nDim; jDim++) {
            TauElem[iDim] += Tau[iDim][jDim]*UnitNormal[jDim];
          }
        }

        /*--- Compute wall shear stress (using the stress tensor). Compute wall skin friction coefficient, and heat flux on the wall ---*/

        TauNormal = 0.0; for (iDim = 0; iDim < nDim; iDim++) TauNormal += TauElem[iDim] * UnitNormal[iDim];

        WallShearStress = 0.0;
        for (iDim = 0; iDim < nDim; iDim++) {
          TauTangent[iDim] = TauElem[iDim] - TauNormal * UnitNormal[iDim];
          CSkinFriction[iMarker][iDim][iVertex] = TauTangent[iDim] / (0.5*RefDensity*RefVel2);
          WallShearStress += TauTangent[iDim] * TauTangent[iDim];
        }
        WallShearStress = sqrt(WallShearStress);

        /*--- If Wall Model is used, load the shear stress from the wall model---*/

        if (config->GetWall_Models() && nodes->GetTauWall_Flag(iPoint)){

          /*--- Note: TauElem will be used later for force computation ---*/
          for (iDim = 0; iDim < nDim; iDim++)
            TauElem[iDim] = nodes->GetTauWallDir(iPoint,iDim);
        }

        for (iDim = 0; iDim < nDim; iDim++) WallDist[iDim] = (Coord[iDim] - Coord_Normal[iDim]);
        WallDistMod = 0.0; for (iDim = 0; iDim < nDim; iDim++) WallDistMod += WallDist[iDim]*WallDist[iDim]; WallDistMod = sqrt(WallDistMod);

        /*--- Compute y+ and non-dimensional velocity ---*/

        FrictionVel = sqrt(fabs(WallShearStress)/Density);
        YPlus[iMarker][iVertex] = WallDistMod*FrictionVel/(Viscosity/Density);

        /*--- Compute total and maximum heat flux on the wall ---*/

        GradTemperature = 0.0;
        for (iDim = 0; iDim < nDim; iDim++)
          GradTemperature -= Grad_Temp[iDim]*UnitNormal[iDim];

        Cp = (Gamma / Gamma_Minus_One) * Gas_Constant;
        thermal_conductivity = Cp * Viscosity/Prandtl_Lam;
        HeatFlux[iMarker][iVertex] = -thermal_conductivity*GradTemperature*RefHeatFlux;

        /*--- Note that y+, and heat are computed at the
         halo cells (for visualization purposes), but not the forces ---*/

        if ((geometry->node[iPoint]->GetDomain()) && (Monitoring == YES)) {

          /*--- Axisymmetric simulations ---*/

          if (axisymmetric) AxiFactor = 2.0*PI_NUMBER*geometry->node[iPoint]->GetCoord(1);
          else AxiFactor = 1.0;

          /*--- Force computation ---*/

          su2double Force[MAXNDIM] = {0.0}, MomentDist[MAXNDIM] = {0.0};
          for (iDim = 0; iDim < nDim; iDim++) {
            Force[iDim] = TauElem[iDim] * Area * factor * AxiFactor;
            ForceViscous[iDim] += Force[iDim];
            MomentDist[iDim] = Coord[iDim] - Origin[iDim];
          }

          /*--- Moment with respect to the reference axis ---*/

          if (iDim == 3) {
            MomentViscous[0] += (Force[2]*MomentDist[1] - Force[1]*MomentDist[2])/RefLength;
            MomentX_Force[1] += (-Force[1]*Coord[2]);
            MomentX_Force[2] += (Force[2]*Coord[1]);

            MomentViscous[1] += (Force[0]*MomentDist[2] - Force[2]*MomentDist[0])/RefLength;
            MomentY_Force[2] += (-Force[2]*Coord[0]);
            MomentY_Force[0] += (Force[0]*Coord[2]);
          }
          MomentViscous[2] += (Force[1]*MomentDist[0] - Force[0]*MomentDist[1])/RefLength;
          MomentZ_Force[0] += (-Force[0]*Coord[1]);
          MomentZ_Force[1] += (Force[1]*Coord[0]);

        }

        HF_Visc[iMarker]          += HeatFlux[iMarker][iVertex]*Area;
        MaxHF_Visc[iMarker]       += pow(HeatFlux[iMarker][iVertex], MaxNorm);

      }

      /*--- Project forces and store the non-dimensional coefficients ---*/

      if (Monitoring == YES) {
        if (nDim == 2) {
          ViscCoeff.CD[iMarker]          =  ForceViscous[0]*cos(Alpha) + ForceViscous[1]*sin(Alpha);
          ViscCoeff.CL[iMarker]          = -ForceViscous[0]*sin(Alpha) + ForceViscous[1]*cos(Alpha);
          ViscCoeff.CEff[iMarker]        = ViscCoeff.CL[iMarker] / (ViscCoeff.CD[iMarker]+EPS);
          ViscCoeff.CFx[iMarker]         = ForceViscous[0];
          ViscCoeff.CFy[iMarker]         = ForceViscous[1];
          ViscCoeff.CMz[iMarker]         = MomentViscous[2];
          ViscCoeff.CoPx[iMarker]        = MomentZ_Force[1];
          ViscCoeff.CoPy[iMarker]        = -MomentZ_Force[0];
          ViscCoeff.CT[iMarker]          = -ViscCoeff.CFx[iMarker];
          ViscCoeff.CQ[iMarker]          = -ViscCoeff.CMz[iMarker];
          ViscCoeff.CMerit[iMarker]      = ViscCoeff.CT[iMarker] / (ViscCoeff.CQ[iMarker]+EPS);
          MaxHF_Visc[iMarker]            = pow(MaxHF_Visc[iMarker], 1.0/MaxNorm);
        }
        if (nDim == 3) {
          ViscCoeff.CD[iMarker]          =  ForceViscous[0]*cos(Alpha)*cos(Beta) + ForceViscous[1]*sin(Beta) + ForceViscous[2]*sin(Alpha)*cos(Beta);
          ViscCoeff.CL[iMarker]          = -ForceViscous[0]*sin(Alpha) + ForceViscous[2]*cos(Alpha);
          ViscCoeff.CSF[iMarker]         = -ForceViscous[0]*sin(Beta)*cos(Alpha) + ForceViscous[1]*cos(Beta) - ForceViscous[2]*sin(Beta)*sin(Alpha);
          ViscCoeff.CEff[iMarker]        = ViscCoeff.CL[iMarker]/(ViscCoeff.CD[iMarker] + EPS);
          ViscCoeff.CFx[iMarker]         = ForceViscous[0];
          ViscCoeff.CFy[iMarker]         = ForceViscous[1];
          ViscCoeff.CFz[iMarker]         = ForceViscous[2];
          ViscCoeff.CMx[iMarker]         = MomentViscous[0];
          ViscCoeff.CMy[iMarker]         = MomentViscous[1];
          ViscCoeff.CMz[iMarker]         = MomentViscous[2];
          ViscCoeff.CoPx[iMarker]        = -MomentY_Force[0];
          ViscCoeff.CoPz[iMarker]        = MomentY_Force[2];
          ViscCoeff.CT[iMarker]          = -ViscCoeff.CFz[iMarker];
          ViscCoeff.CQ[iMarker]          = -ViscCoeff.CMz[iMarker];
          ViscCoeff.CMerit[iMarker]      = ViscCoeff.CT[iMarker] / (ViscCoeff.CQ[iMarker] + EPS);
          MaxHF_Visc[iMarker]            = pow(MaxHF_Visc[iMarker], 1.0/MaxNorm);
        }

        AllBoundViscCoeff.CD          += ViscCoeff.CD[iMarker];
        AllBoundViscCoeff.CL          += ViscCoeff.CL[iMarker];
        AllBoundViscCoeff.CSF         += ViscCoeff.CSF[iMarker];
        AllBoundViscCoeff.CFx         += ViscCoeff.CFx[iMarker];
        AllBoundViscCoeff.CFy         += ViscCoeff.CFy[iMarker];
        AllBoundViscCoeff.CFz         += ViscCoeff.CFz[iMarker];
        AllBoundViscCoeff.CMx         += ViscCoeff.CMx[iMarker];
        AllBoundViscCoeff.CMy         += ViscCoeff.CMy[iMarker];
        AllBoundViscCoeff.CMz         += ViscCoeff.CMz[iMarker];
        AllBoundViscCoeff.CoPx        += ViscCoeff.CoPx[iMarker];
        AllBoundViscCoeff.CoPy        += ViscCoeff.CoPy[iMarker];
        AllBoundViscCoeff.CoPz        += ViscCoeff.CoPz[iMarker];
        AllBoundViscCoeff.CT          += ViscCoeff.CT[iMarker];
        AllBoundViscCoeff.CQ          += ViscCoeff.CQ[iMarker];
        AllBound_HF_Visc              += HF_Visc[iMarker];
        AllBound_MaxHF_Visc           += pow(MaxHF_Visc[iMarker], MaxNorm);

        /*--- Compute the coefficients per surface ---*/

        for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
          Monitoring_Tag = config->GetMarker_Monitoring_TagBound(iMarker_Monitoring);
          Marker_Tag = config->GetMarker_All_TagBound(iMarker);
          if (Marker_Tag == Monitoring_Tag) {
            SurfaceViscCoeff.CL[iMarker_Monitoring]      += ViscCoeff.CL[iMarker];
            SurfaceViscCoeff.CD[iMarker_Monitoring]      += ViscCoeff.CD[iMarker];
            SurfaceViscCoeff.CSF[iMarker_Monitoring]     += ViscCoeff.CSF[iMarker];
            SurfaceViscCoeff.CEff[iMarker_Monitoring]    += ViscCoeff.CEff[iMarker];
            SurfaceViscCoeff.CFx[iMarker_Monitoring]     += ViscCoeff.CFx[iMarker];
            SurfaceViscCoeff.CFy[iMarker_Monitoring]     += ViscCoeff.CFy[iMarker];
            SurfaceViscCoeff.CFz[iMarker_Monitoring]     += ViscCoeff.CFz[iMarker];
            SurfaceViscCoeff.CMx[iMarker_Monitoring]     += ViscCoeff.CMx[iMarker];
            SurfaceViscCoeff.CMy[iMarker_Monitoring]     += ViscCoeff.CMy[iMarker];
            SurfaceViscCoeff.CMz[iMarker_Monitoring]     += ViscCoeff.CMz[iMarker];
            Surface_HF_Visc[iMarker_Monitoring]          += HF_Visc[iMarker];
            Surface_MaxHF_Visc[iMarker_Monitoring]       += pow(MaxHF_Visc[iMarker],MaxNorm);
          }
        }

      }

    }
  }

  /*--- Update some global coeffients ---*/

  AllBoundViscCoeff.CEff = AllBoundViscCoeff.CL / (AllBoundViscCoeff.CD + EPS);
  AllBoundViscCoeff.CMerit = AllBoundViscCoeff.CT / (AllBoundViscCoeff.CQ + EPS);
  AllBound_MaxHF_Visc = pow(AllBound_MaxHF_Visc, 1.0/MaxNorm);


#ifdef HAVE_MPI

  /*--- Add AllBound information using all the nodes ---*/

  if (config->GetComm_Level() == COMM_FULL) {

    auto Allreduce = [](su2double x) {
      su2double tmp = x; x = 0.0;
      SU2_MPI::Allreduce(&tmp, &x, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      return x;
    };
    AllBoundViscCoeff.CD = Allreduce(AllBoundViscCoeff.CD);
    AllBoundViscCoeff.CL = Allreduce(AllBoundViscCoeff.CL);
    AllBoundViscCoeff.CSF = Allreduce(AllBoundViscCoeff.CSF);
    AllBoundViscCoeff.CEff = AllBoundViscCoeff.CL / (AllBoundViscCoeff.CD + EPS);

    AllBoundViscCoeff.CMx = Allreduce(AllBoundViscCoeff.CMx);
    AllBoundViscCoeff.CMy = Allreduce(AllBoundViscCoeff.CMy);
    AllBoundViscCoeff.CMz = Allreduce(AllBoundViscCoeff.CMz);

    AllBoundViscCoeff.CFx = Allreduce(AllBoundViscCoeff.CFx);
    AllBoundViscCoeff.CFy = Allreduce(AllBoundViscCoeff.CFy);
    AllBoundViscCoeff.CFz = Allreduce(AllBoundViscCoeff.CFz);

    AllBoundViscCoeff.CoPx = Allreduce(AllBoundViscCoeff.CoPx);
    AllBoundViscCoeff.CoPy = Allreduce(AllBoundViscCoeff.CoPy);
    AllBoundViscCoeff.CoPz = Allreduce(AllBoundViscCoeff.CoPz);

    AllBoundViscCoeff.CT = Allreduce(AllBoundViscCoeff.CT);
    AllBoundViscCoeff.CQ = Allreduce(AllBoundViscCoeff.CQ);
    AllBoundViscCoeff.CMerit = AllBoundViscCoeff.CT / (AllBoundViscCoeff.CQ + EPS);

    AllBound_HF_Visc = Allreduce(AllBound_HF_Visc);
    AllBound_MaxHF_Visc = pow(Allreduce(pow(AllBound_MaxHF_Visc, MaxNorm)), 1.0/MaxNorm);

  }

  /*--- Add the forces on the surfaces using all the nodes ---*/

  if (config->GetComm_Level() == COMM_FULL) {

    int nMarkerMon = config->GetnMarker_Monitoring();

    /*--- Use the same buffer for all reductions. We could avoid the copy back into
     *    the original variable by swaping pointers, but it is safer this way... ---*/

    su2double* buffer = new su2double [nMarkerMon];

    auto Allreduce_inplace = [buffer](int size, su2double* x) {
      SU2_MPI::Allreduce(x, buffer, size, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      for(int i=0; i<size; ++i) x[i] = buffer[i];
    };

    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CL);
    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CD);
    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CSF);

    for (iMarker_Monitoring = 0; iMarker_Monitoring < nMarkerMon; iMarker_Monitoring++)
      SurfaceViscCoeff.CEff[iMarker_Monitoring] = SurfaceViscCoeff.CL[iMarker_Monitoring] /
                                                 (SurfaceViscCoeff.CD[iMarker_Monitoring] + EPS);

    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CFx);
    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CFy);
    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CFz);

    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CMx);
    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CMy);
    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CMz);

    Allreduce_inplace(nMarkerMon, Surface_HF_Visc);
    Allreduce_inplace(nMarkerMon, Surface_MaxHF_Visc);

    delete [] buffer;

  }

#endif

  /*--- Update the total coefficients (note that all the nodes have the same value)---*/

  TotalCoeff.CD          += AllBoundViscCoeff.CD;
  TotalCoeff.CL          += AllBoundViscCoeff.CL;
  TotalCoeff.CSF         += AllBoundViscCoeff.CSF;
  TotalCoeff.CEff         = TotalCoeff.CL / (TotalCoeff.CD + EPS);
  TotalCoeff.CFx         += AllBoundViscCoeff.CFx;
  TotalCoeff.CFy         += AllBoundViscCoeff.CFy;
  TotalCoeff.CFz         += AllBoundViscCoeff.CFz;
  TotalCoeff.CMx         += AllBoundViscCoeff.CMx;
  TotalCoeff.CMy         += AllBoundViscCoeff.CMy;
  TotalCoeff.CMz         += AllBoundViscCoeff.CMz;
  TotalCoeff.CoPx        += AllBoundViscCoeff.CoPx;
  TotalCoeff.CoPy        += AllBoundViscCoeff.CoPy;
  TotalCoeff.CoPz        += AllBoundViscCoeff.CoPz;
  TotalCoeff.CT          += AllBoundViscCoeff.CT;
  TotalCoeff.CQ          += AllBoundViscCoeff.CQ;
  TotalCoeff.CMerit       = AllBoundViscCoeff.CT / (AllBoundViscCoeff.CQ + EPS);
  Total_Heat         = AllBound_HF_Visc;
  Total_MaxHeat      = AllBound_MaxHF_Visc;

  /*--- Update the total coefficients per surface (note that all the nodes have the same value)---*/

  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
    SurfaceCoeff.CL[iMarker_Monitoring]         += SurfaceViscCoeff.CL[iMarker_Monitoring];
    SurfaceCoeff.CD[iMarker_Monitoring]         += SurfaceViscCoeff.CD[iMarker_Monitoring];
    SurfaceCoeff.CSF[iMarker_Monitoring]        += SurfaceViscCoeff.CSF[iMarker_Monitoring];
    SurfaceCoeff.CEff[iMarker_Monitoring]        = SurfaceViscCoeff.CL[iMarker_Monitoring] / (SurfaceCoeff.CD[iMarker_Monitoring] + EPS);
    SurfaceCoeff.CFx[iMarker_Monitoring]        += SurfaceViscCoeff.CFx[iMarker_Monitoring];
    SurfaceCoeff.CFy[iMarker_Monitoring]        += SurfaceViscCoeff.CFy[iMarker_Monitoring];
    SurfaceCoeff.CFz[iMarker_Monitoring]        += SurfaceViscCoeff.CFz[iMarker_Monitoring];
    SurfaceCoeff.CMx[iMarker_Monitoring]        += SurfaceViscCoeff.CMx[iMarker_Monitoring];
    SurfaceCoeff.CMy[iMarker_Monitoring]        += SurfaceViscCoeff.CMy[iMarker_Monitoring];
    SurfaceCoeff.CMz[iMarker_Monitoring]        += SurfaceViscCoeff.CMz[iMarker_Monitoring];
  }

}

void CNSSolver::Buffet_Monitoring(CGeometry *geometry, CConfig *config) {

  unsigned long iVertex;
  unsigned short Boundary, Monitoring, iMarker, iMarker_Monitoring, iDim;
  su2double *Vel_FS = config->GetVelocity_FreeStream();
  su2double VelMag_FS = 0.0, SkinFrictionMag = 0.0, SkinFrictionDot = 0.0, *Normal, Area, Sref = config->GetRefArea();
  su2double k   = config->GetBuffet_k(),
             lam = config->GetBuffet_lambda();
  string Marker_Tag, Monitoring_Tag;

  for (iDim = 0; iDim < nDim; iDim++){
    VelMag_FS += Vel_FS[iDim]*Vel_FS[iDim];
  }
  VelMag_FS = sqrt(VelMag_FS);

  /*-- Variables initialization ---*/

  Total_Buffet_Metric = 0.0;

  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
    Surface_Buffet_Metric[iMarker_Monitoring] = 0.0;
  }

  /*--- Loop over the Euler and Navier-Stokes markers ---*/

  for (iMarker = 0; iMarker < nMarker; iMarker++) {

    Buffet_Metric[iMarker] = 0.0;

    Boundary   = config->GetMarker_All_KindBC(iMarker);
    Monitoring = config->GetMarker_All_Monitoring(iMarker);

    if ((Boundary == HEAT_FLUX) || (Boundary == ISOTHERMAL) || (Boundary == HEAT_FLUX) || (Boundary == CHT_WALL_INTERFACE)) {

      /*--- Loop over the vertices to compute the buffet sensor ---*/

      for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {

        /*--- Perform dot product of skin friction with freestream velocity ---*/

        SkinFrictionMag = 0.0;
        SkinFrictionDot = 0.0;
        for(iDim = 0; iDim < nDim; iDim++){
          SkinFrictionMag += CSkinFriction[iMarker][iDim][iVertex]*CSkinFriction[iMarker][iDim][iVertex];
          SkinFrictionDot += CSkinFriction[iMarker][iDim][iVertex]*Vel_FS[iDim];
        }
        SkinFrictionMag = sqrt(SkinFrictionMag);

        /*--- Normalize the dot product ---*/

        SkinFrictionDot /= SkinFrictionMag*VelMag_FS;

        /*--- Compute Heaviside function ---*/

        Buffet_Sensor[iMarker][iVertex] = 1./(1. + exp(2.*k*(SkinFrictionDot + lam)));

        /*--- Integrate buffet sensor ---*/

        if(Monitoring == YES){

          Normal = geometry->vertex[iMarker][iVertex]->GetNormal();
          Area = 0.0;
          for(iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim];
          Area = sqrt(Area);

          Buffet_Metric[iMarker] += Buffet_Sensor[iMarker][iVertex]*Area/Sref;

        }

      }

      if(Monitoring == YES){

        Total_Buffet_Metric += Buffet_Metric[iMarker];

        /*--- Per surface buffet metric ---*/

        for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
          Monitoring_Tag = config->GetMarker_Monitoring_TagBound(iMarker_Monitoring);
          Marker_Tag = config->GetMarker_All_TagBound(iMarker);
          if (Marker_Tag == Monitoring_Tag) Surface_Buffet_Metric[iMarker_Monitoring] = Buffet_Metric[iMarker];
        }

      }

    }

  }

#ifdef HAVE_MPI

  /*--- Add buffet metric information using all the nodes ---*/

  su2double MyTotal_Buffet_Metric = Total_Buffet_Metric;
  SU2_MPI::Allreduce(&MyTotal_Buffet_Metric, &Total_Buffet_Metric, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

  /*--- Add the buffet metric on the surfaces using all the nodes ---*/

  su2double *MySurface_Buffet_Metric = new su2double[config->GetnMarker_Monitoring()];

  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
    MySurface_Buffet_Metric[iMarker_Monitoring] = Surface_Buffet_Metric[iMarker_Monitoring];
  }

  SU2_MPI::Allreduce(MySurface_Buffet_Metric, Surface_Buffet_Metric, config->GetnMarker_Monitoring(), MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

  delete [] MySurface_Buffet_Metric;

#endif

}

void CNSSolver::Evaluate_ObjFunc(CConfig *config) {

  unsigned short iMarker_Monitoring, Kind_ObjFunc;
  su2double Weight_ObjFunc;

  /*--- Evaluate objective functions common to Euler and NS solvers ---*/

  CEulerSolver::Evaluate_ObjFunc(config);

  /*--- Evaluate objective functions specific to NS solver ---*/

  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {

    Weight_ObjFunc = config->GetWeight_ObjFunc(iMarker_Monitoring);
    Kind_ObjFunc = config->GetKind_ObjFunc(iMarker_Monitoring);

    switch(Kind_ObjFunc) {
      case BUFFET_SENSOR:
          Total_ComboObj +=Weight_ObjFunc*Surface_Buffet_Metric[iMarker_Monitoring];
          break;
      default:
          break;
    }
  }

}


void CNSSolver::BC_HeatFlux_Wall(CGeometry *geometry, CSolver **solver_container, CNumerics *conv_numerics,
                                 CNumerics *visc_numerics, CConfig *config, unsigned short val_marker) {

  unsigned short iDim, jDim, iVar, jVar;
  unsigned long iVertex, iPoint, Point_Normal, total_index;

  su2double Wall_HeatFlux, dist_ij, *Coord_i, *Coord_j, theta2;
  su2double thetax, thetay, thetaz, etax, etay, etaz, pix, piy, piz, factor;
  su2double ProjGridVel, *GridVel, GridVel2, *Normal, Area, Pressure = 0.0;
  su2double total_viscosity, div_vel, Density, tau_vel[3] = {0.0, 0.0, 0.0}, UnitNormal[3] = {0.0, 0.0, 0.0};
  su2double laminar_viscosity = 0.0, eddy_viscosity = 0.0, Grad_Vel[3][3] = {{0.0,0.0,0.0},{0.0,0.0,0.0},{0.0,0.0,0.0}},
  tau[3][3] = {{0.0,0.0,0.0},{0.0,0.0,0.0},{0.0,0.0,0.0}};
  su2double delta[3][3] = {{1.0, 0.0, 0.0},{0.0,1.0,0.0},{0.0,0.0,1.0}};

  bool implicit       = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);

  /*--- Identify the boundary by string name ---*/

  string Marker_Tag = config->GetMarker_All_TagBound(val_marker);

  /*--- Get the specified wall heat flux from config as well as the
        wall function treatment.---*/

  Wall_HeatFlux = config->GetWall_HeatFlux(Marker_Tag)/config->GetHeat_Flux_Ref();

//  Wall_Function = config->GetWallFunction_Treatment(Marker_Tag);
//  if (Wall_Function != NO_WALL_FUNCTION) {
//    SU2_MPI::Error("Wall function treament not implemented yet", CURRENT_FUNCTION);
//  }

  /*--- Loop over all of the vertices on this boundary marker ---*/

  for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
    iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

    /*--- Check if the node belongs to the domain (i.e, not a halo node) ---*/

    if (geometry->node[iPoint]->GetDomain()) {

      /*--- If it is a customizable patch, retrieve the specified wall heat flux. ---*/

      if (config->GetMarker_All_PyCustom(val_marker)) Wall_HeatFlux = geometry->GetCustomBoundaryHeatFlux(val_marker, iVertex);

      /*--- Compute dual-grid area and boundary normal ---*/

      Normal = geometry->vertex[val_marker][iVertex]->GetNormal();

      Area = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
        Area += Normal[iDim]*Normal[iDim];
      Area = sqrt (Area);

      for (iDim = 0; iDim < nDim; iDim++)
        UnitNormal[iDim] = -Normal[iDim]/Area;

      /*--- Initialize the convective & viscous residuals to zero ---*/

      for (iVar = 0; iVar < nVar; iVar++) {
        Res_Conv[iVar] = 0.0;
        Res_Visc[iVar] = 0.0;
      }

      /*--- Store the corrected velocity at the wall which will
       be zero (v = 0), unless there are moving walls (v = u_wall)---*/

      if (dynamic_grid) {
        GridVel = geometry->node[iPoint]->GetGridVel();
        for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = GridVel[iDim];
      } else {
        for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = 0.0;
      }

      /*--- Impose the value of the velocity as a strong boundary
       condition (Dirichlet). Fix the velocity and remove any
       contribution to the residual at this node. ---*/

      nodes->SetVelocity_Old(iPoint,Vector);

      for (iDim = 0; iDim < nDim; iDim++)
        LinSysRes.SetBlock_Zero(iPoint, iDim+1);
      nodes->SetVel_ResTruncError_Zero(iPoint);

      /*--- Apply a weak boundary condition for the energy equation.
       Compute the residual due to the prescribed heat flux. ---*/

      Res_Visc[nDim+1] = Wall_HeatFlux * Area;

      /*--- If the wall is moving, there are additional residual contributions
       due to pressure (p v_wall.n) and shear stress (tau.v_wall.n). ---*/

      if (dynamic_grid) {

        /*--- Get the grid velocity at the current boundary node ---*/

        GridVel = geometry->node[iPoint]->GetGridVel();
        ProjGridVel = 0.0;
        for (iDim = 0; iDim < nDim; iDim++)
          ProjGridVel += GridVel[iDim]*UnitNormal[iDim]*Area;

        /*--- Retrieve other primitive quantities and viscosities ---*/

        Density  = nodes->GetDensity(iPoint);
        Pressure = nodes->GetPressure(iPoint);
        laminar_viscosity = nodes->GetLaminarViscosity(iPoint);
        eddy_viscosity    = nodes->GetEddyViscosity(iPoint);
        total_viscosity   = laminar_viscosity + eddy_viscosity;

        for (iDim = 0; iDim < nDim; iDim++) {
          for (jDim = 0 ; jDim < nDim; jDim++) {
            Grad_Vel[iDim][jDim] = nodes->GetGradient_Primitive(iPoint,iDim+1, jDim);
          }
        }

        /*--- Divergence of the velocity ---*/

        div_vel = 0.0; for (iDim = 0 ; iDim < nDim; iDim++) div_vel += Grad_Vel[iDim][iDim];

        /*--- Compute the viscous stress tensor ---*/

        for (iDim = 0; iDim < nDim; iDim++) {
          for (jDim = 0; jDim < nDim; jDim++) {
            tau[iDim][jDim] = total_viscosity*( Grad_Vel[jDim][iDim]+Grad_Vel[iDim][jDim] ) -
                              TWO3*total_viscosity*div_vel*delta[iDim][jDim];
          }
        }

        /*--- Dot product of the stress tensor with the grid velocity ---*/

        for (iDim = 0 ; iDim < nDim; iDim++) {
          tau_vel[iDim] = 0.0;
          for (jDim = 0 ; jDim < nDim; jDim++)
            tau_vel[iDim] += tau[iDim][jDim]*GridVel[jDim];
        }

        /*--- Compute the convective and viscous residuals (energy eqn.) ---*/

        Res_Conv[nDim+1] = Pressure*ProjGridVel;
        for (iDim = 0 ; iDim < nDim; iDim++)
          Res_Visc[nDim+1] += tau_vel[iDim]*UnitNormal[iDim]*Area;

        /*--- Implicit Jacobian contributions due to moving walls ---*/

        if (implicit) {

          /*--- Jacobian contribution related to the pressure term ---*/

          GridVel2 = 0.0;
          for (iDim = 0; iDim < nDim; iDim++)
            GridVel2 += GridVel[iDim]*GridVel[iDim];
          for (iVar = 0; iVar < nVar; iVar++)
            for (jVar = 0; jVar < nVar; jVar++)
              Jacobian_i[iVar][jVar] = 0.0;
          Jacobian_i[nDim+1][0] = 0.5*(Gamma-1.0)*GridVel2*ProjGridVel;
          for (jDim = 0; jDim < nDim; jDim++)
            Jacobian_i[nDim+1][jDim+1] = -(Gamma-1.0)*GridVel[jDim]*ProjGridVel;
          Jacobian_i[nDim+1][nDim+1] = (Gamma-1.0)*ProjGridVel;

          /*--- Add the block to the Global Jacobian structure ---*/

          Jacobian.AddBlock2Diag(iPoint, Jacobian_i);

          /*--- Now the Jacobian contribution related to the shear stress ---*/

          for (iVar = 0; iVar < nVar; iVar++)
            for (jVar = 0; jVar < nVar; jVar++)
              Jacobian_i[iVar][jVar] = 0.0;

          /*--- Compute closest normal neighbor ---*/

          Point_Normal = geometry->vertex[val_marker][iVertex]->GetNormal_Neighbor();

          /*--- Get coordinates of i & nearest normal and compute distance ---*/

          Coord_i = geometry->node[iPoint]->GetCoord();
          Coord_j = geometry->node[Point_Normal]->GetCoord();

          dist_ij = 0;
          for (iDim = 0; iDim < nDim; iDim++)
            dist_ij += (Coord_j[iDim]-Coord_i[iDim])*(Coord_j[iDim]-Coord_i[iDim]);
          dist_ij = sqrt(dist_ij);

          theta2 = 0.0;
          for (iDim = 0; iDim < nDim; iDim++)
            theta2 += UnitNormal[iDim]*UnitNormal[iDim];

          factor = total_viscosity*Area/(Density*dist_ij);

          if (nDim == 2) {
            thetax = theta2 + UnitNormal[0]*UnitNormal[0]/3.0;
            thetay = theta2 + UnitNormal[1]*UnitNormal[1]/3.0;

            etaz   = UnitNormal[0]*UnitNormal[1]/3.0;

            pix = GridVel[0]*thetax + GridVel[1]*etaz;
            piy = GridVel[0]*etaz   + GridVel[1]*thetay;

            Jacobian_i[nDim+1][0] -= factor*(-pix*GridVel[0]+piy*GridVel[1]);
            Jacobian_i[nDim+1][1] -= factor*pix;
            Jacobian_i[nDim+1][2] -= factor*piy;
          } else {
            thetax = theta2 + UnitNormal[0]*UnitNormal[0]/3.0;
            thetay = theta2 + UnitNormal[1]*UnitNormal[1]/3.0;
            thetaz = theta2 + UnitNormal[2]*UnitNormal[2]/3.0;

            etaz = UnitNormal[0]*UnitNormal[1]/3.0;
            etax = UnitNormal[1]*UnitNormal[2]/3.0;
            etay = UnitNormal[0]*UnitNormal[2]/3.0;

            pix = GridVel[0]*thetax + GridVel[1]*etaz   + GridVel[2]*etay;
            piy = GridVel[0]*etaz   + GridVel[1]*thetay + GridVel[2]*etax;
            piz = GridVel[0]*etay   + GridVel[1]*etax   + GridVel[2]*thetaz;

            Jacobian_i[nDim+1][0] -= factor*(-pix*GridVel[0]+piy*GridVel[1]+piz*GridVel[2]);
            Jacobian_i[nDim+1][1] -= factor*pix;
            Jacobian_i[nDim+1][2] -= factor*piy;
            Jacobian_i[nDim+1][3] -= factor*piz;
          }

          /*--- Subtract the block from the Global Jacobian structure ---*/

          Jacobian.SubtractBlock2Diag(iPoint, Jacobian_i);

        }
      }

      /*--- Convective contribution to the residual at the wall ---*/

      LinSysRes.AddBlock(iPoint, Res_Conv);

      /*--- Viscous contribution to the residual at the wall ---*/

      LinSysRes.SubtractBlock(iPoint, Res_Visc);

      /*--- Enforce the no-slip boundary condition in a strong way by
       modifying the velocity-rows of the Jacobian (1 on the diagonal). ---*/

      if (implicit) {
        for (iVar = 1; iVar <= nDim; iVar++) {
          total_index = iPoint*nVar+iVar;
          Jacobian.DeleteValsRowi(total_index);
        }
      }

    }
  }
}

void CNSSolver::BC_Isothermal_Wall(CGeometry *geometry, CSolver **solver_container, CNumerics *conv_numerics,
                                   CNumerics *visc_numerics, CConfig *config, unsigned short val_marker) {

  unsigned short iVar, jVar, iDim, jDim;
  unsigned long iVertex, iPoint, Point_Normal, total_index;

  su2double *Normal, *Coord_i, *Coord_j, Area, dist_ij, theta2;
  su2double Twall, dTdn, dTdrho, thermal_conductivity;
  su2double thetax, thetay, thetaz, etax, etay, etaz, pix, piy, piz, factor;
  su2double ProjGridVel, *GridVel, GridVel2, Pressure = 0.0, Density, Vel2;
  su2double total_viscosity, div_vel, tau_vel[3] = {0.0,0.0,0.0}, UnitNormal[3] = {0.0,0.0,0.0};
  su2double laminar_viscosity, eddy_viscosity, Grad_Vel[3][3] = {{1.0, 0.0, 0.0},{0.0,1.0,0.0},{0.0,0.0,1.0}},
  tau[3][3] = {{0.0, 0.0, 0.0},{0.0,0.0,0.0},{0.0,0.0,0.0}}, delta[3][3] = {{1.0, 0.0, 0.0},{0.0,1.0,0.0},{0.0,0.0,1.0}};

  su2double Prandtl_Lam  = config->GetPrandtl_Lam();
  su2double Prandtl_Turb = config->GetPrandtl_Turb();
  su2double Gas_Constant = config->GetGas_ConstantND();
  su2double Cp = (Gamma / Gamma_Minus_One) * Gas_Constant;

  bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);

  /*--- Identify the boundary ---*/

  string Marker_Tag = config->GetMarker_All_TagBound(val_marker);

  /*--- Retrieve the specified wall temperature from config
        as well as the wall function treatment.---*/

  Twall = config->GetIsothermal_Temperature(Marker_Tag)/config->GetTemperature_Ref();

//  Wall_Function = config->GetWallFunction_Treatment(Marker_Tag);
//  if (Wall_Function != NO_WALL_FUNCTION) {
//    SU2_MPI::Error("Wall function treament not implemented yet", CURRENT_FUNCTION);
//  }

  /*--- Loop over boundary points ---*/

  for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {

    iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

    if (geometry->node[iPoint]->GetDomain()) {

      /*--- If it is a customizable patch, retrieve the specified wall temperature. ---*/

      if (config->GetMarker_All_PyCustom(val_marker)) Twall = geometry->GetCustomBoundaryTemperature(val_marker, iVertex);

      /*--- Compute dual-grid area and boundary normal ---*/

      Normal = geometry->vertex[val_marker][iVertex]->GetNormal();

      Area = 0.0; for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim]; Area = sqrt (Area);

      for (iDim = 0; iDim < nDim; iDim++)
        UnitNormal[iDim] = -Normal[iDim]/Area;

      /*--- Calculate useful quantities ---*/

      theta2 = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
        theta2 += UnitNormal[iDim]*UnitNormal[iDim];

      /*--- Compute closest normal neighbor ---*/

      Point_Normal = geometry->vertex[val_marker][iVertex]->GetNormal_Neighbor();

      /*--- Get coordinates of i & nearest normal and compute distance ---*/

      Coord_i = geometry->node[iPoint]->GetCoord();
      Coord_j = geometry->node[Point_Normal]->GetCoord();
      dist_ij = 0;
      for (iDim = 0; iDim < nDim; iDim++)
        dist_ij += (Coord_j[iDim]-Coord_i[iDim])*(Coord_j[iDim]-Coord_i[iDim]);
      dist_ij = sqrt(dist_ij);

      /*--- Store the corrected velocity at the wall which will
       be zero (v = 0), unless there is grid motion (v = u_wall)---*/

      if (dynamic_grid) {
        GridVel = geometry->node[iPoint]->GetGridVel();
        for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = GridVel[iDim];
      }
      else {
        for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = 0.0;
      }

      /*--- Initialize the convective & viscous residuals to zero ---*/

      for (iVar = 0; iVar < nVar; iVar++) {
        Res_Conv[iVar] = 0.0;
        Res_Visc[iVar] = 0.0;
      }

      /*--- Set the residual, truncation error and velocity value on the boundary ---*/

      nodes->SetVelocity_Old(iPoint,Vector);

      for (iDim = 0; iDim < nDim; iDim++)
        LinSysRes.SetBlock_Zero(iPoint, iDim+1);
      nodes->SetVel_ResTruncError_Zero(iPoint);

      /*--- Compute the normal gradient in temperature using Twall ---*/

      dTdn = -(nodes->GetTemperature(Point_Normal) - Twall)/dist_ij;

      /*--- Get transport coefficients ---*/

      laminar_viscosity    = nodes->GetLaminarViscosity(iPoint);
      eddy_viscosity       = nodes->GetEddyViscosity(iPoint);
      thermal_conductivity = Cp * ( laminar_viscosity/Prandtl_Lam + eddy_viscosity/Prandtl_Turb);

      // work in progress on real-gases...
      //thermal_conductivity = nodes->GetThermalConductivity(iPoint);
      //Cp = nodes->GetSpecificHeatCp(iPoint);
      //thermal_conductivity += Cp*eddy_viscosity/Prandtl_Turb;

      /*--- Apply a weak boundary condition for the energy equation.
       Compute the residual due to the prescribed heat flux. ---*/

      Res_Visc[nDim+1] = thermal_conductivity * dTdn * Area;

      /*--- Calculate Jacobian for implicit time stepping ---*/

      if (implicit) {

        for (iVar = 0; iVar < nVar; iVar ++)
          for (jVar = 0; jVar < nVar; jVar ++)
            Jacobian_i[iVar][jVar] = 0.0;

        /*--- Calculate useful quantities ---*/

        Density = nodes->GetDensity(iPoint);
        Vel2 = 0.0;
        for (iDim = 0; iDim < nDim; iDim++)
          Vel2 += pow(nodes->GetVelocity(iPoint,iDim),2);
        dTdrho = 1.0/Density * ( -Twall + (Gamma-1.0)/Gas_Constant*(Vel2/2.0) );

        /*--- Enforce the no-slip boundary condition in a strong way ---*/

        for (iVar = 1; iVar <= nDim; iVar++) {
          total_index = iPoint*nVar+iVar;
          Jacobian.DeleteValsRowi(total_index);
        }

        /*--- Add contributions to the Jacobian from the weak enforcement of the energy equations ---*/

        Jacobian_i[nDim+1][0]      = -thermal_conductivity*theta2/dist_ij * dTdrho * Area;
        Jacobian_i[nDim+1][nDim+1] = -thermal_conductivity*theta2/dist_ij * (Gamma-1.0)/(Gas_Constant*Density) * Area;

        /*--- Subtract the block from the Global Jacobian structure ---*/

        Jacobian.SubtractBlock2Diag(iPoint, Jacobian_i);

      }

      /*--- If the wall is moving, there are additional residual contributions
       due to pressure (p v_wall.n) and shear stress (tau.v_wall.n). ---*/

      if (dynamic_grid) {

        /*--- Get the grid velocity at the current boundary node ---*/

        GridVel = geometry->node[iPoint]->GetGridVel();
        ProjGridVel = 0.0;
        for (iDim = 0; iDim < nDim; iDim++)
          ProjGridVel += GridVel[iDim]*UnitNormal[iDim]*Area;

        /*--- Retrieve other primitive quantities and viscosities ---*/

        Density  = nodes->GetDensity(iPoint);
        Pressure = nodes->GetPressure(iPoint);
        laminar_viscosity = nodes->GetLaminarViscosity(iPoint);
        eddy_viscosity    = nodes->GetEddyViscosity(iPoint);

        total_viscosity   = laminar_viscosity + eddy_viscosity;

        for (iDim = 0; iDim < nDim; iDim++) {
          for (jDim = 0 ; jDim < nDim; jDim++) {
            Grad_Vel[iDim][jDim] = nodes->GetGradient_Primitive(iPoint,iDim+1, jDim);
          }
        }

        /*--- Divergence of the velocity ---*/

        div_vel = 0.0; for (iDim = 0 ; iDim < nDim; iDim++) div_vel += Grad_Vel[iDim][iDim];

        /*--- Compute the viscous stress tensor ---*/

        for (iDim = 0; iDim < nDim; iDim++)
          for (jDim = 0; jDim < nDim; jDim++) {
            tau[iDim][jDim] = total_viscosity*( Grad_Vel[jDim][iDim] + Grad_Vel[iDim][jDim] ) -
                                TWO3*total_viscosity*div_vel*delta[iDim][jDim];
          }

        /*--- Dot product of the stress tensor with the grid velocity ---*/

        for (iDim = 0 ; iDim < nDim; iDim++) {
          tau_vel[iDim] = 0.0;
          for (jDim = 0 ; jDim < nDim; jDim++)
            tau_vel[iDim] += tau[iDim][jDim]*GridVel[jDim];
        }

        /*--- Compute the convective and viscous residuals (energy eqn.) ---*/

        Res_Conv[nDim+1] = Pressure*ProjGridVel;
        for (iDim = 0 ; iDim < nDim; iDim++)
          Res_Visc[nDim+1] += tau_vel[iDim]*UnitNormal[iDim]*Area;

        /*--- Implicit Jacobian contributions due to moving walls ---*/

        if (implicit) {

          /*--- Jacobian contribution related to the pressure term ---*/

          GridVel2 = 0.0;
          for (iDim = 0; iDim < nDim; iDim++)
            GridVel2 += GridVel[iDim]*GridVel[iDim];
          for (iVar = 0; iVar < nVar; iVar++)
            for (jVar = 0; jVar < nVar; jVar++)
              Jacobian_i[iVar][jVar] = 0.0;

          Jacobian_i[nDim+1][0] = 0.5*(Gamma-1.0)*GridVel2*ProjGridVel;
          for (jDim = 0; jDim < nDim; jDim++)
            Jacobian_i[nDim+1][jDim+1] = -(Gamma-1.0)*GridVel[jDim]*ProjGridVel;
          Jacobian_i[nDim+1][nDim+1] = (Gamma-1.0)*ProjGridVel;

          /*--- Add the block to the Global Jacobian structure ---*/

          Jacobian.AddBlock2Diag(iPoint, Jacobian_i);

          /*--- Now the Jacobian contribution related to the shear stress ---*/

          for (iVar = 0; iVar < nVar; iVar++)
            for (jVar = 0; jVar < nVar; jVar++)
              Jacobian_i[iVar][jVar] = 0.0;

          factor = total_viscosity*Area/(Density*dist_ij);

          if (nDim == 2) {
            thetax = theta2 + UnitNormal[0]*UnitNormal[0]/3.0;
            thetay = theta2 + UnitNormal[1]*UnitNormal[1]/3.0;

            etaz   = UnitNormal[0]*UnitNormal[1]/3.0;

            pix = GridVel[0]*thetax + GridVel[1]*etaz;
            piy = GridVel[0]*etaz   + GridVel[1]*thetay;

            Jacobian_i[nDim+1][0] -= factor*(-pix*GridVel[0]+piy*GridVel[1]);
            Jacobian_i[nDim+1][1] -= factor*pix;
            Jacobian_i[nDim+1][2] -= factor*piy;
          }
          else {
            thetax = theta2 + UnitNormal[0]*UnitNormal[0]/3.0;
            thetay = theta2 + UnitNormal[1]*UnitNormal[1]/3.0;
            thetaz = theta2 + UnitNormal[2]*UnitNormal[2]/3.0;

            etaz = UnitNormal[0]*UnitNormal[1]/3.0;
            etax = UnitNormal[1]*UnitNormal[2]/3.0;
            etay = UnitNormal[0]*UnitNormal[2]/3.0;

            pix = GridVel[0]*thetax + GridVel[1]*etaz   + GridVel[2]*etay;
            piy = GridVel[0]*etaz   + GridVel[1]*thetay + GridVel[2]*etax;
            piz = GridVel[0]*etay   + GridVel[1]*etax   + GridVel[2]*thetaz;

            Jacobian_i[nDim+1][0] -= factor*(-pix*GridVel[0]+piy*GridVel[1]+piz*GridVel[2]);
            Jacobian_i[nDim+1][1] -= factor*pix;
            Jacobian_i[nDim+1][2] -= factor*piy;
            Jacobian_i[nDim+1][3] -= factor*piz;
          }

          /*--- Subtract the block from the Global Jacobian structure ---*/

          Jacobian.SubtractBlock2Diag(iPoint, Jacobian_i);
        }

      }

      /*--- Convective contribution to the residual at the wall ---*/

      LinSysRes.AddBlock(iPoint, Res_Conv);

      /*--- Viscous contribution to the residual at the wall ---*/

      LinSysRes.SubtractBlock(iPoint, Res_Visc);

      /*--- Enforce the no-slip boundary condition in a strong way by
       modifying the velocity-rows of the Jacobian (1 on the diagonal). ---*/

      if (implicit) {
        for (iVar = 1; iVar <= nDim; iVar++) {
          total_index = iPoint*nVar+iVar;
          Jacobian.DeleteValsRowi(total_index);
        }
      }

    }
  }
}

void CNSSolver::BC_WallModel(CGeometry      *geometry,
                                CSolver        **solver_container,
                                CNumerics      *conv_numerics,
                                CNumerics      *visc_numerics,
                                CConfig        *config,
                                unsigned short val_marker) {

  unsigned short iDim, iVar;
  unsigned long iVertex, iPoint, total_index;

  bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
  bool HeatFlux_Prescribed = false;

  /*--- Allocation of variables necessary for convective fluxes. ---*/
  su2double Area, ProjVelocity_i, Wall_HeatFlux;
  su2double *V_reflected, *V_domain;
  su2double *Normal     = new su2double[nDim];
  su2double *UnitNormal = new su2double[nDim];

  /*--- Identify the boundary by string name ---*/
  string Marker_Tag = config->GetMarker_All_TagBound(val_marker);

  if(config->GetMarker_All_KindBC(val_marker) == HEAT_FLUX) {
    HeatFlux_Prescribed = true;
  }

  /*--- Loop over all the vertices on this boundary marker. ---*/
  for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {

    iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

    /*--- Check if the node belongs to the domain (i.e., not a halo node) ---*/
    if (geometry->node[iPoint]->GetDomain()) {

      /*-------------------------------------------------------------------------------*/
      /*--- Step 1: For the convective fluxes, create a reflected state of the      ---*/
      /*---         Primitive variables by copying all interior values to the       ---*/
      /*---         reflected. Only the velocity is mirrored for the wall model     ---*/
      /*---         and negative for wall functions (weakly impose v = 0)           ---*/
      /*---         axis. Based on the Upwind_Residual routine.                     ---*/
      /*-------------------------------------------------------------------------------*/

      geometry->vertex[val_marker][iVertex]->GetNormal(Normal);

      Area = 0.0;
      for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim];
      Area = sqrt (Area);

      for (iDim = 0; iDim < nDim; iDim++) {
        UnitNormal[iDim] = -Normal[iDim]/Area;
      }

      /*--- Allocate the reflected state at the symmetry boundary. ---*/
      V_reflected = GetCharacPrimVar(val_marker, iVertex);

      /*--- Grid movement ---*/
      if (config->GetGrid_Movement())
        conv_numerics->SetGridVel(geometry->node[iPoint]->GetGridVel(), geometry->node[iPoint]->GetGridVel());

      /*--- Normal vector for this vertex (negate for outward convention). ---*/
      geometry->vertex[val_marker][iVertex]->GetNormal(Normal);
      for (iDim = 0; iDim < nDim; iDim++) Normal[iDim] = -Normal[iDim];
      conv_numerics->SetNormal(Normal);

      /*--- Get current solution at this boundary node ---*/
      V_domain = nodes->GetPrimitive(iPoint);

      /*--- Set the reflected state based on the boundary node. ---*/
      for(iVar = 0; iVar < nPrimVar; iVar++)
        V_reflected[iVar] = nodes->GetPrimitive(iPoint,iVar);

      /*--- Compute velocity in normal direction (ProjVelcity_i=(v*n)) and substract from
       velocity in normal direction: v_r = v - (v*n)n ---*/
      ProjVelocity_i = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
        ProjVelocity_i += nodes->GetVelocity(iPoint,iDim)*UnitNormal[iDim];


      if (nodes->GetTauWall_Flag(iPoint)){
        /*--- Scalars are copied and the velocity is mirrored along the wall boundary,
         i.e. the velocity in normal direction is substracted twice. ---*/

        /*--- Force the velocity to be tangential ---*/
        for (iDim = 0; iDim < nDim; iDim++)
          V_reflected[iDim+1] = nodes->GetVelocity(iPoint,iDim) - 2.0 * ProjVelocity_i*UnitNormal[iDim];

        /*--- Set Primitive and Secondary for numerics class. ---*/

        conv_numerics->SetPrimitive(V_domain, V_reflected);
        conv_numerics->SetSecondary(nodes->GetSecondary(iPoint), nodes->GetSecondary(iPoint));

        /*--- Compute the residual using an upwind scheme. ---*/

        auto residual = conv_numerics->ComputeResidual(config);

        LinSysRes.AddBlock(iPoint, residual);

        /*--- Jacobian contribution for implicit integration. ---*/
        if (implicit)
          Jacobian.AddBlock2Diag(iPoint, residual.jacobian_i);
      }
      else{

        for (iVar = 0; iVar < nVar; iVar++) Res_Conv[iVar] = 0.0;

        /*--- Store the corrected velocity at the wall which will
         be zero (v = 0), unless there are moving walls (v = u_wall)---*/
        /*--- TODO: Before adding the moving walls capability make sure that it is running correctly. ---*/

        for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = 0.0;

        /*--- Impose the value of the velocity as a strong boundary
         condition (Dirichlet). Fix the velocity and remove any
         contribution to the residual at this node. ---*/

        nodes->SetVelocity_Old(iPoint,Vector);

        for (iDim = 0; iDim < nDim; iDim++)
          LinSysRes.SetBlock_Zero(iPoint, iDim+1);
        nodes->SetVel_ResTruncError_Zero(iPoint);

        /*--- Update residual value ---*/
        LinSysRes.AddBlock(iPoint, Res_Conv);

        /*--- Enforce the no-slip boundary condition in a strong way by
         modifying the velocity-rows of the Jacobian (1 on the diagonal). ---*/
        if (implicit){
          for (iVar = 1; iVar <= nDim; iVar++) {
            total_index = iPoint*nVar+iVar;
            Jacobian.DeleteValsRowi(total_index);
          }
        }
      }


      /*-------------------------------------------------------*/
      /*-------------------------------------------------------*/
      /*--- Viscous residual contribution of the wall model ---*/
      /*--- TODO: Build the jacobian contribution of the WM ---*/
      /*-------------------------------------------------------*/
      /*-------------------------------------------------------*/

      if (nodes->GetTauWall_Flag(iPoint)){

        /*--- Weakly enforce the WM heat flux for the energy equation---*/
        su2double velWall_tan = 0.;
        su2double DirTanWM[3] = {0.,0.,0.};

        for (unsigned short iDim = 0; iDim < nDim; iDim++)
          DirTanWM[iDim] = GetFlowDirTan_WMLES(val_marker,iVertex,iDim);

        const su2double TauWall = GetTauWall_WMLES(val_marker,iVertex);
        const su2double Wall_HeatFlux = GetHeatFlux_WMLES(val_marker, iVertex);

        for (unsigned short iDim = 0; iDim < nDim; iDim++)
          velWall_tan +=  nodes->GetVelocity(iPoint,iDim) * DirTanWM[iDim];

        Res_Visc[0] = 0.0;
        Res_Visc[nDim+1] = 0.0;
        for (unsigned short iDim = 0; iDim < nDim; iDim++)
          Res_Visc[iDim+1] = 0.0;

        for (unsigned short iDim = 0; iDim < nDim; iDim++)
          Res_Visc[iDim+1] = - TauWall * DirTanWM[iDim] * Area;

        Res_Visc[nDim+1] = (Wall_HeatFlux - TauWall * velWall_tan) * Area;
      }
      else{

        /*--- If it is isothermal wall, calculate the heat flux---*/
        if (!HeatFlux_Prescribed){

          su2double Prandtl_Lam  = config->GetPrandtl_Lam();
          su2double Prandtl_Turb = config->GetPrandtl_Turb();
          su2double Gas_Constant = config->GetGas_ConstantND();
          su2double Cp = (Gamma / Gamma_Minus_One) * Gas_Constant;

          /*--- Retrieve the specified wall temperature from config
                as well as the wall function treatment.---*/
          su2double Twall = config->GetIsothermal_Temperature(Marker_Tag)/config->GetTemperature_Ref();

          /*--- Compute closest normal neighbor ---*/
          unsigned long Point_Normal = geometry->vertex[val_marker][iVertex]->GetNormal_Neighbor();

          /*--- Get coordinates of i & nearest normal and compute distance ---*/
          su2double *Coord_i = geometry->node[iPoint]->GetCoord();
          su2double *Coord_j = geometry->node[Point_Normal]->GetCoord();
          su2double dist_ij = 0;
          for (iDim = 0; iDim < nDim; iDim++)
            dist_ij += (Coord_j[iDim]-Coord_i[iDim])*(Coord_j[iDim]-Coord_i[iDim]);
          dist_ij = sqrt(dist_ij);


          /*--- Compute the normal gradient in temperature using Twall ---*/
          su2double dTdn = -(nodes->GetTemperature(Point_Normal) - Twall)/dist_ij;

          /*--- Get transport coefficients ---*/
          su2double laminar_viscosity    = nodes->GetLaminarViscosity(iPoint);
          su2double eddy_viscosity       = nodes->GetEddyViscosity(iPoint);
          su2double thermal_conductivity = Cp * ( laminar_viscosity/Prandtl_Lam + eddy_viscosity/Prandtl_Turb);

          Wall_HeatFlux = thermal_conductivity * dTdn;
        }else{

          Wall_HeatFlux = config->GetWall_HeatFlux(Marker_Tag) /config->GetHeat_Flux_Ref();
        }
        for (iVar = 0; iVar < nVar; iVar++) Res_Visc[iVar] = 0.0;

        /*--- Weakly impose the WM heat flux for the energy equation---*/
        Res_Visc[nDim+1] = Wall_HeatFlux * Area;

      }

      LinSysRes.SubtractBlock(iPoint, Res_Visc);

    }
  }

  /*--- Free locally allocated memory ---*/
  delete [] Normal;
  delete [] UnitNormal;

}

void CNSSolver::SetRoe_Dissipation(CGeometry *geometry, CConfig *config){

  const unsigned short kind_roe_dissipation = config->GetKind_RoeLowDiss();

  SU2_OMP_FOR_STAT(omp_chunk_size)
  for (unsigned long iPoint = 0; iPoint < nPoint; iPoint++) {

    if (kind_roe_dissipation == FD || kind_roe_dissipation == FD_DUCROS){

      su2double wall_distance = geometry->node[iPoint]->GetWall_Distance();

      nodes->SetRoe_Dissipation_FD(iPoint, wall_distance);

    } else if (kind_roe_dissipation == NTS || kind_roe_dissipation == NTS_DUCROS) {

      const su2double delta = geometry->node[iPoint]->GetMaxLength();
      assert(delta > 0 && "Delta must be initialized and non-negative");
      nodes->SetRoe_Dissipation_NTS(iPoint, delta, config->GetConst_DES());
    }
  }

}

void CNSSolver::BC_ConjugateHeat_Interface(CGeometry *geometry, CSolver **solver_container, CNumerics *conv_numerics,
                                           CConfig *config, unsigned short val_marker) {

  unsigned short iVar, jVar, iDim, jDim;
  unsigned long iVertex, iPoint, Point_Normal, total_index;

  su2double *Normal, *Coord_i, *Coord_j, Area, dist_ij, theta2;
  su2double Twall= 0.0, There, dTdn= 0.0, dTdrho, thermal_conductivity, Tconjugate, HF_FactorHere, HF_FactorConjugate;
  su2double thetax, thetay, thetaz, etax, etay, etaz, pix, piy, piz, factor;
  su2double ProjGridVel, *GridVel, GridVel2, Pressure = 0.0, Density, Vel2;
  su2double total_viscosity, div_vel, tau_vel[3] = {0.0,0.0,0.0}, UnitNormal[3] = {0.0,0.0,0.0};
  su2double laminar_viscosity, eddy_viscosity, Grad_Vel[3][3] = {{1.0, 0.0, 0.0},{0.0,1.0,0.0},{0.0,0.0,1.0}},
  tau[3][3] = {{0.0, 0.0, 0.0},{0.0,0.0,0.0},{0.0,0.0,0.0}}, delta[3][3] = {{1.0, 0.0, 0.0},{0.0,1.0,0.0},{0.0,0.0,1.0}};

  su2double Prandtl_Lam  = config->GetPrandtl_Lam();
  su2double Prandtl_Turb = config->GetPrandtl_Turb();
  su2double Gas_Constant = config->GetGas_ConstantND();
  su2double Cp = (Gamma / Gamma_Minus_One) * Gas_Constant;

  su2double Temperature_Ref = config->GetTemperature_Ref();

  bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);

  /*--- Identify the boundary ---*/

  string Marker_Tag = config->GetMarker_All_TagBound(val_marker);

//  /*--- Retrieve the specified wall function treatment.---*/
//
//  Wall_Function = config->GetWallFunction_Treatment(Marker_Tag);
//  if (Wall_Function != NO_WALL_FUNCTION) {
//      SU2_MPI::Error("Wall function treament not implemented yet", CURRENT_FUNCTION);
//  }

  /*--- Loop over boundary points ---*/

  for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {

    iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

    if (geometry->node[iPoint]->GetDomain()) {

      /*--- Compute dual-grid area and boundary normal ---*/

      Normal = geometry->vertex[val_marker][iVertex]->GetNormal();

      Area = 0.0; for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim]; Area = sqrt (Area);

      for (iDim = 0; iDim < nDim; iDim++)
        UnitNormal[iDim] = -Normal[iDim]/Area;

      /*--- Calculate useful quantities ---*/

      theta2 = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
        theta2 += UnitNormal[iDim]*UnitNormal[iDim];

      /*--- Compute closest normal neighbor ---*/

      Point_Normal = geometry->vertex[val_marker][iVertex]->GetNormal_Neighbor();

      /*--- Get coordinates of i & nearest normal and compute distance ---*/

      Coord_i = geometry->node[iPoint]->GetCoord();
      Coord_j = geometry->node[Point_Normal]->GetCoord();
      dist_ij = 0;
      for (iDim = 0; iDim < nDim; iDim++)
        dist_ij += (Coord_j[iDim]-Coord_i[iDim])*(Coord_j[iDim]-Coord_i[iDim]);
      dist_ij = sqrt(dist_ij);

      /*--- Store the corrected velocity at the wall which will
       be zero (v = 0), unless there is grid motion (v = u_wall)---*/

      if (dynamic_grid) {
        GridVel = geometry->node[iPoint]->GetGridVel();
        for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = GridVel[iDim];
      }
      else {
        for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = 0.0;
      }

      /*--- Initialize the convective & viscous residuals to zero ---*/

      for (iVar = 0; iVar < nVar; iVar++) {
        Res_Conv[iVar] = 0.0;
        Res_Visc[iVar] = 0.0;
      }

      /*--- Set the residual, truncation error and velocity value on the boundary ---*/

      nodes->SetVelocity_Old(iPoint,Vector);

      for (iDim = 0; iDim < nDim; iDim++)
        LinSysRes.SetBlock_Zero(iPoint, iDim+1);
      nodes->SetVel_ResTruncError_Zero(iPoint);

      /*--- Get transport coefficients ---*/

      laminar_viscosity    = nodes->GetLaminarViscosity(iPoint);
      eddy_viscosity       = nodes->GetEddyViscosity(iPoint);
      thermal_conductivity = Cp * ( laminar_viscosity/Prandtl_Lam + eddy_viscosity/Prandtl_Turb);

      // work in progress on real-gases...
      //thermal_conductivity = nodes->GetThermalConductivity(iPoint);
      //Cp = nodes->GetSpecificHeatCp(iPoint);
      //thermal_conductivity += Cp*eddy_viscosity/Prandtl_Turb;

      /*--- Compute the normal gradient in temperature using Twall ---*/

      There = nodes->GetTemperature(Point_Normal);
      Tconjugate = GetConjugateHeatVariable(val_marker, iVertex, 0)/Temperature_Ref;

      if ((config->GetKind_CHT_Coupling() == AVERAGED_TEMPERATURE_NEUMANN_HEATFLUX) ||
          (config->GetKind_CHT_Coupling() == AVERAGED_TEMPERATURE_ROBIN_HEATFLUX)) {

        /*--- Compute wall temperature from both temperatures ---*/

        HF_FactorHere = thermal_conductivity*config->GetViscosity_Ref()/dist_ij;
        HF_FactorConjugate = GetConjugateHeatVariable(val_marker, iVertex, 2);

        Twall = (There*HF_FactorHere + Tconjugate*HF_FactorConjugate)/(HF_FactorHere + HF_FactorConjugate);
        dTdn = -(There - Twall)/dist_ij;
      }
      else if ((config->GetKind_CHT_Coupling() == DIRECT_TEMPERATURE_NEUMANN_HEATFLUX) ||
              (config->GetKind_CHT_Coupling() == DIRECT_TEMPERATURE_ROBIN_HEATFLUX)) {

        /*--- (Directly) Set wall temperature to conjugate temperature. ---*/

        Twall = Tconjugate;
        dTdn = -(There - Twall)/dist_ij;
      }
      else {
        Twall = dTdn = 0.0;
        SU2_MPI::Error("Unknown CHT coupling method.", CURRENT_FUNCTION);
      }

      /*--- Apply a weak boundary condition for the energy equation.
       Compute the residual due to the prescribed heat flux. ---*/

      Res_Visc[nDim+1] = thermal_conductivity * dTdn * Area;

      /*--- Calculate Jacobian for implicit time stepping ---*/

      if (implicit) {

        for (iVar = 0; iVar < nVar; iVar ++)
          for (jVar = 0; jVar < nVar; jVar ++)
            Jacobian_i[iVar][jVar] = 0.0;

        /*--- Calculate useful quantities ---*/

        Density = nodes->GetDensity(iPoint);
        Vel2 = 0.0;
        for (iDim = 0; iDim < nDim; iDim++)
          Vel2 += pow(nodes->GetVelocity(iPoint,iDim),2);
        dTdrho = 1.0/Density * ( -Twall + (Gamma-1.0)/Gas_Constant*(Vel2/2.0) );

        /*--- Enforce the no-slip boundary condition in a strong way ---*/

        for (iVar = 1; iVar <= nDim; iVar++) {
          total_index = iPoint*nVar+iVar;
          Jacobian.DeleteValsRowi(total_index);
        }

        /*--- Add contributions to the Jacobian from the weak enforcement of the energy equations ---*/

        Jacobian_i[nDim+1][0]      = -thermal_conductivity*theta2/dist_ij * dTdrho * Area;
        Jacobian_i[nDim+1][nDim+1] = -thermal_conductivity*theta2/dist_ij * (Gamma-1.0)/(Gas_Constant*Density) * Area;

        /*--- Subtract the block from the Global Jacobian structure ---*/

        Jacobian.SubtractBlock2Diag(iPoint, Jacobian_i);

      }

      /*--- If the wall is moving, there are additional residual contributions
       due to pressure (p v_wall.n) and shear stress (tau.v_wall.n). ---*/

      if (dynamic_grid) {

        /*--- Get the grid velocity at the current boundary node ---*/

        GridVel = geometry->node[iPoint]->GetGridVel();
        ProjGridVel = 0.0;
        for (iDim = 0; iDim < nDim; iDim++)
          ProjGridVel += GridVel[iDim]*UnitNormal[iDim]*Area;

        /*--- Retrieve other primitive quantities and viscosities ---*/

        Density  = nodes->GetDensity(iPoint);
        Pressure = nodes->GetPressure(iPoint);
        laminar_viscosity = nodes->GetLaminarViscosity(iPoint);
        eddy_viscosity    = nodes->GetEddyViscosity(iPoint);

        total_viscosity   = laminar_viscosity + eddy_viscosity;

        for (iDim = 0; iDim < nDim; iDim++) {
          for (jDim = 0 ; jDim < nDim; jDim++) {
            Grad_Vel[iDim][jDim] = nodes->GetGradient_Primitive(iPoint,iDim+1, jDim);
          }
        }

        /*--- Divergence of the velocity ---*/

        div_vel = 0.0; for (iDim = 0 ; iDim < nDim; iDim++) div_vel += Grad_Vel[iDim][iDim];

        /*--- Compute the viscous stress tensor ---*/

        for (iDim = 0; iDim < nDim; iDim++)
          for (jDim = 0; jDim < nDim; jDim++) {
            tau[iDim][jDim] = total_viscosity*( Grad_Vel[jDim][iDim] + Grad_Vel[iDim][jDim] )
                              - TWO3*total_viscosity*div_vel*delta[iDim][jDim];
          }

        /*--- Dot product of the stress tensor with the grid velocity ---*/

        for (iDim = 0 ; iDim < nDim; iDim++) {
          tau_vel[iDim] = 0.0;
          for (jDim = 0 ; jDim < nDim; jDim++)
            tau_vel[iDim] += tau[iDim][jDim]*GridVel[jDim];
        }

        /*--- Compute the convective and viscous residuals (energy eqn.) ---*/

        Res_Conv[nDim+1] = Pressure*ProjGridVel;
        for (iDim = 0 ; iDim < nDim; iDim++)
          Res_Visc[nDim+1] += tau_vel[iDim]*UnitNormal[iDim]*Area;

        /*--- Implicit Jacobian contributions due to moving walls ---*/

        if (implicit) {

          /*--- Jacobian contribution related to the pressure term ---*/

          GridVel2 = 0.0;
          for (iDim = 0; iDim < nDim; iDim++)
            GridVel2 += GridVel[iDim]*GridVel[iDim];
          for (iVar = 0; iVar < nVar; iVar++)
            for (jVar = 0; jVar < nVar; jVar++)
              Jacobian_i[iVar][jVar] = 0.0;

          Jacobian_i[nDim+1][0] = 0.5*(Gamma-1.0)*GridVel2*ProjGridVel;
          for (jDim = 0; jDim < nDim; jDim++)
            Jacobian_i[nDim+1][jDim+1] = -(Gamma-1.0)*GridVel[jDim]*ProjGridVel;
          Jacobian_i[nDim+1][nDim+1] = (Gamma-1.0)*ProjGridVel;

          /*--- Add the block to the Global Jacobian structure ---*/

          Jacobian.AddBlock2Diag(iPoint, Jacobian_i);

          /*--- Now the Jacobian contribution related to the shear stress ---*/

          for (iVar = 0; iVar < nVar; iVar++)
            for (jVar = 0; jVar < nVar; jVar++)
              Jacobian_i[iVar][jVar] = 0.0;

          factor = total_viscosity*Area/(Density*dist_ij);

          if (nDim == 2) {
            thetax = theta2 + UnitNormal[0]*UnitNormal[0]/3.0;
            thetay = theta2 + UnitNormal[1]*UnitNormal[1]/3.0;

            etaz   = UnitNormal[0]*UnitNormal[1]/3.0;

            pix = GridVel[0]*thetax + GridVel[1]*etaz;
            piy = GridVel[0]*etaz   + GridVel[1]*thetay;

            Jacobian_i[nDim+1][0] -= factor*(-pix*GridVel[0]+piy*GridVel[1]);
            Jacobian_i[nDim+1][1] -= factor*pix;
            Jacobian_i[nDim+1][2] -= factor*piy;
          }
          else {
            thetax = theta2 + UnitNormal[0]*UnitNormal[0]/3.0;
            thetay = theta2 + UnitNormal[1]*UnitNormal[1]/3.0;
            thetaz = theta2 + UnitNormal[2]*UnitNormal[2]/3.0;

            etaz = UnitNormal[0]*UnitNormal[1]/3.0;
            etax = UnitNormal[1]*UnitNormal[2]/3.0;
            etay = UnitNormal[0]*UnitNormal[2]/3.0;

            pix = GridVel[0]*thetax + GridVel[1]*etaz   + GridVel[2]*etay;
            piy = GridVel[0]*etaz   + GridVel[1]*thetay + GridVel[2]*etax;
            piz = GridVel[0]*etay   + GridVel[1]*etax   + GridVel[2]*thetaz;

            Jacobian_i[nDim+1][0] -= factor*(-pix*GridVel[0]+piy*GridVel[1]+piz*GridVel[2]);
            Jacobian_i[nDim+1][1] -= factor*pix;
            Jacobian_i[nDim+1][2] -= factor*piy;
            Jacobian_i[nDim+1][3] -= factor*piz;
          }

          /*--- Subtract the block from the Global Jacobian structure ---*/

          Jacobian.SubtractBlock2Diag(iPoint, Jacobian_i);
        }

      }

      /*--- Convective contribution to the residual at the wall ---*/

      LinSysRes.AddBlock(iPoint, Res_Conv);

      /*--- Viscous contribution to the residual at the wall ---*/

      LinSysRes.SubtractBlock(iPoint, Res_Visc);

      /*--- Enforce the no-slip boundary condition in a strong way by
       modifying the velocity-rows of the Jacobian (1 on the diagonal). ---*/

      if (implicit) {
        for (iVar = 1; iVar <= nDim; iVar++) {
          total_index = iPoint*nVar+iVar;
          Jacobian.DeleteValsRowi(total_index);
        }
      }
    }
  }
}

void CNSSolver::SetTauWall_WF(CGeometry *geometry, CSolver **solver_container, CConfig *config) {

  /*---  The wall function implemented herein is based on Nichols and Nelson AIAAJ v32 n6 2004.
   At this moment, the wall function is only available for adiabatic flows.
   ---*/

  bool debug = false;
  unsigned short iDim, jDim, iMarker;
  unsigned long iVertex, iPoint, Point_Normal, counter;

  su2double Area, div_vel, UnitNormal[3], *Normal;
  su2double **grad_primvar, tau[3][3];

  su2double Vel[3], VelNormal, VelTang[3], VelTangMod, VelInfMod, WallDist[3], WallDistMod;
  su2double T_Normal, P_Normal;
  su2double Density_Wall, T_Wall, P_Wall, Lam_Visc_Wall, Tau_Wall = 0.0;
  su2double *Coord, *Coord_Normal;
  su2double diff, Delta, grad_diff;
  su2double U_Tau, U_Plus, Gam, Beta, Phi, Q, Y_Plus_White, Y_Plus;
  su2double TauElem[3], TauNormal, TauTangent[3], WallShearStress;
  su2double Gas_Constant = config->GetGas_ConstantND();
  su2double Cp = (Gamma / Gamma_Minus_One) * Gas_Constant;

  unsigned short max_iter = 50;
  su2double tol = 1e-6;

  /*--- Get the freestream velocity magnitude for non-dim. purposes ---*/

  su2double *VelInf = config->GetVelocity_FreeStreamND();
  VelInfMod = 0.0;
  for (iDim = 0; iDim < nDim; iDim++)
    VelInfMod += VelInf[iDim];
  VelInfMod = sqrt(VelInfMod);

  /*--- Compute the recovery factor ---*/
  // Double-check: laminar or turbulent Pr for this?
  su2double Recovery = pow(config->GetPrandtl_Lam(), (1.0/3.0));

  /*--- Typical constants from boundary layer theory ---*/

  su2double kappa = 0.41;
  su2double B = 5.0;
  bool converged = true;

  for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++) {

    if ((config->GetMarker_All_KindBC(iMarker) == HEAT_FLUX) ||
        (config->GetMarker_All_KindBC(iMarker) == ISOTHERMAL) ) {

      /*--- Identify the boundary by string name ---*/

      string Marker_Tag = config->GetMarker_All_TagBound(iMarker);

      /*--- Jump to another BC if it is not wall function ---*/

      if (config->GetWallFunction_Treatment(Marker_Tag) != STANDARD_WALL_FUNCTION)
        continue;

      /*--- Get the specified wall heat flux from config ---*/

      // Wall_HeatFlux = config->GetWall_HeatFlux(Marker_Tag);

      /*--- Loop over all of the vertices on this boundary marker ---*/

      for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {

        iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
        Point_Normal = geometry->vertex[iMarker][iVertex]->GetNormal_Neighbor();

        /*--- Check if the node belongs to the domain (i.e, not a halo node)
         and the neighbor is not part of the physical boundary ---*/

        if (geometry->node[iPoint]->GetDomain()) {

          /*--- Get coordinates of the current vertex and nearest normal point ---*/

          Coord = geometry->node[iPoint]->GetCoord();
          Coord_Normal = geometry->node[Point_Normal]->GetCoord();

          /*--- Compute dual-grid area and boundary normal ---*/

          Normal = geometry->vertex[iMarker][iVertex]->GetNormal();

          Area = 0.0;
          for (iDim = 0; iDim < nDim; iDim++)
            Area += Normal[iDim]*Normal[iDim];
          Area = sqrt (Area);

          for (iDim = 0; iDim < nDim; iDim++)
            UnitNormal[iDim] = -Normal[iDim]/Area;

          /*--- Get the velocity, pressure, and temperature at the nearest
           (normal) interior point. ---*/

          for (iDim = 0; iDim < nDim; iDim++)
            Vel[iDim] = nodes->GetVelocity(Point_Normal,iDim);
          P_Normal = nodes->GetPressure(Point_Normal);
          T_Normal = nodes->GetTemperature(Point_Normal);

          /*--- Compute the wall-parallel velocity at first point off the wall ---*/

          VelNormal = 0.0;
          for (iDim = 0; iDim < nDim; iDim++)
            VelNormal += Vel[iDim] * UnitNormal[iDim];
          for (iDim = 0; iDim < nDim; iDim++)
            VelTang[iDim] = Vel[iDim] - VelNormal*UnitNormal[iDim];

          VelTangMod = 0.0;
          for (iDim = 0; iDim < nDim; iDim++)
            VelTangMod += VelTang[iDim]*VelTang[iDim];
          VelTangMod = sqrt(VelTangMod);

          /*--- Compute normal distance of the interior point from the wall ---*/

          for (iDim = 0; iDim < nDim; iDim++)
            WallDist[iDim] = (Coord[iDim] - Coord_Normal[iDim]);

          WallDistMod = 0.0;
          for (iDim = 0; iDim < nDim; iDim++)
            WallDistMod += WallDist[iDim]*WallDist[iDim];
          WallDistMod = sqrt(WallDistMod);

          /*--- Compute mach number ---*/

          // M_Normal = VelTangMod / sqrt(Gamma * Gas_Constant * T_Normal);

          /*--- Compute the wall temperature using the Crocco-Buseman equation ---*/

          //T_Wall = T_Normal * (1.0 + 0.5*Gamma_Minus_One*Recovery*M_Normal*M_Normal);
          T_Wall = T_Normal + Recovery*pow(VelTangMod,2.0)/(2.0*Cp);

          /*--- Extrapolate the pressure from the interior & compute the
           wall density using the equation of state ---*/

          P_Wall = P_Normal;
          Density_Wall = P_Wall/(Gas_Constant*T_Wall);
          Lam_Visc_Wall = nodes->GetLaminarViscosity(iPoint);

          /*--- Compute the shear stress at the wall in the regular fashion
           by using the stress tensor on the surface ---*/

          grad_primvar  = nodes->GetGradient_Primitive(iPoint);

          div_vel = 0.0;
          for (iDim = 0; iDim < nDim; iDim++)
            div_vel += grad_primvar[iDim+1][iDim];

          for (iDim = 0; iDim < nDim; iDim++) {
            for (jDim = 0 ; jDim < nDim; jDim++) {
              Delta = 0.0; if (iDim == jDim) Delta = 1.0;
              tau[iDim][jDim] = Lam_Visc_Wall*(  grad_primvar[jDim+1][iDim]
                                               + grad_primvar[iDim+1][jDim]) -
              TWO3*Lam_Visc_Wall*div_vel*Delta;
            }
            TauElem[iDim] = 0.0;
            for (jDim = 0; jDim < nDim; jDim++)
              TauElem[iDim] += tau[iDim][jDim]*UnitNormal[jDim];
          }

          /*--- Compute wall shear stress as the magnitude of the wall-tangential
           component of the shear stress tensor---*/

          TauNormal = 0.0;
          for (iDim = 0; iDim < nDim; iDim++)
            TauNormal += TauElem[iDim] * UnitNormal[iDim];

          for (iDim = 0; iDim < nDim; iDim++)
            TauTangent[iDim] = TauElem[iDim] - TauNormal * UnitNormal[iDim];

          WallShearStress = 0.0;
          for (iDim = 0; iDim < nDim; iDim++)
            WallShearStress += TauTangent[iDim]*TauTangent[iDim];
          WallShearStress = sqrt(WallShearStress);

          /*--- Calculate the quantities from boundary layer theory and
           iteratively solve for a new wall shear stress. Use the current wall
           shear stress as a starting guess for the wall function. ---*/

          counter = 0; diff = 1.0;
          U_Tau = sqrt(WallShearStress/Density_Wall);
          Y_Plus = 0.0; // to avoid warning

          su2double Y_Plus_Start = Density_Wall * U_Tau * WallDistMod / Lam_Visc_Wall;

          /*--- Automatic switch off when y+ < 5 according to Nichols & Nelson (2004) ---*/

          if (Y_Plus_Start < 5.0) {
            nodes->SetTauWall_Flag(iPoint,false);
            continue;
          }

          while (fabs(diff) > tol) {

            /*--- Friction velocity and u+ ---*/

            U_Plus = VelTangMod/U_Tau;

            /*--- Gamma, Beta, Q, and Phi, defined by Nichols & Nelson (2004) ---*/

            Gam  = Recovery*U_Tau*U_Tau/(2.0*Cp*T_Wall);
            Beta = 0.0; // For adiabatic flows only
            Q    = sqrt(Beta*Beta + 4.0*Gam);
            Phi  = asin(-1.0*Beta/Q);

            /*--- Y+ defined by White & Christoph (compressibility and heat transfer) negative value for (2.0*Gam*U_Plus - Beta)/Q ---*/

            Y_Plus_White = exp((kappa/sqrt(Gam))*(asin((2.0*Gam*U_Plus - Beta)/Q) - Phi))*exp(-1.0*kappa*B);

            /*--- Spalding's universal form for the BL velocity with the
             outer velocity form of White & Christoph above. ---*/

            Y_Plus = U_Plus + Y_Plus_White - (exp(-1.0*kappa*B)*
                                              (1.0 + kappa*U_Plus + kappa*kappa*U_Plus*U_Plus/2.0 +
                                               kappa*kappa*kappa*U_Plus*U_Plus*U_Plus/6.0));

            /* --- Define function for Newton method to zero --- */

            diff = (Density_Wall * U_Tau * WallDistMod / Lam_Visc_Wall) - Y_Plus;

            /* --- Gradient of function defined above --- */
            grad_diff = Density_Wall * WallDistMod / Lam_Visc_Wall + VelTangMod / (U_Tau * U_Tau) +
                      kappa /(U_Tau * sqrt(Gam)) * asin(U_Plus * sqrt(Gam)) * Y_Plus_White -
                      exp(-1.0 * B * kappa) * (0.5 * pow(VelTangMod * kappa / U_Tau, 3) +
                      pow(VelTangMod * kappa / U_Tau, 2) + VelTangMod * kappa / U_Tau) / U_Tau;

            /* --- Newton Step --- */

            U_Tau = U_Tau - diff / grad_diff;

            counter++;
            if (counter == max_iter) {
              if (debug){
                cout << "WARNING: Y_Plus evaluation has not converged in solver_direct_mean.cpp" << endl;
                cout << rank << " " << iPoint;
                for (iDim = 0; iDim < nDim; iDim++)
                  cout << " " << Coord[iDim];
                cout << endl;
              }
              converged = false;
              nodes->SetTauWall_Flag(iPoint,false);
              break;
            }

          }
          /* --- If not converged jump to the next point. --- */

          if (!converged) continue;

          /*--- Calculate an updated value for the wall shear stress
            using the y+ value, the definition of y+, and the definition of
            the friction velocity. ---*/

          Tau_Wall = (1.0/Density_Wall)*pow(Y_Plus*Lam_Visc_Wall/WallDistMod,2.0);

          /*--- Store this value for the wall shear stress at the node.  ---*/

          nodes->SetTauWall_Flag(iPoint,true);
          nodes->SetTauWall(iPoint,Tau_Wall);
          nodes->SetTemperature(iPoint,T_Wall);
          nodes->SetSolution(iPoint, 0, Density_Wall);
          nodes->SetPrimitive(iPoint, nDim + 1, P_Wall);

        }
      }
    }
  }

}
void CNSSolver::SetEddyViscFirstPoint(CGeometry *geometry, CSolver **solver_container, CConfig *config) {

  unsigned short iDim;
  unsigned long iPoint, iVertex, Point_Normal;
  su2double Lam_Visc_Normal, Vel[3] = {0.,0.,0.}, P_Normal, T_Normal;
  su2double VelTang[3] = {0.,0.,0.}, VelTangMod, VelNormal;
  su2double WallDist[3] = {0.,0.,0.}, WallDistMod;
  su2double *Coord, *Coord_Normal, Tau_Wall;
  su2double *Normal, Area, UnitNormal[3] = {0.,0.,0.};
  su2double T_Wall, P_Wall, Density_Wall, Lam_Visc_Wall;
  su2double dypw_dyp, Y_Plus_White, Eddy_Visc;
  su2double U_Plus, U_Tau, Gam, Beta, Q, Phi;
  su2double Gas_Constant = config->GetGas_ConstantND();
  su2double Cp = (Gamma / Gamma_Minus_One) * Gas_Constant;

  /*--- Typical constants from boundary layer theory ---*/

  su2double kappa = 0.41;
  su2double B = 5.0;

  /*--- Compute the recovery factor ---*/
  // su2double-check: laminar or turbulent Pr for this?
  su2double Recovery = pow(config->GetPrandtl_Lam(),(1.0/3.0));

  /* Loop over the markers and select the ones for which a wall model
    treatment is carried out. */

  for(unsigned short iMarker=0; iMarker<nMarker; ++iMarker) {
    switch (config->GetMarker_All_KindBC(iMarker)) {
      case ISOTHERMAL:
      case HEAT_FLUX:{
        const string Marker_Tag = config->GetMarker_All_TagBound(iMarker);

        /* Set the Eddy Viscosity at the first point off the wall */

        if ((config->GetWallFunction_Treatment(Marker_Tag) == STANDARD_WALL_FUNCTION)){

          for (iVertex = 0; iVertex < nVertex[iMarker]; iVertex++) {
            iPoint       = geometry->vertex[iMarker][iVertex]->GetNode();
            Point_Normal = geometry->vertex[iMarker][iVertex]->GetNormal_Neighbor();

            Coord        = geometry->node[iPoint]->GetCoord();
            Coord_Normal = geometry->node[Point_Normal]->GetCoord();

            /*--- Compute dual-grid area and boundary normal ---*/

            Normal = geometry->vertex[iMarker][iVertex]->GetNormal();

            Area = 0.0;
            for (iDim = 0; iDim < nDim; iDim++)
              Area += Normal[iDim]*Normal[iDim];
            Area = sqrt (Area);

            for (iDim = 0; iDim < nDim; iDim++)
              UnitNormal[iDim] = -Normal[iDim]/Area;

            /*--- Check if the node belongs to the domain (i.e, not a halo node)
             and the neighbor is not part of the physical boundary ---*/

            if (geometry->node[iPoint]->GetDomain()) {

              Tau_Wall = nodes->GetTauWall(iPoint);

              /*--- Verify the wall function flag on the node. If false,
               jump to the next iPoint.---*/

              if (!nodes->GetTauWall_Flag(iPoint)) continue;

              /*--- Get the velocity, pressure, and temperature at the nearest
               (normal) interior point. ---*/

              for (iDim = 0; iDim < nDim; iDim++)
                Vel[iDim]    = nodes->GetVelocity(Point_Normal,iDim);
              P_Normal       = nodes->GetPressure(Point_Normal);
              T_Normal       = nodes->GetTemperature(Point_Normal);

              /*--- Compute the wall-parallel velocity at first point off the wall ---*/

              VelNormal = 0.0;
              for (iDim = 0; iDim < nDim; iDim++)
                VelNormal += Vel[iDim] * UnitNormal[iDim];
              for (iDim = 0; iDim < nDim; iDim++)
                VelTang[iDim] = Vel[iDim] - VelNormal*UnitNormal[iDim];

              VelTangMod = 0.0;
              for (iDim = 0; iDim < nDim; iDim++)
                VelTangMod += VelTang[iDim]*VelTang[iDim];
              VelTangMod = sqrt(VelTangMod);

              /*--- Compute normal distance of the interior point from the wall ---*/

              for (iDim = 0; iDim < nDim; iDim++)
                WallDist[iDim] = (Coord[iDim] - Coord_Normal[iDim]);

              WallDistMod = 0.0;
              for (iDim = 0; iDim < nDim; iDim++)
                WallDistMod += WallDist[iDim]*WallDist[iDim];
              WallDistMod = sqrt(WallDistMod);

              /*--- Compute the wall temperature using the Crocco-Buseman equation ---*/

              //T_Wall = T_Normal * (1.0 + 0.5*Gamma_Minus_One*Recovery*M_Normal*M_Normal);
              T_Wall = T_Normal + Recovery*pow(VelTangMod,2.0)/(2.0*Cp);

              /*--- Extrapolate the pressure from the interior & compute the
               wall density using the equation of state ---*/

              P_Wall       = P_Normal;
              Density_Wall = P_Wall/(Gas_Constant*T_Wall);
              Lam_Visc_Wall = nodes->GetLaminarViscosity(iPoint);

              /*--- Friction velocity and u+ ---*/

              U_Tau  = sqrt(Tau_Wall / Density_Wall);
              U_Plus = VelTangMod/U_Tau;

              Gam  = Recovery*U_Tau*U_Tau/(2.0*Cp*T_Wall);
              Beta = 0.0; // For adiabatic flows only
              Q    = sqrt(Beta*Beta + 4.0*Gam);
              Phi  = asin(-1.0*Beta/Q);

              /*--- Y+ defined by White & Christoph (compressibility and heat transfer) ---*/

              Y_Plus_White = exp((kappa/sqrt(Gam))*(asin((2.0*Gam*U_Plus - Beta)/Q) - Phi))*exp(-1.0*kappa*B);

              /*--- If the Y+ defined by White & Christoph is too high so the eddy viscosity.
                Disable the wall function calculation on this point and do not set the eddy viscosity
                at the first point off the wall. ---*/

              if (Y_Plus_White > 1e4){
                cout << "WARNING: Y+ is too high (>1e4). The muT computation at the 1st point off the wall is disable." << endl;
                cout << rank << " " << iPoint;
                for (iDim = 0; iDim < nDim; iDim++)
                  cout << " " << Coord[iDim];
                cout << endl;
                nodes->SetTauWall_Flag(iPoint,false);
                continue;
              }

              /*--- Now compute the Eddy viscosity at the first point off of the wall ---*/

              Lam_Visc_Normal = nodes->GetLaminarViscosity(Point_Normal);

              dypw_dyp = 2.0*Y_Plus_White*(kappa*sqrt(Gam)/Q)*pow(1.0 - pow(2.0*Gam*U_Plus - Beta,2.0)/(Q*Q), -0.5);

              Eddy_Visc = Lam_Visc_Wall*(1.0 + dypw_dyp - kappa*exp(-1.0*kappa*B)*
                                         (1.0 + kappa*U_Plus + kappa*kappa*U_Plus*U_Plus/2.0)
                                         - Lam_Visc_Normal/Lam_Visc_Wall);

              nodes->SetEddyViscosity(Point_Normal,Eddy_Visc);

            }
          }
        }
      }
        break;
      default:
        break;
    }
  }
}
void CNSSolver::Setmut_LES(CGeometry *geometry, CSolver **solver_container, CConfig *config) {

  unsigned long iPoint;
  su2double Grad_Vel[3][3] = {{0.0,0.0,0.0},{0.0,0.0,0.0},{0.0,0.0,0.0}};
  su2double lenScale, muTurb, rho;

  for (iPoint = 0; iPoint < nPoint; iPoint++){

    /* Get Density */
    rho = nodes->GetSolution(iPoint, 0);

    /* Velocity Gradients */
    for (unsigned short iDim = 0; iDim < nDim; iDim++)
      for (unsigned short jDim = 0 ; jDim < nDim; jDim++)
        Grad_Vel[iDim][jDim] = nodes->GetGradient_Primitive(iPoint, iDim+1, jDim);

    /* Distance to the wall. */
    su2double dist = geometry->node[iPoint]->GetWall_Distance(); // Is the distance to the wall used in any SGS calculation?

    /* Length Scale for the SGS model: Cubic root of the volume. */
    su2double Vol = geometry->node[iPoint]->GetVolume() + geometry->node[iPoint]->GetPeriodicVolume();
    lenScale = pow(Vol,1./3.);

    /* Compute the eddy viscosity. */
    if (nDim == 2){
      muTurb = SGSModel->ComputeEddyViscosity_2D(rho, Grad_Vel[0][0], Grad_Vel[1][0],
                                                 Grad_Vel[0][1], Grad_Vel[1][1],
                                                 lenScale, dist);
    }
    else{
      muTurb = SGSModel->ComputeEddyViscosity_3D(rho, Grad_Vel[0][0], Grad_Vel[1][0], Grad_Vel[2][0],
                                               Grad_Vel[0][1], Grad_Vel[1][1], Grad_Vel[2][1],
                                               Grad_Vel[0][2], Grad_Vel[1][2], Grad_Vel[2][2],
                                               lenScale, dist);
    }
    /* Set eddy viscosity. */
    nodes->SetEddyViscosity(iPoint, muTurb);
  }

  /*--- MPI parallelization ---*/

//  InitiateComms(geometry, config, SGS_MODEL);
//  CompleteComms(geometry, config, SGS_MODEL);

}

void CNSSolver::SetTauWallHeatFlux_WMLES1stPoint(CGeometry *geometry, CSolver **solver_container, CConfig *config, unsigned short iRKStep) {

  /*---
  List TODO here:
   - For each vertex (point):
   - Load the interpolation coefficients.
   - Extract the LES quantities at the exchange points.
   - Call the Wall Model: Calculate Tau_Wall and Heat_Flux.
   - Set Tau_Wall and Heat_Flux in the node structure for future use.
  ---*/

  unsigned short iDim, iMarker;
  unsigned long iVertex, iPoint, Point_Normal;
  bool CalculateWallModel = false;
  bool WMLESFirstPoint = config->GetWMLES_First_Point();

  su2double Vel[3], VelNormal, VelTang[3], VelTangMod, WallDist[3], WallDistMod;
  su2double GradP[3], GradP_TangMod;
  su2double T_Normal, P_Normal, mu_Normal;
  su2double *Coord, *Coord_Normal, UnitNormal[3], *Normal, Area;
  su2double TimeFilter = config->GetDelta_UnstTimeND()/ (config->GetTimeFilter_WMLES() / config->GetTime_Ref());

  su2double Yplus_Max_Local = 0.0;
  su2double Yplus_Min_Local = 1e9;

  for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++) {

    if ((config->GetMarker_All_KindBC(iMarker) == HEAT_FLUX) ||
       (config->GetMarker_All_KindBC(iMarker) == ISOTHERMAL) ) {

     /*--- Identify the boundary by string name ---*/
     string Marker_Tag = config->GetMarker_All_TagBound(iMarker);

     /*--- Identify if this marker is a wall model one---*/
     switch (config->GetWallFunction_Treatment(Marker_Tag)) {
       case EQUILIBRIUM_WALL_MODEL:
       case LOGARITHMIC_WALL_MODEL:
       case ALGEBRAIC_WALL_MODEL:
       case APGLL_WALL_MODEL:
       case TEMPLATE_WALL_MODEL:
         CalculateWallModel = true;
         break;

       case NO_WALL_FUNCTION:
       case STANDARD_WALL_FUNCTION:
         CalculateWallModel = false;
       default:
         break;
     }

     /*--- If not just continue to the next---*/
     if (!CalculateWallModel) continue;

     /*--- Determine the prescribed heat flux or prescribed temperature. ---*/
     bool HeatFlux_Prescribed = false, Temperature_Prescribed = false;
     su2double Wall_HeatFlux = 0.0, Wall_Temperature = 0.0;

     if(config->GetMarker_All_KindBC(iMarker) == HEAT_FLUX) {
       HeatFlux_Prescribed = true;
       Wall_HeatFlux       = config->GetWall_HeatFlux(Marker_Tag);
     }
     else {
       Temperature_Prescribed = true;
       Wall_Temperature       = config->GetIsothermal_Temperature(Marker_Tag);
     }

     /*--- Loop over all of the vertices on this boundary marker ---*/
     for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {

       iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
       Point_Normal = geometry->vertex[iMarker][iVertex]->GetNormal_Neighbor();

       /*--- Check if the node belongs to the domain (i.e, not a halo node) ---*/
       if (!geometry->node[iPoint]->GetDomain()) continue;

       /*--- Check if the node has all boundary neighbors ---*/
       const auto nNeigh = geometry->node[iPoint]->GetnPoint();
       bool found_fluid = false;
       for (unsigned short iNeigh = 0; iNeigh <= nNeigh; iNeigh++) {
       
         auto jPoint = iPoint;
         if (iNeigh < nNeigh) jPoint = geometry->node[iPoint]->GetPoint(iNeigh);
         bool boundary_j = geometry->node[jPoint]->GetPhysicalBoundary();
         if (!boundary_j){
           found_fluid = true;
           break;
         }
       }

       if(!found_fluid){
        nodes->SetTauWall_Flag(iPoint,false);
        continue;
       }

       /*--- Get coordinates of the current vertex and nearest normal point ---*/

       Coord = geometry->node[iPoint]->GetCoord();
       Coord_Normal = geometry->node[Point_Normal]->GetCoord();

       /*--- Compute dual-grid area and boundary normal ---*/

       Normal = geometry->vertex[iMarker][iVertex]->GetNormal();

       Area = 0.0;
       for (iDim = 0; iDim < nDim; iDim++)
         Area += Normal[iDim]*Normal[iDim];
       Area = sqrt (Area);

       for (iDim = 0; iDim < nDim; iDim++)
         UnitNormal[iDim] = -Normal[iDim]/Area;

       /*--- If an exchange location was found (donor element) use this information as the input
       for the wall model. Otherwise, the information of the 1st point off the wall is used. ---*/

       if (geometry->vertex[iMarker][iVertex]->GetDonorFound() && !WMLESFirstPoint){

         const su2double *doubleInfo = config->GetWallFunction_DoubleInfo(Marker_Tag);
         WallDistMod = doubleInfo[0];

         /*--- Load the coefficients and interpolate---*/
         unsigned short nDonors = geometry->vertex[iMarker][iVertex]->GetnDonorPoints();
         su2double rho_Normal = 0.0, e_Normal   = 0.0;

         for (iDim = 0; iDim < nDim; iDim++ ){
           Vel[iDim]   = 0.0;
         }

         for (unsigned short iNode = 0; iNode < nDonors; iNode++) {
           unsigned long donorPoint = geometry->vertex[iMarker][iVertex]->GetInterpDonorPoint(iNode);
           su2double donnorCoeff    = geometry->vertex[iMarker][iVertex]->GetDonorCoeff(iNode);

           rho_Normal += donnorCoeff*nodes->GetSolution(donorPoint,0);
           e_Normal   += donnorCoeff*nodes->GetSolution(donorPoint,nVar-1)/nodes->GetSolution(donorPoint,0);

           for (iDim = 0; iDim < nDim; iDim++ ){
             Vel[iDim] += donnorCoeff*nodes->GetSolution(donorPoint,iDim+1)/nodes->GetSolution(donorPoint,0);
           }
         }

         /*--- Load the fluid model to have pressure and temperature at exchange location---*/
         GetFluidModel()->SetTDState_rhoe(rho_Normal, e_Normal);
         P_Normal = GetFluidModel()->GetPressure();
         T_Normal = GetFluidModel()->GetTemperature();
         mu_Normal= GetFluidModel()->GetLaminarViscosity();

       }
       else{

         /*--- Compute normal distance of the interior point from the wall ---*/

         for (iDim = 0; iDim < nDim; iDim++)
           WallDist[iDim] = (Coord[iDim] - Coord_Normal[iDim]);

         WallDistMod = 0.0;
         for (iDim = 0; iDim < nDim; iDim++)
           WallDistMod += WallDist[iDim]*WallDist[iDim];
         WallDistMod = sqrt(WallDistMod);

         /*--- Get the velocity, pressure, and temperature at the nearest
          (normal) interior point. ---*/

         for (iDim = 0; iDim < nDim; iDim++){
           Vel[iDim]   = nodes->GetVelocity(Point_Normal,iDim);
         }

         P_Normal  = nodes->GetPressure(Point_Normal);
         T_Normal  = nodes->GetTemperature(Point_Normal);
         mu_Normal = nodes->GetLaminarViscosity(Point_Normal);
       }

       for (iDim = 0; iDim < nDim; iDim++){
         GradP[iDim] = nodes->GetGradient_Primitive(iPoint, nDim+1, iDim);
       }

       /*--- Filter the input LES velocity ---*/

       long curAbsTimeIter = (config->GetTimeIter() - config->GetRestart_Iter());
       if (curAbsTimeIter > 0){

         /*--- Old input LES velocity and GradP---*/
         su2double Vel_old[3]   = {0.,0.,0.};
         for (iDim = 0; iDim < nDim; iDim++){
           Vel_old[iDim]   = VelTimeFilter_WMLES[iMarker][iVertex][iDim];
         }
         /*--- Now filter the LES velocity ---*/
         for (iDim = 0; iDim < nDim; iDim++){
           Vel[iDim] = (1.0 - TimeFilter) * Vel_old[iDim] + TimeFilter * Vel[iDim];
         }
       }

       /*--- Update input LES velocity if it is the 1st inner iteration---*/
       if (config->GetInnerIter() == 0){
         for (iDim = 0; iDim < nDim; iDim++){
           VelTimeFilter_WMLES[iMarker][iVertex][iDim] = Vel[iDim];
         }
       }

       /*--- Compute dimensional variables before calling the Wall Model ---*/
       for (iDim = 0; iDim < nDim; iDim++ ){
         Vel[iDim] *= config->GetVelocity_Ref();
         GradP[iDim] *= config->GetPressure_Ref();
       }
       P_Normal *= config->GetPressure_Ref();
       T_Normal *= config->GetTemperature_Ref();
       mu_Normal *= (config->GetPressure_Ref()/config->GetVelocity_Ref());

       /*--- Compute the wall-parallel velocity ---*/

       VelNormal = 0.0;
       for (iDim = 0; iDim < nDim; iDim++)
         VelNormal += Vel[iDim] * UnitNormal[iDim];
       for (iDim = 0; iDim < nDim; iDim++)
         VelTang[iDim] = Vel[iDim] - VelNormal*UnitNormal[iDim];

       VelTangMod = 0.0;
       for (iDim = 0; iDim < nDim; iDim++)
         VelTangMod += VelTang[iDim]*VelTang[iDim];
       VelTangMod = sqrt(VelTangMod);
       VelTangMod = max(VelTangMod,1.e-25);

       su2double dirTan[3] = {0.0, 0.0, 0.0};
       for(iDim = 0; iDim<nDim; iDim++) dirTan[iDim] = VelTang[iDim]/VelTangMod;

       /*--- If it is pressure gradient driven flow
        subtract the body force in all directions. ---*/
       if (config->GetBody_Force()){
         for (iDim = 0; iDim < nDim; iDim++)
           GradP[iDim] -= config->GetBody_Force_Vector()[iDim];
       }
       
       /*--- Pressure gradient in the tangent direction: ---*/
       GradP_TangMod = 0.0;
       for (iDim = 0; iDim < nDim; iDim++)
         GradP_TangMod += GradP[iDim]*dirTan[iDim];
              
       /* Compute the wall shear stress and heat flux vector using
        the wall model. */
       su2double tauWall, qWall, ViscosityWall, kOverCvWall;
       bool converged;
       WallModel->UpdateExchangeLocation(WallDistMod);
       WallModel->WallShearStressAndHeatFlux(T_Normal, VelTangMod, mu_Normal, P_Normal, GradP_TangMod,
                                             Wall_HeatFlux, HeatFlux_Prescribed,
                                             Wall_Temperature, Temperature_Prescribed,
                                             GetFluidModel(), tauWall, qWall, ViscosityWall,
                                             kOverCvWall, converged);

       if (!converged || std::isnan(tauWall)){
         nodes->SetTauWall_Flag(iPoint,false);
         continue;
       }
       
       su2double rho    = nodes->GetDensity(iPoint) * config->GetDensity_Ref();
       su2double u_tau  = sqrt(tauWall/rho);
       su2double y_plus = rho * u_tau * WallDistMod / ViscosityWall; 

       if (config->GetWMLES_Monitoring()){
        if(y_plus < 0.1){
         nodes->SetTauWall_Flag(iPoint,false);
         continue;          
        }
       }
       
       Yplus_Max_Local = max(Yplus_Max_Local, y_plus);
       Yplus_Min_Local = min(Yplus_Min_Local, y_plus);

       /*--- Compute the non-dimensional values if necessary. ---*/
       tauWall /= config->GetPressure_Ref();
       qWall   /= (config->GetPressure_Ref() * config->GetVelocity_Ref());
       ViscosityWall /= (config->GetPressure_Ref()/config->GetVelocity_Ref());
       nodes->SetLaminarViscosity(iPoint, ViscosityWall);

       /*--- Set tau wall value and flag for flux computation---*/
       nodes->SetTauWall_Flag(iPoint,true);
       nodes->SetTauWall(iPoint, tauWall);

       /*--- Set tau wall projected to the flow direction for pos-processing only---*/
       for(iDim = 0; iDim<nDim; iDim++)
         nodes->SetTauWallDir(iPoint, iDim, tauWall*dirTan[iDim]);

       /*--- Set tau wall value and heat flux for boundary conditions---*/
       TauWall_WMLES[iMarker][iVertex] = tauWall;
       HeatFlux_WMLES[iMarker][iVertex] = qWall;
       for (iDim = 0; iDim < nDim; iDim++)
         FlowDirTan_WMLES[iMarker][iVertex][iDim] = dirTan[iDim];

     }
   }
 }
 su2double Yplus_Max_Global = Yplus_Max_Local;
 su2double Yplus_Min_Global = Yplus_Min_Local;

 SU2_MPI::Allreduce(&Yplus_Max_Local, &Yplus_Max_Global, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
 SU2_MPI::Allreduce(&Yplus_Min_Local, &Yplus_Min_Global, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);

 if ((rank == MASTER_NODE) && (config->GetInnerIter()==0)){
  cout << endl   << "------------------------ WMLES -----------------------" << endl;
  cout << "Y+ (Max): " << setprecision(6) << Yplus_Max_Global << endl;
  cout << "Y+ (Min): " << setprecision(6) << Yplus_Min_Global << endl;
 }

}

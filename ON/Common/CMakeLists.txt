add_library (ON_Common
    MatrixKpoint.cpp
 Exx_on.cpp
 GetNewRho_on.cpp
 Scf_on_proj.cpp
 WriteWaveFunctions.cpp
 WriteCij.cpp
 read_orbitals_on.cpp
 CalculateResidual.cpp
 get_te.cpp
 ReadDataFromRMG.cpp
 UpdatePot.cpp
 OrbitalOptimize.cpp
 DiagScalapack.cpp
 DiagGpu.cpp
 DiagElemental.cpp
 ReadPermInfo.cpp
 WritePermInfo.cpp
 PermAtoms.cpp
 GetPermStateIndex.cpp
 BandwidthReduction.cpp
 write_restart.cpp
 init_wf_lcao.cpp
 add_orbit_to_wave.cpp
 allocate_matrix.cpp
 app_cil_orbital6.cpp
 app_cil_orbital.cpp
 change_states_crds.cpp
 CMakeLists.txt
 diag_eig_matrix.cpp
 Fill_on.cpp
 genvpsi.cpp
 get_cholesky_real.cpp
 get_mat_Omega.cpp
 get_orbit_center.cpp
 get_state_to_proc.cpp
 get_sum_eig.cpp
 get_wave.cpp
 InitON.cpp
 init_pe_on.cpp
 init_rho_atom.cpp
 init_wf_atom.cpp
 init_wf.cpp
 init_wf_gaussian.cpp
 interpolate_atom_density.cpp
 Kain.cpp
 md_fastrelax.cpp
 mgrid_solv_local.cpp
 MgridSolvLocal.cpp
 movie.cpp
 nlforce.cpp
 nlforce_par_omega.cpp
 normalize_orbits.cpp
 ortho_norm_local.cpp
 Precond.cpp
 PrecondMg.cpp
 PrecondMg1.cpp
 print_status.cpp
 print_wave.cpp
 qnm_beta_betapsi.cpp
 quench.cpp
 rand0.cpp
 read_data.cpp
 rmg_timings.cpp
 Scf_on.cpp
 sd.cpp
 tools_submat.cpp
 update_orbit_centers.cpp
 write_data.cpp
 write_eigs.cpp
 write_states_info.cpp
 xbsmovie.cpp
 ZeroBoundary.cpp
)
include_directories("${RMG_SOURCE_DIR}/ON/Headers/")
include_directories("${RMG_BINARY_DIR}/ON/Headers/")

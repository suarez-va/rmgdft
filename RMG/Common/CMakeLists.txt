add_library (RMG_Common
#GetFdFactor.cpp
STM_calc.cpp
ComputeHcore.cpp
OutputDos.cpp
Reinit.cpp
Stress.cpp
Neb.cpp
EnergyCorrection.cpp
BroydenPotential.cpp
DavPreconditioner.cpp
ApplyHamiltonian.cpp
ApplyHamiltonianBlock.cpp 
Davidson.cpp
MgridSubspace.cpp
MolecularDynamics.cpp
Fill.cpp
GetAugRho.cpp
GeneralDiag.cpp
GetTe.cpp
MixRho.cpp
WriteData.cpp
WriteSerialData.cpp
WriteBGW_Wfng.cpp
WriteBGW_Rhog.cpp
WriteBGW_VxcEig.cpp
WriteRestart.cpp
LcaoGetAwave.cpp
LcaoGetPsi.cpp
ReadData.cpp
Read_nsocc.cpp
ReadSerialData.cpp
AssignWeight.cpp
GatherScatter.cpp
GetDelocalizedWeight.cpp
GetLocalizedWeight.cpp
ReinitIonicPotentials.cpp
GetDelocalizedOrbital.cpp
PotentialAcceleration.cpp
Projector.cpp
GetNewRho.cpp
Betaxpsi.cpp
LdaplusUxpsi.cpp
LdaU.cpp
Init.cpp
Main.cpp
AppNls.cpp
Kpoint.cpp
State.cpp
Quench.cpp
Relax.cpp
RmgThread.cpp
OutputEigenvalues.cpp
Scf.cpp
Nscf.cpp
genvpsi.cpp
MgEigState.cpp
GetGamma.cpp
fastrelax.cpp
lbfgs_init.cpp
Nlforce.cpp
pack_rho_ctof.cpp
write_force.cpp
#getpoi_bc.c
rand0.cpp
#get_pdos.cpp
BandStructure.cpp
#mulliken.c
write_avgd.cpp
claim_ion.cpp
debug_write_rho_z.cpp
write_pdb.cpp
xbsmovie.cpp
write_zstates.cpp
lbfgs.cpp
rmg_lbfgs.cpp
test_overlap.cpp
write_avgv.cpp
move_ions.cpp
print_density_z_direction.cpp
set_pbc.cpp
PartialGamma.cpp
fire.cpp
Write_Wfs_forWannier.cpp
WellPotential.cpp
write_ffield.cpp
)
include_directories("${RMG_SOURCE_DIR}/RMG/Headers/")
include_directories("${RMG_BINARY_DIR}/RMG/Headers/")

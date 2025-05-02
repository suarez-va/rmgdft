#include <iostream> 
#include <fstream>
#include <sstream>
#include <iterator>
#include <string> 
#include <cfloat> 
#include <climits> 
#include <unordered_map>
#include <set>
#include <list>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/filesystem.hpp>
#include "MapElements.h"
#include "transition.h"

#include "const.h"
#include "rmgtypedefs.h"
#include "params.h"
#include "typedefs.h"
#include "RmgException.h"
#include "RmgInputFile.h"
#include "InputOpts.h"


#if OPENBABEL_LIBS
#include <openbabel/mol.h>
#include <openbabel/obconversion.h>
#endif


/**********************************************************************

    Reads internal RMG atomic format which consists of a single atom
    per line with the entire block of lines enclosed in double quotes
    and the block identified by an atoms keyword.

    Column 1: Atomic symbol
    Column 2: x-coordinate
    Column 3: y-coordinate
    Column 4: z-coordinate
    Column 5: boolean value indicating whether the ion is movable or not.
              can be omitted for non spin polarized and the default is movable(1).
    Column 6: starting spin polarization. Required for spin polarized. Each
              unique <species,spin polarization> pair counts as a seperate
              atomic species.
Example:

atoms = "
N    15.3293    12.9600    19.2070   1
C    17.5363    12.9600    17.7857    1
H    13.7313    15.1732    19.2251    1
O    14.4083    17.2893    17.8251    1
C    11.1292    14.3262    19.2527    1
C     9.3055    15.6378    17.8881    1
"

    The coordinates may be either cell relative or absolute in units
    of either bohr or Angstroms. The driver routine is not responsible
    for transforming the input coordinates into the internal format
    which is handled at a higher level.
    
    Individual driver routines for different formats require 3 arguments.

    cfile        = name of the file containing the ionic information.
    SpeciesTypes = a std::set that is empty on entry and contains the
                   atomic symbol of each unique species on exit.
    IonSpecies   = a std::list that contains the atomic symbol of each
                   ion in the order in which they were read from cfile.
    InputMap     = Control Map. May not be needed by all atomic input
                   drivers but is useful for reading the RMG format.
    
**********************************************************************/

namespace Ri = RmgInput;

void ReadRmgAtoms(char *cfile, std::set<std::string>& SpeciesTypes, std::list<std::string>& IonSpecies, 
        std::vector<ION>& Atoms_read, CONTROL& lc)
{

    std::string AtomArray;
    std::string line_delims = "^\n";
    std::string whitespace_delims = " \n\t";
    std::vector<std::string> Atoms_str;
    std::unordered_map<std::string, InputKey *> NewMap;
    int nions = 0;

    RmgInputFile If(cfile, NewMap, pct.img_comm);

    If.RegisterInputKey("atoms", &AtomArray, "",
                     CHECK_AND_FIX, REQUIRED,
                     "Ionic species and coordinate information.\n",
                     "");

    If.RegisterInputKey("atomic_coordinate_type", NULL, &lc.crd_flag, "Absolute",
                     CHECK_AND_TERMINATE, OPTIONAL, atomic_coordinate_type,
                     "Flag indicated whether or not atomic coordinates are absolute or cell relative.\n",
                     "atomic_coordinate_type must be either \"Absolute\" or \"Cell Relative\". Terminating.\n");
    

    If.RegisterInputKey("crds_units", NULL, NULL, "Bohr",
                     CHECK_AND_FIX, OPTIONAL, crds_units,
                     "Units for the atomic coordinates.\n",
                     "Coordinates must be specified in either Bohr or Angstrom.\n");

    
    If.LoadInputKeys();


    // Process atoms
    boost::trim(AtomArray);
    boost::trim_if(AtomArray, boost::algorithm::is_any_of("\"^"));

    boost::algorithm::split( Atoms_str, AtomArray, boost::is_any_of(line_delims), boost::token_compress_on );

    // Next step is to determine if the ions are represented by an internal atoms tag or an external file.
    bool external_atoms = true;
    if(Atoms_str.size() > 1) {

        external_atoms = false;  // Must have just one entry in the tag

    }
    else if(Atoms_str.size() == 1) {

        // If the file exists use it
        if( !boost::filesystem::exists(Atoms_str[0].c_str())) external_atoms = false;

    }
    else {

        throw RmgFatalException() << "No atomic coordinates defined! Terminating.\n";

    }
    

#if OPENBABEL_LIBS
    if(external_atoms) {

        const char *atomfile = Atoms_str[0].c_str();

        std::ifstream ifs(atomfile);
        OpenBabel::OBConversion conv;
        OpenBabel::OBFormat* inFormat = conv.FormatFromExt(atomfile);
        if(!inFormat) {
            throw RmgFatalException() << "Unable to process atom input file. " << atomfile << " Terminating.\n";
        }

        if(!conv.SetOutFormat("XYZ")) {
            throw RmgFatalException() << "Unable to set output file format to XYZ in " << __FILE__ << " at line " << __LINE__ << ". Terminating.\n";
        }

        OpenBabel::OBFormat* outFormat = conv.GetOutFormat();
        if(!outFormat) {
            throw RmgFatalException() << "Unable to process atom input file. " << atomfile << " Terminating.\n";
        }

        std::istream* pIn = &ifs;
        std::stringstream newstream;

        conv.SetInAndOutFormats(inFormat,outFormat);
        conv.Convert(pIn, &newstream);
        std::string XYZArray= newstream.str(); 

        std::vector<std::string>  XYZlines;
        boost::algorithm::split( XYZlines, XYZArray, boost::is_any_of(line_delims), boost::token_compress_on );

        // Skip first two lines and reconstruct AtomArray
        std::string fline = *XYZlines.begin();
        int num_ions = std::atoi(fline.c_str());
        XYZlines.erase(XYZlines.begin());
        XYZlines.erase(XYZlines.begin());
        AtomArray.erase();
        auto it1 = XYZlines.begin();
        // A little awkward but the Openbabel conversion seems to leave empty lines at the end.
        for(int ion=0;ion < num_ions;ion++) {
            AtomArray = AtomArray + *it1 + "    1\n";
            ++it1;
        }

        boost::trim(AtomArray);
        boost::algorithm::split( Atoms_str, AtomArray, boost::is_any_of(line_delims), boost::token_compress_on );

        // Openbabel always uses Angstroms and absolute coordinates
        InputKey *Ik = NewMap["crds_units"]; 
        static std::string Angstroms("Angstrom");
        Ik->Readstr = Angstroms;

        Ik = NewMap["atomic_coordinate_type"]; 
        static std::string AbsoluteCoords("Absolute");
        Ik->Readstr = AbsoluteCoords;
    }

#else

    if(external_atoms)
        throw RmgFatalException() << "This version of RMG was not built with OpenBabel support so external atomic coordinate formats are not supported. Terminating.\n";

#endif


    // Converted to RMG internal format and process from here.

    lc.num_ions = Atoms_str.size();
    Atoms_read.resize(lc.num_ions);

    std::vector<std::string>::iterator it, it1;
    for (it = Atoms_str.begin(); it != Atoms_str.end(); ++it) {

        if(nions > lc.num_ions)
            throw RmgFatalException() << "Inconsistency in number of ions: " << lc.num_ions << " was specified initially but " << nions << " were found.\n";

        std::string Atom = *it;
        boost::trim(Atom);

        std::vector<std::string> AtomComponents;
        boost::algorithm::split( AtomComponents, Atom, boost::is_any_of(whitespace_delims), boost::token_compress_on );

        size_t ncomp = AtomComponents.size();
        if((ncomp < 4) || (ncomp > 10)) throw RmgFatalException() << "Synax error in ionic information near " << Atom << "\n";

        // First field should be an atomic symbol
        it1 = AtomComponents.begin();
        std::string sp = *it1;
        boost::trim(sp);

        // Valid atomic symbol? GetAtomicMass will throw a fatal exception if the symbol is not valid.
        GetAtomicMass(sp);
        Atoms_read[nions].symbol = sp;
        // Look for the coordinates
        it1++;
        std::string xstr = *it1;
        Atoms_read[nions].crds[0] = std::atof(xstr.c_str());
        it1++;
        std::string ystr = *it1;
        Atoms_read[nions].crds[1] = std::atof(ystr.c_str());
        it1++;
        std::string zstr = *it1;
        Atoms_read[nions].crds[2] = std::atof(zstr.c_str());



        int movable[3] = {1,1,1};
        std::string smov;
        double init_spin_ratio = 0.0;
        double init_spin_angle1 = 0.0;
        double init_spin_angle2 = 0.0;
        switch(ncomp)
        {

            case 5:
            case 6:
            {
                it1++;
                smov = *it1;
                boost::trim(smov);
                if( !smov.compare("0") ) 
                {
                    movable[0] = 0;
                    movable[1] = 0;
                    movable[2] = 0;
                }
            }
                break;
            case 7:
            case 8:
            case 10:
            {
                it1++;
                smov = *it1;
                boost::trim(smov);
                if( !smov.compare("0") ) 
                    movable[0] = 0;
                it1++;
                smov = *it1;
                boost::trim(smov);
                if( !smov.compare("0") ) 
                    movable[1] = 0;
                it1++;
                smov = *it1;
                boost::trim(smov);
                if( !smov.compare("0") ) 
                    movable[2] = 0;
            }
                break;
        }

        if(!ct.noncoll)
        {
            switch(ncomp)
            {
                case 6:
                case 8:
                case 10:
                    {
                        it1++;
                        std::string rho_updown_diff = *it1;
                //        sp += rho_updown_diff;
                        init_spin_ratio = std::atof(rho_updown_diff.c_str());
                    }
                    break;
                default :
                    if(lc.nspin > 1)
                    {
                        throw RmgFatalException() << "nspin = " << lc.nspin <<
                            "\nfor spin-polarization, one needs to have init_spin density setup near " << Atom << "\n";
                    }
            }

        }
        else  // for noncoll or SOC
        {
            if(ncomp == 10)
            {
                it1++;
                std::string rho_updown_diff = *it1;
            //    sp += rho_updown_diff;
                init_spin_ratio = std::atof(rho_updown_diff.c_str());
                it1++;
                std::string rho_spin_theta = *it1;
             //   sp += rho_spin_theta;
                init_spin_angle1 = std::atof(rho_spin_theta.c_str());
                it1++;
                std::string rho_spin_phi = *it1;
              //  sp += rho_spin_phi;
                init_spin_angle2 = std::atof(rho_spin_phi.c_str());
            }
            else 
            {
                throw RmgFatalException() << 
                    "noncollinear require 10 iterm in atom lines:\n" << Atom << "\n"; 

            }
        }

        Atoms_read[nions].movable[0] = movable[0];
        Atoms_read[nions].movable[1] = movable[1];
        Atoms_read[nions].movable[2] = movable[2];
        Atoms_read[nions].init_spin_rho = init_spin_ratio;
        Atoms_read[nions].init_spin_x = 2.0*init_spin_ratio * std::sin(init_spin_angle1/180.0 * PI) * std::cos(init_spin_angle2/180.0*PI) ;
        Atoms_read[nions].init_spin_y = 2.0*init_spin_ratio * std::sin(init_spin_angle1/180.0 * PI) * std::sin(init_spin_angle2/180.0*PI) ;
        Atoms_read[nions].init_spin_z = 2.0*init_spin_ratio * std::cos(init_spin_angle1/180.0 * PI);
        Atoms_read[nions].init_spin_angle1 = init_spin_angle1;
        Atoms_read[nions].init_spin_angle2 = init_spin_angle2;

        if(init_spin_ratio > 0.5 || init_spin_ratio < -0.5) 
            throw RmgFatalException() << 
                "for spin-polarization, the spin updown difference must be -0.5 to 0.5" << Atom << "\n"; 

        // Everything checks out so make an entry in the SpeciesTypes set and in the IonSpecies list.
        // SpeciesTypes set only contains one entry for each unique species type while IonSpecies 
        // contains lc.num_ions entries (one for each ion). A different value of the starting spin
        // polarization counts as a new species type.
        SpeciesTypes.emplace(sp);
        IonSpecies.emplace_back(sp);

        nions++;

    }


    // Atoms have been read, now take care of conversion to internal coordinates
    if (Verify ("atomic_coordinate_type", "Cell Relative", NewMap)) {

        for(int ion = 0;ion < lc.num_ions;ion++) {

            Atoms_read[ion].xtal[0] = Atoms_read[ion].crds[0];
            Atoms_read[ion].xtal[1] = Atoms_read[ion].crds[1];
            Atoms_read[ion].xtal[2] = Atoms_read[ion].crds[2];
            to_cartesian(Atoms[ion].xtal, Atoms[ion].crds);

        }

    }
    else if(Verify ("crds_units", "Angstrom", NewMap)) {

        for(int ion = 0;ion < lc.num_ions;ion++) {

            Atoms_read[ion].crds[0] *= A_a0;
            Atoms_read[ion].crds[1] *= A_a0;
            Atoms_read[ion].crds[2] *= A_a0;
            Rmg_L.to_crystal(Atoms[ion].xtal, Atoms[ion].crds);
            Rmg_L.to_cartesian(Atoms[ion].xtal, Atoms[ion].crds);

        }

    }


    if(nions > lc.num_ions)
        throw RmgFatalException() << "Inconsistency in number of ions: " << lc.num_ions << " was specified initially but " << nions << " were found.\n";

}


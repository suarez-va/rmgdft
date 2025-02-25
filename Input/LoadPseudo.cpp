
#include <exception>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>


#include "const.h"
#include "rmgtypedefs.h"
#include "params.h"
#include "typedefs.h"
#include "MapElements.h"
#include "RmgException.h"
#include "transition.h"



// Identifies pseudopotential file formats and dispatches to correct driver routine
void LoadPseudo(SPECIES *sp)
{

    if(ct.internal_pseudo_type == ALL_ELECTRON)
    {
        LoadAllElectronPseudo(sp);
        return;
    }

    std::string Msg;
    if(!std::strcmp(sp->atomic_symbol, "DLO")) {

        sp->zvalence = 0.0;
        sp->nbeta = 0;
        sp->rg_points = 0;
        return;

    }
    
    if((sp->pseudo_filename == std::string("./@Internal")) || (sp->pseudo_filename.length() == 0)) {
#if INTERNAL_PP
        LoadUpfPseudo(sp);
#else
        throw RmgFatalException() << "This version of RMG was not built with internal pseudopotential support. You need to "
                                  << "specify a pseudopotential for " << sp->atomic_symbol << " in the input file.\n";
#endif
        return;
    }

    std::string extension = sp->pseudo_filename.substr(sp->pseudo_filename.size() - 4);
    boost::to_upper(extension);

    std::string check = ".UPF";
    if(extension == check)
    {
        sp->ftype = UPF_FORMAT;
        LoadUpfPseudo(sp);
        return;
    }

    check = ".XML";
    if(extension == check)
    {
        sp->ftype = QMC_FORMAT;
        LoadXmlPseudo(sp);
        return;
    }

    throw RmgFatalException() << "Unknown pseudopotential format extension " << extension << " terminating.\n";

}

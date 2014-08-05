#include "ElectroMagn.h"

#include <limits>
#include <iostream>

#include "ElectroMagn1D.h"
#include "PicParams.h"
#include "Species.h"
#include "Projector.h"
#include "Laser.h"
#include "Field.h"
#include "FieldsBC.h"
#include "FieldsBC_Factory.h"
#include "SimWindow.h"

using namespace std;


// ---------------------------------------------------------------------------------------------------------------------
// Constructor for the virtual class ElectroMagn
// ---------------------------------------------------------------------------------------------------------------------
ElectroMagn::ElectroMagn(PicParams* params, SmileiMPI* smpi) :
res_time(params->res_time)
{
    // initialize poynting vector
    poynting[0].resize(params->nDim_field,0.0);
    poynting[1].resize(params->nDim_field,0.0);
    
    // take useful things from params
    cell_volume=params->cell_volume;
    n_space=params->n_space;
    oversize=params->oversize;
    for (unsigned int i=0; i<3; i++) {
        DEBUG("____________________ OVERSIZE: " <<i << " " << oversize[i]);
    }
    
    if (n_space.size() != 3) ERROR("this should not happend");

    Ex_=NULL;
    Ey_=NULL;
    Ez_=NULL;
    Bx_=NULL;
    By_=NULL;
    Bz_=NULL;
    Bx_m=NULL;
    By_m=NULL;
    Bz_m=NULL;
    Jx_=NULL;
    Jy_=NULL;
    Jz_=NULL;
    rho_=NULL;
    rho_o=NULL;
    
    Ex_avg=NULL;
    Ey_avg=NULL;
    Ez_avg=NULL;
    Bx_avg=NULL;
    By_avg=NULL;
    Bz_avg=NULL;
    
    // Species charge currents and density
    n_species = params->n_species;
    Jx_s.resize(n_species);
    Jy_s.resize(n_species);
    Jz_s.resize(n_species);
    rho_s.resize(n_species);
    for (unsigned int ispec=0; ispec<n_species; ispec++) {
        Jx_s[ispec]  = NULL;
        Jy_s[ispec]  = NULL;
        Jz_s[ispec]  = NULL;
        rho_s[ispec] = NULL;
    }

    
    for (unsigned int i=0; i<3; i++) {
        for (unsigned int j=0; j<2; j++) {
            istart[i][j]=0;
            bufsize[i][j]=0;
        }
    }    

    fieldsBoundCond = FieldsBC_Factory::create(*params);

}



// ---------------------------------------------------------------------------------------------------------------------
// Destructor for the virtual class ElectroMagn
// ---------------------------------------------------------------------------------------------------------------------
ElectroMagn::~ElectroMagn()
{
    delete Ex_;
    delete Ey_;
    delete Ez_;
    delete Bx_;
    delete By_;
    delete Bz_;
    delete Bx_m;
    delete By_m;
    delete Bz_m;
    delete Jx_;
    delete Jy_;
    delete Jz_;
    delete rho_;
    delete rho_o;

    int nBC = fieldsBoundCond.size();
    for ( int i=0 ; i<nBC ;i++ )
      delete fieldsBoundCond[i];

}//END Destructer

// ---------------------------------------------------------------------------------------------------------------------
// Maxwell solver using the FDTD scheme
// ---------------------------------------------------------------------------------------------------------------------
/*void ElectroMagn::solveMaxwell(double time_dual, SmileiMPI* smpi)
 {
 //solve Maxwell's equations
 solveMaxwellAmpere();
 //smpi->exchangeE( EMfields );
 solveMaxwellFaraday();
 smpi->exchangeB( this );
 boundaryConditions(time_dual, smpi);
 
 }*/
void ElectroMagn::solveMaxwell(int itime, double time_dual, SmileiMPI* smpi, PicParams &params, SimWindow* simWindow)
{
    saveMagneticFields();

    // Compute Ex_, Ey_, Ez_
    solveMaxwellAmpere();
    // Exchange Ex_, Ey_, Ez_
    smpi->exchangeE( this );

    // Compute Bx_, By_, Bz_
    solveMaxwellFaraday();

    // Update Bx_, By_, Bz_
    if ((!simWindow) || (!simWindow->isMoving(itime)) )
	fieldsBoundCond[0]->apply(this, time_dual, smpi);
    if ( (!params.use_transverse_periodic) && (fieldsBoundCond.size()>1) )
	fieldsBoundCond[1]->apply(this, time_dual, smpi);
 
    // Exchange Bx_, By_, Bz_
    smpi->exchangeB( this );

    // Compute Bx_m, By_m, Bz_m
    centerMagneticFields();

}


// ---------------------------------------------------------------------------------------------------------------------
// Method used to create a dump of the data contained in ElectroMagn
// ---------------------------------------------------------------------------------------------------------------------
void ElectroMagn::dump(PicParams* params)
{
    //!\todo Check for none-cartesian grid & for generic grid (neither all dual or all primal) (MG & JD)
    
    vector<unsigned int> dimPrim;
    dimPrim.resize(1);
    dimPrim[0] = params->n_space[0]+2*params->oversize[0]+1;
    vector<unsigned int> dimDual;
    dimDual.resize(1);
    dimDual[0] = params->n_space[0]+2*params->oversize[0]+2;
    
    // dump of the electromagnetic fields
    Ex_->dump(dimDual);
    Ey_->dump(dimPrim);
    Ez_->dump(dimPrim);
    Bx_->dump(dimPrim);
    By_->dump(dimDual);
    Bz_->dump(dimDual);
    // dump of the total charge density & currents
    rho_->dump(dimPrim);
    Jx_->dump(dimDual);
    Jy_->dump(dimPrim);
    Jz_->dump(dimPrim);
}



// ---------------------------------------------------------------------------------------------------------------------
// Method used to initialize the total charge density
// ---------------------------------------------------------------------------------------------------------------------
void ElectroMagn::initRhoJ(vector<Species*> vecSpecies, Projector* Proj)
{
    //! \todo Check that one uses only none-test particles
    // number of (none-test) used in the simulation
    unsigned int n_species = vecSpecies.size();
    
    //loop on all (none-test) Species
    for (unsigned int iSpec=0 ; iSpec<n_species; iSpec++ ) {
        Particles cuParticles = vecSpecies[iSpec]->getParticlesList();
        unsigned int n_particles = vecSpecies[iSpec]->getNbrOfParticles();
        
        DEBUG(n_particles<<" species "<<iSpec);
        for (unsigned int iPart=0 ; iPart<n_particles; iPart++ ) {
            // project charge & current densities
            (*Proj)(Jx_s[iSpec], Jy_s[iSpec], Jz_s[iSpec], rho_s[iSpec], cuParticles, iPart,
                    cuParticles.lor_fac(iPart));
        }
        
    }//iSpec
    
    computeTotalRhoJ();
    DEBUG("projection done for initRhoJ");
    
}



// ---------------------------------------------------------------------------------------------------------------------
// Method used to compute EM fields related scalar quantities used in diagnostics
// ---------------------------------------------------------------------------------------------------------------------
void ElectroMagn::computeScalars()
{
    
    vector<Field*> fields;
    
    fields.push_back(Ex_);
    fields.push_back(Ey_);
    fields.push_back(Ez_);
    fields.push_back(Bx_m);
    fields.push_back(By_m);
    fields.push_back(Bz_m);
    
    vector<double> E_tot_fields(1,0);
    
    for (vector<Field*>::iterator field=fields.begin(); field!=fields.end(); field++) {
        
        map<string,vector<double> > scalars_map;
        
        vector<double> Etot(1,0);

        vector<unsigned int> iFieldStart(3,0), iFieldEnd(3,1), iFieldGlobalSize(3,1);
        for (unsigned int i=0 ; i<(*field)->isDual_.size() ; i++ ) {
            iFieldStart[i] = istart[i][(*field)->isDual(i)];
            iFieldEnd [i] = iFieldStart[i] + bufsize[i][(*field)->isDual(i)];
            iFieldGlobalSize [i] = (*field)->dims_[i];
        }
        
//        unsigned int my_incr=0;
        for (unsigned int k=iFieldStart[2]; k<iFieldEnd[2]; k++) {
            for (unsigned int j=iFieldStart[1]; j<iFieldEnd[1]; j++) {
                for (unsigned int i=iFieldStart[0]; i<iFieldEnd[0]; i++) {
// was:             unsigned int ii=i+j*(*field)->dims_[0]+k*(*field)->dims_[0]*(*field)->dims_[1];
                    unsigned int ii=k+ j*iFieldGlobalSize[2] +i*iFieldGlobalSize[1]*iFieldGlobalSize[2];

                    //!debug stuff
//                    int __rk; MPI_Comm_rank( MPI_COMM_WORLD, &__rk );
//                    if ((*field)->name == "Ex" && __rk==1) {
//                        DEBUG(__rk << " ------------------- "<< (*field)->name << "  -> " << ii << " " << ++my_incr << " " << std::scientific << setprecision(12) << setw(20) << (**field)(ii));
//                        DEBUG( i << " " << j);
//                        DEBUG(iFieldStart[0] << " " <<  iFieldStart[1] << " " << iFieldEnd[0] << " " <<  iFieldEnd[1]);
//                        DEBUG((*field)->dims_[0] << " " <<  (*field)->dims_[1] << " " <<  (*field)->dims_[2] << " : " << (*field)->globalDims_);
//                    }

                    Etot[0]+=pow((**field)(ii),2);
                }
            }
        }
        Etot[0]*=0.5*cell_volume;
        scalars_map["sum"]=Etot;
        E_tot_fields[0]+=Etot[0];
        scalars[(*field)->name+"_U"]=scalars_map;
    }
    
    //Field energy stuff
    map<string,vector<double> > E_tot_fields_map;
    E_tot_fields_map["sum"]=E_tot_fields;
    scalars["E_EMfields"]=E_tot_fields_map;
    
    
    
    // now we add currents and density
    
    fields.push_back(Jx_);
    fields.push_back(Jy_);
    fields.push_back(Jz_);
    fields.push_back(rho_);
    
    
    
    for (vector<Field*>::iterator field=fields.begin(); field!=fields.end(); field++) {
        
        map<string,vector<double> > scalars_map;
        vector<double> minVec(4,0);
        vector<double> maxVec(4,0);
        
        minVec[0]=maxVec[0]=(**field)(0);
        
        vector<unsigned int> iFieldStart(3,0), iFieldEnd(3,1), iFieldGlobalSize(3,1);
        for (unsigned int i=0 ; i<(*field)->isDual_.size() ; i++ ) {
            iFieldStart[i] = istart [i][(*field)->isDual(i)];
            iFieldEnd [i] = iFieldStart[i] + bufsize[i][(*field)->isDual(i)];
            iFieldGlobalSize [i] = (*field)->dims_[i];
        }
        for (unsigned int k=iFieldStart[2]; k<iFieldEnd[2]; k++) {
            for (unsigned int j=iFieldStart[1]; j<iFieldEnd[1]; j++) {
                for (unsigned int i=iFieldStart[0]; i<iFieldEnd[0]; i++) {
// was:             unsigned int ii=i+j*(*field)->dims_[0]+k*(*field)->dims_[0]*(*field)->dims_[1];
                    unsigned int ii=k+ j*iFieldGlobalSize[2] +i*iFieldGlobalSize[1]*iFieldGlobalSize[2];
                    if (minVec[0]>(**field)(ii)) {
                        minVec[0]=(**field)(ii);
                        minVec[1]=i;
                        minVec[2]=j;
                        minVec[3]=k;
                    }
                    if (maxVec[0]<(**field)(ii)) {
                        maxVec[0]=(**field)(ii);
                        maxVec[1]=i;
                        maxVec[2]=j;
                        maxVec[3]=k;
                    }
                }
            }
        }
        minVec.resize(1+(*field)->dims_.size());
        maxVec.resize(1+(*field)->dims_.size());

        // we just store the values that change
        scalars_map["min"]=minVec;
        scalars_map["max"]=maxVec;
        scalars[(*field)->name]=scalars_map;
    }
    
    // poynting stuff
    for (unsigned int j=0; j<2;j++) {    
        for (unsigned int i=0; i<poynting[j].size();i++) {
            vector<double> dummy_poy(1,poynting[j][i]);
            map<string,vector<double> > dummy_poynting_map;
            dummy_poynting_map["sum"]=dummy_poy;
            stringstream s;
            s << "Poy_" << (j==0?"inf":"sup") << "_" << i;
            scalars[s.str()]=dummy_poynting_map;
        }
    }
    vector<double> poynting_tot;
    poynting_tot.insert(poynting_tot.end(),poynting[0].begin(),poynting[0].end());
    poynting_tot.insert(poynting_tot.end(),poynting[1].begin(),poynting[1].end());
    
    map<string,vector<double> > poynting_map;
    poynting_map["sum"]=poynting_tot;
    scalars["Poy"]=poynting_map;
    
}


void ElectroMagn::movingWindow_x(unsigned int shift, SmileiMPI *smpi)
{
    Ex_->shift_x(shift);
    Ey_->shift_x(shift);
    Ez_->shift_x(shift);
    //! \ Comms to optimize, only in x, east to west 
    //! \ Implement SmileiMPI::exchangeE( EMFields*, int nDim, int nbNeighbors );
    smpi->exchangeE( this );

    Bx_->shift_x(shift);
    By_->shift_x(shift);
    Bz_->shift_x(shift);
    smpi->exchangeB( this );

    //Here you might want to apply some new boundary conditions on the +x boundary. For the moment, all fields are set to 0. 
}


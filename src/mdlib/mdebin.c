/* -*- mode: c; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; c-file-style: "stroustrup"; -*-
 *
 * 
 *                This source code is part of
 * 
 *                 G   R   O   M   A   C   S
 * 
 *          GROningen MAchine for Chemical Simulations
 * 
 *                        VERSION 3.2.0
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team,
 * check out http://www.gromacs.org for more information.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 * 
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 * 
 * For more info, check our website at http://www.gromacs.org
 * 
 * And Hey:
 * GROwing Monsters And Cloning Shrimps
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "typedefs.h"
#include "string2.h"
#include "mdebin.h"
#include "smalloc.h"
#include "physics.h"
#include "enxio.h"
#include "vec.h"
#include "disre.h"
#include "main.h"
#include "network.h"
#include "names.h"
#include "orires.h"
#include "constr.h"
#include "mtop_util.h"
#include "xvgr.h"

static bool bEInd[egNR] = { TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE };

static bool bEner[F_NRE];

static const char *conrmsd_nm[] = { "Constr. rmsd", "Constr.2 rmsd" };

static const char *boxs_nm[] = { "Box-X", "Box-Y", "Box-Z" };

static const char *tricl_boxs_nm[] = { "Box-XX", "Box-YX", "Box-YY",
                                 "Box-ZX", "Box-ZY", "Box-ZZ" };

static const char *vol_nm[] = { "Volume" };

static const char *dens_nm[] = {"Density" };

static const char *pv_nm[] = {"pV" };

static const char *boxvel_nm[] = {
  "Box-Vel-XX", "Box-Vel-YY", "Box-Vel-ZZ",
  "Box-Vel-YX", "Box-Vel-ZX", "Box-Vel-ZY"
};

#define NBOXS asize(boxs_nm)
#define NTRICLBOXS asize(tricl_boxs_nm)

static bool bConstr,bConstrVir,bTricl,bDynBox;
static int  f_nre=0,epc,etc,nCrmsd;

t_mdebin *init_mdebin(int fp_ene,
                      const gmx_mtop_t *mtop,
                      const t_inputrec *ir)
{
  const char *ener_nm[F_NRE];
  static const char *vir_nm[] = {
    "Vir-XX", "Vir-XY", "Vir-XZ",
    "Vir-YX", "Vir-YY", "Vir-YZ",
    "Vir-ZX", "Vir-ZY", "Vir-ZZ"
  };
  static const char *sv_nm[] = {
    "ShakeVir-XX", "ShakeVir-XY", "ShakeVir-XZ",
    "ShakeVir-YX", "ShakeVir-YY", "ShakeVir-YZ",
    "ShakeVir-ZX", "ShakeVir-ZY", "ShakeVir-ZZ"
  };
  static const char *fv_nm[] = {
    "ForceVir-XX", "ForceVir-XY", "ForceVir-XZ",
    "ForceVir-YX", "ForceVir-YY", "ForceVir-YZ",
    "ForceVir-ZX", "ForceVir-ZY", "ForceVir-ZZ"
  };
  static const char *pres_nm[] = {
    "Pres-XX","Pres-XY","Pres-XZ",
    "Pres-YX","Pres-YY","Pres-YZ",
    "Pres-ZX","Pres-ZY","Pres-ZZ"
  };
  static const char *surft_nm[] = {
    "#Surf*SurfTen"
  };
  static const char *mu_nm[] = {
    "Mu-X", "Mu-Y", "Mu-Z"
  };
  static const char *vcos_nm[] = {
    "2CosZ*Vel-X"
  };
  static const char *visc_nm[] = {
    "1/Viscosity"
  };
  char     **grpnms;
  const gmx_groups_t *groups;
  char     **gnm;
  char     buf[256];
  t_mdebin *md;
  int      i,j,ni,nj,n,k,kk,ncon,nset;
  bool     bBHAM,b14;
  
  groups = &mtop->groups;

  bBHAM = (mtop->ffparams.functype[0] == F_BHAM);
  b14   = (gmx_mtop_ftype_count(mtop,F_LJ14) > 0 ||
	   gmx_mtop_ftype_count(mtop,F_LJC14_Q) > 0);

  ncon = gmx_mtop_ftype_count(mtop,F_CONSTR);
  nset = gmx_mtop_ftype_count(mtop,F_SETTLE);
  bConstr    = (ncon > 0 || nset > 0);
  bConstrVir = FALSE;
  if (bConstr) {
    if (ncon > 0 && ir->eConstrAlg == econtLINCS) {
      if (ir->eI == eiSD2)
	nCrmsd = 2;
      else
	nCrmsd = 1;
    }
    bConstrVir = (getenv("GMX_CONSTRAINTVIR") != NULL);
  } else {
    nCrmsd = 0;
  }

  for(i=0; i<F_NRE; i++) {
    bEner[i] = FALSE;
    if (i == F_LJ)
      bEner[i] = !bBHAM;
    else if (i == F_BHAM)
      bEner[i] = bBHAM;
    else if (i == F_EQM)
      bEner[i] = ir->bQMMM;
    else if (i == F_COUL_LR)
      bEner[i] = (ir->rcoulomb > ir->rlist);
    else if (i == F_LJ_LR)
      bEner[i] = (!bBHAM && ir->rvdw > ir->rlist);
    else if (i == F_BHAM_LR)
      bEner[i] = (bBHAM && ir->rvdw > ir->rlist);
    else if (i == F_RF_EXCL)
      bEner[i] = (EEL_RF(ir->coulombtype) && ir->coulombtype != eelRF_NEC);
    else if (i == F_COUL_RECIP)
      bEner[i] = EEL_FULL(ir->coulombtype);
    else if (i == F_LJ14)
      bEner[i] = b14;
    else if (i == F_COUL14)
      bEner[i] = b14;
    else if (i == F_LJC14_Q || i == F_LJC_PAIRS_NB)
      bEner[i] = FALSE;
    else if ((i == F_DVDL) || (i == F_DKDL))
      bEner[i] = (ir->efep != efepNO);
    else if (i == F_DHDL_CON)
      bEner[i] = (ir->efep != efepNO && bConstr);
    else if ((interaction_function[i].flags & IF_VSITE) ||
	     (i == F_CONSTR) || (i == F_CONSTRNC) || (i == F_SETTLE))
      bEner[i] = FALSE;
    else if ((i == F_COUL_SR) || (i == F_EPOT) || (i == F_PRES)  || (i==F_EQM))
      bEner[i] = TRUE;
    else if ((i == F_ETOT) || (i == F_EKIN) || (i == F_TEMP))
      bEner[i] = EI_DYNAMICS(ir->eI);
    else if (i == F_DISPCORR || i == F_PDISPCORR)
      bEner[i] = (ir->eDispCorr != edispcNO);
    else if (i == F_DISRESVIOL)
      bEner[i] = (gmx_mtop_ftype_count(mtop,F_DISRES) > 0);
    else if (i == F_ORIRESDEV)
      bEner[i] = (gmx_mtop_ftype_count(mtop,F_ORIRES) > 0);
    else if (i == F_CONNBONDS)
      bEner[i] = FALSE;
    else if (i == F_COM_PULL)
      bEner[i] = (ir->ePull == epullUMBRELLA || ir->ePull == epullCONST_F);
    else if (i == F_ECONSERVED)
      bEner[i] = ((ir->etc == etcNOSEHOOVER || ir->etc == etcVRESCALE) &&
		  ir->epc == epcNO);
    else
      bEner[i] = (gmx_mtop_ftype_count(mtop,i) > 0);
  }

    for(i=0; i<F_NRE; i++)
    {
        if (bEner[i])
        {
            /* FIXME: The constness should not be cast away */
            ener_nm[f_nre]=(char *)interaction_function[i].longname;
            f_nre++;
        }
    }

    epc = ir->epc;
    bTricl = TRICLINIC(ir->compress) || TRICLINIC(ir->deform);
    bDynBox = DYNAMIC_BOX(*ir);
    etc = ir->etc;
  
    /* Energy monitoring */
    snew(md,1);
    md->ebin  = mk_ebin();
    /* Pass NULL for unit to let get_ebin_space determine the units
     * for interaction_function[i].longname
     */
    md->ie    = get_ebin_space(md->ebin,f_nre,ener_nm,NULL);
    if (nCrmsd)
    {
        /* This should be called directly after the call for md->ie,
         * such that md->iconrmsd follows directly in the list.
         */
        md->iconrmsd = get_ebin_space(md->ebin,nCrmsd,conrmsd_nm,"");
    }
    if (bDynBox)
    {
        md->ib    = get_ebin_space(md->ebin, bTricl ? NTRICLBOXS :
                                   NBOXS, bTricl ? tricl_boxs_nm : boxs_nm,
                                   unit_length);
        md->ivol  = get_ebin_space(md->ebin, 1, vol_nm,  unit_volume);
        md->idens = get_ebin_space(md->ebin, 1, dens_nm, unit_density_SI);
        md->ipv   = get_ebin_space(md->ebin, 1, pv_nm,   unit_energy);
    }
    if (bConstrVir)
    {
        md->isvir = get_ebin_space(md->ebin,asize(sv_nm),sv_nm,unit_energy);
        md->ifvir = get_ebin_space(md->ebin,asize(fv_nm),fv_nm,unit_energy);
    }
    md->ivir   = get_ebin_space(md->ebin,asize(vir_nm),vir_nm,unit_energy);
    md->ipres  = get_ebin_space(md->ebin,asize(pres_nm),pres_nm,unit_pres_bar);
    md->isurft = get_ebin_space(md->ebin,asize(surft_nm),surft_nm,
                                unit_surft_bar);
    if (epc == epcPARRINELLORAHMAN)
    {
        md->ipc = get_ebin_space(md->ebin,bTricl ? 6 : 3,boxvel_nm,unit_vel);
    }
    md->imu    = get_ebin_space(md->ebin,asize(mu_nm),mu_nm,unit_dipole_D);
    if (ir->cos_accel != 0)
    {
        md->ivcos = get_ebin_space(md->ebin,asize(vcos_nm),vcos_nm,unit_vel);
        md->ivisc = get_ebin_space(md->ebin,asize(visc_nm),visc_nm,
                                   unit_invvisc_SI);
    }
    if (ir->rcoulomb > ir->rlist)
    {
        bEInd[egCOULLR] = TRUE;
    }
    if (!bBHAM)
    {
        if (ir->rvdw > ir->rlist)
        {
            bEInd[egLJLR]   = TRUE;
        }
    }
    else
    {
        bEInd[egLJSR]   = FALSE;
        bEInd[egBHAMSR] = TRUE;
        if (ir->rvdw > ir->rlist)
        {
            bEInd[egBHAMLR]   = TRUE;
        }
    }
    if (b14)
    {
        bEInd[egLJ14] = TRUE;
        bEInd[egCOUL14] = TRUE;
    }
    md->nEc=0;
    for(i=0; (i<egNR); i++)
    {
        if (bEInd[i])
        {
            md->nEc++;
        }
    }
    
    n=groups->grps[egcENER].nr;
    md->nEg=n;
    md->nE=(n*(n+1))/2;
    snew(md->igrp,md->nE);
    if (md->nE > 1)
    {
        n=0;
        snew(gnm,md->nEc);
        for(k=0; (k<md->nEc); k++)
        {
            snew(gnm[k],STRLEN);
        }
        for(i=0; (i<groups->grps[egcENER].nr); i++)
        {
            ni=groups->grps[egcENER].nm_ind[i];
            for(j=i; (j<groups->grps[egcENER].nr); j++)
            {
                nj=groups->grps[egcENER].nm_ind[j];
                for(k=kk=0; (k<egNR); k++)
                {
                    if (bEInd[k])
                    {
                        sprintf(gnm[kk],"%s:%s-%s",egrp_nm[k],
                                *(groups->grpname[ni]),*(groups->grpname[nj]));
                        kk++;
                    }
                }
                md->igrp[n]=get_ebin_space(md->ebin,md->nEc,
                                           (const char **)gnm,unit_energy);
                n++;
            }
        }
        for(k=0; (k<md->nEc); k++)
        {
            sfree(gnm[k]);
        }
        sfree(gnm);
        
        if (n != md->nE)
        {
            gmx_incons("Number of energy terms wrong");
        }
    }
    
    md->nTC=groups->grps[egcTC].nr;
    snew(md->grpnms,md->nTC);
    snew(md->tmp_r,md->nTC);
    snew(md->tmp_v,md->nTC);
    grpnms = md->grpnms;
    for(i=0; (i<md->nTC); i++)
    {
        ni=groups->grps[egcTC].nm_ind[i];
        sprintf(buf,"T-%s",*(groups->grpname[ni]));
        grpnms[i]=strdup(buf);
    }
    md->itemp=get_ebin_space(md->ebin,md->nTC,(const char **)grpnms,
                             unit_temp_K);
    if (etc == etcNOSEHOOVER)
    {
        for(i=0; (i<md->nTC); i++)
        {
            ni=groups->grps[egcTC].nm_ind[i];
            sprintf(buf,"Xi-%s",*(groups->grpname[ni]));
            grpnms[i]=strdup(buf);
        }
        md->itc=get_ebin_space(md->ebin,md->nTC,(const char **)grpnms,
                               unit_invtime);
    }
    else  if (etc == etcBERENDSEN || etc == etcYES || etc == etcVRESCALE)
    {
        for(i=0; (i<md->nTC); i++)
        {
            ni=groups->grps[egcTC].nm_ind[i];
            sprintf(buf,"Lamb-%s",*(groups->grpname[ni]));
            grpnms[i]=strdup(buf);
        }
        md->itc=get_ebin_space(md->ebin,md->nTC,(const char **)grpnms,"");
    }
    sfree(grpnms);
    
    md->nU=groups->grps[egcACC].nr;
    if (md->nU > 1)
    {
        snew(grpnms,3*md->nU);
        for(i=0; (i<md->nU); i++)
        {
            ni=groups->grps[egcACC].nm_ind[i];
            sprintf(buf,"Ux-%s",*(groups->grpname[ni]));
            grpnms[3*i+XX]=strdup(buf);
            sprintf(buf,"Uy-%s",*(groups->grpname[ni]));
            grpnms[3*i+YY]=strdup(buf);
            sprintf(buf,"Uz-%s",*(groups->grpname[ni]));
            grpnms[3*i+ZZ]=strdup(buf);
        }
        md->iu=get_ebin_space(md->ebin,3*md->nU,(const char **)grpnms,unit_vel);
        sfree(grpnms);
    }
    
    if (fp_ene != -1)
    {
        do_enxnms(fp_ene,&md->ebin->nener,&md->ebin->enm);
    }
    
    return md;
}

FILE *open_dhdl(const char *filename,t_inputrec *ir)
{
    FILE *fp;
    const char *dhdl="dH/d\\8l\\4",*deltag="\\8D\\4H",*lambda="\\8l\\4";
    char title[STRLEN],label_x[STRLEN],label_y[STRLEN];
    int  nsets,s;
    char **setname,buf[STRLEN];

    sprintf(label_x,"%s (%s)","Time",unit_time);
    if (ir->n_flambda == 0)
    {
        sprintf(title,"%s",dhdl);
        sprintf(label_y,"%s (%s %s)",
                dhdl,unit_energy,"[\\8l\\4]\\S-1\\N");
    }
    else
    {
        sprintf(title,"%s, %s",dhdl,deltag);
        sprintf(label_y,"(%s)",unit_energy);
    }
    fp = xvgropen(filename,title,label_x,label_y);

    if (ir->n_flambda > 0)
    {
        /* g_bar has to determine the lambda values used in this simulation
         * from this xvg legend.
         */
        nsets = 1 + ir->n_flambda;
        snew(setname,nsets);
        sprintf(buf,"%s %s %g",dhdl,lambda,ir->init_lambda);
        setname[0] = strdup(buf);
        for(s=1; s<nsets; s++)
        {
            sprintf(buf,"%s %s %g",deltag,lambda,ir->flambda[s-1]);
            setname[s] = strdup(buf);
        }
        xvgr_legend(fp,nsets,setname);

        for(s=0; s<nsets; s++)
        {
            sfree(setname[s]);
        }
        sfree(setname);
    }

    return fp;
}

static void copy_energy(real e[],real ecpy[])
{
  int i,j;
  
  for(i=j=0; (i<F_NRE); i++)
    if (bEner[i])
      ecpy[j++] = e[i];
  if (j != f_nre) 
    gmx_incons("Number of energy terms wrong");
}

void upd_mdebin(t_mdebin *md,FILE *fp_dhdl,
                bool bSum,
                double time,
                real tmass,
                gmx_enerdata_t *enerd,
                t_state *state,
                matrix  box,
                tensor svir,
                tensor fvir,
                tensor vir,
                tensor pres,
                gmx_ekindata_t *ekind,
                rvec mu_tot,
                gmx_constr_t constr)
{
    int    i,j,k,kk,m,n,gid;
    real   crmsd[2],tmp6[6];
    real   bs[NTRICLBOXS],vol,dens,pv;
    real   eee[egNR];
    real   ecopy[F_NRE];
    real   tmp;
    
    /* Do NOT use the box in the state variable, but the separate box provided
     * as an argument. This is because we sometimes need to write the box from
     * the last timestep to match the trajectory frames.
     */
    copy_energy(enerd->term,ecopy);
    add_ebin(md->ebin,md->ie,f_nre,ecopy,bSum);
    if (nCrmsd)
    {
        crmsd[0] = constr_rmsd(constr,FALSE);
        if (nCrmsd > 1)
        {
            crmsd[1] = constr_rmsd(constr,TRUE);
        }
        add_ebin(md->ebin,md->iconrmsd,nCrmsd,crmsd,FALSE);
    }
    if (bDynBox)
    {
        if(bTricl)
        {
            bs[0] = box[XX][XX];
            bs[1] = box[YY][XX];
            bs[2] = box[YY][YY];
            bs[3] = box[ZZ][XX];
            bs[4] = box[ZZ][YY];
            bs[5] = box[ZZ][ZZ];
        }
        else
        {
            bs[0] = box[XX][XX];
            bs[1] = box[YY][YY];
            bs[2] = box[ZZ][ZZ];
        }
        vol  = box[XX][XX]*box[YY][YY]*box[ZZ][ZZ];
        dens = (tmass*AMU)/(vol*NANO*NANO*NANO);
        
        /* This is pV (in kJ/mol) */  
        pv = vol*enerd->term[F_PRES]/PRESFAC;

        add_ebin(md->ebin,md->ib   ,NBOXS,bs   ,bSum);
        add_ebin(md->ebin,md->ivol ,1    ,&vol ,bSum);
        add_ebin(md->ebin,md->idens,1    ,&dens,bSum);
        add_ebin(md->ebin,md->ipv  ,1    ,&pv  ,bSum);
    }
    if (bConstrVir)
    {
        add_ebin(md->ebin,md->isvir,9,svir[0],bSum);
        add_ebin(md->ebin,md->ifvir,9,fvir[0],bSum);
    }
    add_ebin(md->ebin,md->ivir,9,vir[0],bSum);
    add_ebin(md->ebin,md->ipres,9,pres[0],bSum);
    tmp = (pres[ZZ][ZZ]-(pres[XX][XX]+pres[YY][YY])*0.5)*box[ZZ][ZZ];
    add_ebin(md->ebin,md->isurft,1,&tmp,bSum);
    if (epc == epcPARRINELLORAHMAN)
    {
        tmp6[0] = state->boxv[XX][XX];
        tmp6[1] = state->boxv[YY][YY];
        tmp6[2] = state->boxv[ZZ][ZZ];
        tmp6[3] = state->boxv[YY][XX];
        tmp6[4] = state->boxv[ZZ][XX];
        tmp6[5] = state->boxv[ZZ][YY];
        add_ebin(md->ebin,md->ipc,bTricl ? 6 : 3,tmp6,bSum);
    }
    add_ebin(md->ebin,md->imu,3,mu_tot,bSum);
    if (ekind && ekind->cosacc.cos_accel != 0)
    {
        vol  = box[XX][XX]*box[YY][YY]*box[ZZ][ZZ];
        dens = (tmass*AMU)/(vol*NANO*NANO*NANO);
        add_ebin(md->ebin,md->ivcos,1,&(ekind->cosacc.vcos),bSum);
        /* 1/viscosity, unit 1/(kg m^-1 s^-1) */
        tmp = 1/(ekind->cosacc.cos_accel/(ekind->cosacc.vcos*PICO)
                 *vol*sqr(box[ZZ][ZZ]*NANO/(2*M_PI)));
        add_ebin(md->ebin,md->ivisc,1,&tmp,bSum);    
    }
    if (md->nE > 1)
    {
        n=0;
        for(i=0; (i<md->nEg); i++)
        {
            for(j=i; (j<md->nEg); j++)
            {
                gid=GID(i,j,md->nEg);
                for(k=kk=0; (k<egNR); k++)
                {
                    if (bEInd[k])
                    {
                        eee[kk++] = enerd->grpp.ener[k][gid];
                    }
                }
                add_ebin(md->ebin,md->igrp[n],md->nEc,eee,bSum);
                n++;
            }
        }
    }
    
    if (ekind)
    {
        for(i=0; (i<md->nTC); i++)
        {
            md->tmp_r[i] = ekind->tcstat[i].T;
        }
        add_ebin(md->ebin,md->itemp,md->nTC,md->tmp_r,bSum);
        if (etc == etcNOSEHOOVER)
        {
            for(i=0; (i<md->nTC); i++)
            {
                 md->tmp_r[i] = state->nosehoover_xi[i];
            }
            add_ebin(md->ebin,md->itc,md->nTC,md->tmp_r,bSum);
        }
        else if (etc == etcBERENDSEN || etc == etcYES || etc == etcVRESCALE)
        {
            for(i=0; (i<md->nTC); i++)
            {
                md->tmp_r[i] = ekind->tcstat[i].lambda;
            }
            add_ebin(md->ebin,md->itc,md->nTC,md->tmp_r,bSum);
        }
    }
    
    if (ekind && md->nU > 1)
    {
        for(i=0; (i<md->nU); i++)
        {
            copy_rvec(ekind->grpstat[i].u,md->tmp_v[i]);
        }
        add_ebin(md->ebin,md->iu,3*md->nU,md->tmp_v[0],bSum);
    }
    
    ebin_increase_count(md->ebin,bSum);
    
    if (fp_dhdl)
    {
        fprintf(fp_dhdl,"%.4f %g",
                time,
                enerd->term[F_DVDL]+enerd->term[F_DKDL]+enerd->term[F_DHDL_CON]);
        for(i=1; i<enerd->n_lambda; i++)
        {
            fprintf(fp_dhdl," %g",
                    enerd->enerpart_lambda[i]-enerd->enerpart_lambda[0]);
        }
        fprintf(fp_dhdl,"\n");
    }
}

void upd_mdebin_step(t_mdebin *md)
{
   ebin_increase_count(md->ebin,FALSE); 
}

static void npr(FILE *log,int n,char c)
{
  for(; (n>0); n--) fprintf(log,"%c",c);
}

static void pprint(FILE *log,const char *s,t_mdebin *md)
{
    char CHAR='#';
    int  slen;
    char buf1[22],buf2[22];
    
    slen = strlen(s);
    fprintf(log,"\t<======  ");
    npr(log,slen,CHAR);
    fprintf(log,"  ==>\n");
    fprintf(log,"\t<====  %s  ====>\n",s);
    fprintf(log,"\t<==  ");
    npr(log,slen,CHAR);
    fprintf(log,"  ======>\n\n");

    fprintf(log,"\tStatistics over %s steps using %s frames\n",
            gmx_step_str(md->ebin->nsteps_sim,buf1),
            gmx_step_str(md->ebin->nsum_sim,buf2));
    fprintf(log,"\n");
}

void print_ebin_header(FILE *log,gmx_step_t steps,double time,real lamb)
{
    char buf[22];

    fprintf(log,"   %12s   %12s   %12s\n"
            "   %12s   %12.5f   %12.5f\n\n",
            "Step","Time","Lambda",gmx_step_str(steps,buf),time,lamb);
}

void print_mc_ratio(FILE *log,int *a,int *b,int c,int d)
{
    char buf[22];
    if(a) {
     fprintf(log,"%s of Accepted Steps %s:   %12.5f\n",
            "%", "[TRANSLATION]",(real)a[MC_TRANSLATE]/((real)b[MC_TRANSLATE]));
     fprintf(log,"%s of Accepted Steps %s:   %12.5f\n",
            "%", "[ROTATION]",(real)(a[MC_ROTATEX]+a[MC_ROTATEY]+a[MC_ROTATEZ])/((real)(b[MC_ROTATEX]+b[MC_ROTATEY]+b[MC_ROTATEZ])));
     fprintf(log,"%s of Accepted Steps %s:   %12.5f\n",
            "%", "[BOND STRETCH]",(real)a[MC_BONDS]/((real)b[MC_BONDS]));
     fprintf(log,"%s of Accepted Steps %s:   %12.5f\n",
            "%", "[ANGLE BEND]",(real)a[MC_ANGLES]/((real)b[MC_ANGLES]));
     fprintf(log,"%s of Accepted Steps %s:   %12.5f\n",
            "%", "[DIHEDRAL ROTATION]",(real)a[MC_DIHEDRALS]/((real)b[MC_DIHEDRALS]));
     fprintf(log,"%s of Accepted Steps %s:   %12.5f\n",
            "%", "[CRA]",(real)a[MC_CRA]/((real)b[MC_CRA]));
     fprintf(log,"%s of Accepted Steps %s:   %12.5f\n",
            "%", "[VOLUME]",(real)c/((real)d));
    }
}

void print_ebin(int fp_ene,bool bEne,bool bDR,bool bOR,
                FILE *log,
                gmx_step_t step,double time,
                int mode,bool bCompact,
                t_mdebin *md,t_fcdata *fcd,
                gmx_groups_t *groups,t_grpopts *opts)
{
    static char **grpnms=NULL;
    char        buf[246];
    int         i,j,n,ni,nj,ndr,nor;
    int         nr[enxNR];
    real        *block[enxNR];
    t_enxframe  fr;
	
    switch (mode)
    {
    case eprNORMAL:
        fr.t            = time;
        fr.step         = step;
        fr.nsteps       = md->ebin->nsteps;
        fr.nsum         = md->ebin->nsum;
        fr.nre          = (bEne) ? md->ebin->nener : 0;
        fr.ener         = md->ebin->e;
        fr.ndisre       = bDR ? fcd->disres.npair : 0;
        fr.disre_rm3tav = fcd->disres.rm3tav;
        fr.disre_rt     = fcd->disres.rt;
        /* Optional additional blocks */
        for(i=0; i<enxNR; i++)
        {
            nr[i] = 0;
        }
        if (fcd->orires.nr > 0 && bOR)
        {
            diagonalize_orires_tensors(&(fcd->orires));
            nr[enxOR]     = fcd->orires.nr;
            block[enxOR]  = fcd->orires.otav;
            nr[enxORI]    = (fcd->orires.oinsl != fcd->orires.otav) ? 
                fcd->orires.nr : 0;
            block[enxORI] = fcd->orires.oinsl;
            nr[enxORT]    = fcd->orires.nex*12;
            block[enxORT] = fcd->orires.eig;
        }
        fr.nblock = 0;
        for(i=0; i<enxNR; i++)
        {
            if (nr[i] > 0)
            {
                fr.nblock = i + 1;
            }
        }
        fr.nr         = nr;
        fr.block      = block;
        if (fr.nre || fr.ndisre || fr.nr[enxOR] || fr.nr[enxORI])
        {
            do_enx(fp_ene,&fr);
            if (fr.nre)
            {
                /* We have stored the sums, so reset the sum history */
                reset_ebin_sums(md->ebin);
            }
        }
        break;
    case eprAVER:
        if (log)
        {
            pprint(log,"A V E R A G E S",md);
        }
        break;
    case eprRMS:
        if (log)
        {
            pprint(log,"R M S - F L U C T U A T I O N S",md);
        }
        break;
    default:
        gmx_fatal(FARGS,"Invalid print mode (%d)",mode);
    }
    
    if (log)
    {
        for(i=0;i<opts->ngtc;i++)
        {
            if(opts->annealing[i]!=eannNO)
            {
                fprintf(log,"Current ref_t for group %s: %8.1f\n",
                        *(groups->grpname[groups->grps[egcTC].nm_ind[i]]),opts->ref_t[i]);
            }
        }
        if (mode==eprNORMAL && fcd->orires.nr>0)
        {
            print_orires_log(log,&(fcd->orires));
        }
        fprintf(log,"   Energies (%s)\n",unit_energy);
        pr_ebin(log,md->ebin,md->ie,f_nre+nCrmsd,5,mode,TRUE);  
        fprintf(log,"\n");
        
        if (!bCompact)
        {
            if (bDynBox)
            {
                pr_ebin(log,md->ebin,md->ib, bTricl ? NTRICLBOXS : NBOXS,5,mode,TRUE);      
                fprintf(log,"\n");
            }
            if (bConstrVir)
            {
                fprintf(log,"   Constraint Virial (%s)\n",unit_energy);
                pr_ebin(log,md->ebin,md->isvir,9,3,mode,FALSE);  
                fprintf(log,"\n");
                fprintf(log,"   Force Virial (%s)\n",unit_energy);
                pr_ebin(log,md->ebin,md->ifvir,9,3,mode,FALSE);  
                fprintf(log,"\n");
            }
            fprintf(log,"   Total Virial (%s)\n",unit_energy);
            pr_ebin(log,md->ebin,md->ivir,9,3,mode,FALSE);   
            fprintf(log,"\n");
            fprintf(log,"   Pressure (%s)\n",unit_pres_bar);
            pr_ebin(log,md->ebin,md->ipres,9,3,mode,FALSE);  
            fprintf(log,"\n");
            fprintf(log,"   Total Dipole (%s)\n",unit_dipole_D);
            pr_ebin(log,md->ebin,md->imu,3,3,mode,FALSE);    
            fprintf(log,"\n");
            
            if (md->nE > 1)
            {
                if (grpnms==NULL)
                {
                    snew(grpnms,md->nE);
                    n=0;
                    for(i=0; (i<md->nEg); i++)
                    {
                        ni=groups->grps[egcENER].nm_ind[i];
                        for(j=i; (j<md->nEg); j++)
                        {
                            nj=groups->grps[egcENER].nm_ind[j];
                            sprintf(buf,"%s-%s",*(groups->grpname[ni]),*(groups->grpname[nj]));
                            grpnms[n++]=strdup(buf);
                        }
                    }
                }
                sprintf(buf,"Epot (%s)",unit_energy);
                fprintf(log,"%15s   ",buf);
                for(i=0; (i<egNR); i++)
                {
                    if (bEInd[i])
                    {
                        fprintf(log,"%12s   ",egrp_nm[i]);
                    }
                }
                fprintf(log,"\n");
                for(i=0; (i<md->nE); i++)
                {
                    fprintf(log,"%15s",grpnms[i]);
                    pr_ebin(log,md->ebin,md->igrp[i],md->nEc,md->nEc,mode,FALSE);
                }
                fprintf(log,"\n");
            }
            if (md->nTC > 1)
            {
                pr_ebin(log,md->ebin,md->itemp,md->nTC,4,mode,TRUE);
                fprintf(log,"\n");
            }
            if (md->nU > 1)
            {
                fprintf(log,"%15s   %12s   %12s   %12s\n",
                        "Group","Ux","Uy","Uz");
                for(i=0; (i<md->nU); i++)
                {
                    ni=groups->grps[egcACC].nm_ind[i];
                    fprintf(log,"%15s",*groups->grpname[ni]);
                    pr_ebin(log,md->ebin,md->iu+3*i,3,3,mode,FALSE);
                }
                fprintf(log,"\n");
            }
        }
    }
}

void 
init_energyhistory(energyhistory_t * enerhist)
{
    enerhist->nener = 0;

    enerhist->ener_ave     = NULL;
    enerhist->ener_sum     = NULL;
    enerhist->ener_sum_sim = NULL;

    enerhist->nsteps     = 0;
    enerhist->nsum       = 0;
    enerhist->nsteps_sim = 0;
    enerhist->nsum_sim   = 0;
}

void
update_energyhistory(energyhistory_t * enerhist,t_mdebin * mdebin)
{
    int i;
    
    enerhist->nsteps     = mdebin->ebin->nsteps;
    enerhist->nsum       = mdebin->ebin->nsum;
    enerhist->nsteps_sim = mdebin->ebin->nsteps_sim;
    enerhist->nsum_sim   = mdebin->ebin->nsum_sim;
    enerhist->nener      = mdebin->ebin->nener;

    if (mdebin->ebin->nsum > 0)
    {
        /* Check if we need to allocate first */
        if(enerhist->ener_ave == NULL)
        {
            snew(enerhist->ener_ave,enerhist->nener);
            snew(enerhist->ener_sum,enerhist->nener);
        }
        
        for(i=0;i<enerhist->nener;i++)
        {
            enerhist->ener_ave[i] = mdebin->ebin->e[i].eav;
            enerhist->ener_sum[i] = mdebin->ebin->e[i].esum;
        }
    }

    if (mdebin->ebin->nsum_sim > 0)
    {
        /* Check if we need to allocate first */
        if(enerhist->ener_sum_sim == NULL)
        {
            snew(enerhist->ener_sum_sim,enerhist->nener);
        }
        
        for(i=0;i<enerhist->nener;i++)
        {
            enerhist->ener_sum_sim[i] = mdebin->ebin->e_sim[i].esum;
        }
    }
}

void
restore_energyhistory_from_state(t_mdebin * mdebin,energyhistory_t * enerhist)
{
    int i;

    if ((enerhist->nsum > 0 || enerhist->nsum_sim > 0) &&
        mdebin->ebin->nener != enerhist->nener)
    {
        gmx_fatal(FARGS,"Mismatch between number of energies in run input (%d) and checkpoint file (%d).",
                  mdebin->ebin->nener,enerhist->nener);
	}

    mdebin->ebin->nsteps     = enerhist->nsteps;
    mdebin->ebin->nsum       = enerhist->nsum;
    mdebin->ebin->nsteps_sim = enerhist->nsteps_sim;
    mdebin->ebin->nsum_sim   = enerhist->nsum_sim;

	for(i=0; i<mdebin->ebin->nener; i++)
	{
		mdebin->ebin->e[i].eav  =
            (enerhist->nsum > 0 ? enerhist->ener_ave[i] : 0);
		mdebin->ebin->e[i].esum =
            (enerhist->nsum > 0 ? enerhist->ener_sum[i] : 0);
        mdebin->ebin->e_sim[i].esum =
            (enerhist->nsum_sim > 0 ? enerhist->ener_sum_sim[i] : 0);
	}
}
